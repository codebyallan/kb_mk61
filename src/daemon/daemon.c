#include "kbdd.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t kbdd_stop_flag = 0;

static void kbdd_signal_handler(int sig)
{
    (void)sig;
    kbdd_stop_flag = 1;
}

static const kbdd_model_t KBDD_MODELS[] = {
    {"MK61L", 0x1EA7, 0x9018, 70, 61},
    {"MK68M", 0x1EA7, 0x9050, 75, 68},
    {"GK8150", 0x2442, 0x0011, 75, 68},
    {"GK8150", 0x2442, 0xB011, 75, 68},
    {"GK8150", 0x2442, 0xB014, 75, 68},
};

static int kbdd_ensure_dir(const char *path)
{
    struct stat st;
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (lstat(path, &st) != 0) {
        if (errno != ENOENT) {
            return -1;
        }
        if (mkdir(path, 0700) != 0) {
            return -1;
        }
        if (lstat(path, &st) != 0) {
            return -1;
        }
    }
    if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        return -1;
    }
    if (st.st_uid != getuid()) {
        return -1;
    }
    if ((st.st_mode & 0777) != 0700) {
        /* tighten permissions if owned by us */
        if (chmod(path, 0700) != 0) {
            return -1;
        }
    }
    return 0;
}

static int kbdd_config_dir(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        if (snprintf(buf, sz, "%s/kbdd", xdg) >= (int)sz) {
            return -1;
        }
    } else {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return -1;
        }
        if (snprintf(buf, sz, "%s/.config/kbdd", home) >= (int)sz) {
            return -1;
        }
    }
    /* create parent directory if needed (best effort) */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", buf);
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (parent[0] != '\0') {
            mkdir(parent, 0755);
        }
    }
    if (kbdd_ensure_dir(buf) != 0) {
        return -1;
    }
    return 0;
}

static int kbdd_runtime_dir(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg == NULL || xdg[0] == '\0') {
        return -1;
    }
    if (snprintf(buf, sz, "%s", xdg) >= (int)sz) {
        return -1;
    }
    return 0;
}

static int kbdd_acquire_lock(void)
{
    char rdir[512];
    if (kbdd_runtime_dir(rdir, sizeof(rdir)) != 0) {
        return -1;
    }
    char lockpath[768];
    snprintf(lockpath, sizeof(lockpath), "%s/kbdd.lock", rdir);
    int fd = open(lockpath, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

typedef struct {
    const kbdd_hid_device_t *devices;
    size_t count;
} kbdd_ctx_t;

static int kbdd_reply_text(kbdd_ipc_message_t *resp, const char *msg)
{
    size_t n = strlen(msg);
    resp->payload = malloc(n + 1);
    if (resp->payload == NULL) {
        return -1;
    }
    memcpy(resp->payload, msg, n + 1);
    resp->payload_len = (uint32_t)(n + 1);
    return 0;
}

static int kbdd_ipc_handle(const kbdd_ipc_message_t *req, kbdd_ipc_message_t *resp,
                           void *ctx)
{
    const kbdd_ctx_t *kctx = (const kbdd_ctx_t *)ctx;
    if (req == NULL || resp == NULL) {
        return -1;
    }
    resp->version = KBDD_IPC_VERSION;
    resp->request_id = req->request_id;
    resp->opcode = req->opcode;

    switch (req->opcode) {
        case KBDD_IPC_OP_PING: {
            const char *msg = "pong";
            size_t n = strlen(msg);
            resp->payload = malloc(n + 1);
            if (resp->payload == NULL) {
                return -1;
            }
            memcpy(resp->payload, msg, n);
            resp->payload[n] = '\0';
            resp->payload_len = (uint32_t)(n + 1);
            return 0;
        }
        case KBDD_IPC_OP_GET_VERSION: {
            const char *msg = "kbdd 0.1.0";
            size_t n = strlen(msg);
            resp->payload = malloc(n + 1);
            if (resp->payload == NULL) {
                return -1;
            }
            memcpy(resp->payload, msg, n);
            resp->payload[n] = '\0';
            resp->payload_len = (uint32_t)(n + 1);
            return 0;
        }
        case KBDD_IPC_OP_LIST_DEVICES: {
            /* Read-only: what hidraw enumeration found. No device I/O. */
            char buf[4096];
            size_t off = 0;
            size_t n = (kctx != NULL) ? kctx->count : 0;
            if (n == 0) {
                return kbdd_reply_text(resp, "no devices");
            }
            for (size_t i = 0; i < n && off < sizeof(buf); i++) {
                const kbdd_hid_device_t *d = &kctx->devices[i];
                int w = snprintf(buf + off, sizeof(buf) - off,
                                 "%s  %04x:%04x  %s  usage=%04x/%04x  role=%s\n",
                                 d->path, (unsigned)d->vid, (unsigned)d->pid,
                                 d->model != NULL ? d->model->name : "?",
                                 (unsigned)d->usage_page, (unsigned)d->usage,
                                 d->role == 0 ? "control" : "input");
                if (w < 0 || (size_t)w >= sizeof(buf) - off) {
                    break;
                }
                off += (size_t)w;
            }
            return kbdd_reply_text(resp, buf);
        }
        case KBDD_IPC_OP_SAVE_PROFILE:
            /* Disabled on purpose: the protocol is not yet validated on real
             * hardware (spec/QUESTIONS.md Q1, Q5, Q8, Q10). Nothing is sent. */
            return kbdd_reply_text(resp,
                "error: write-profile not implemented (disabled until hardware validation)");
        case KBDD_IPC_OP_STATUS: {
            char buf[96];
            snprintf(buf, sizeof(buf), "kbdd 0.1.0: running, %zu hid interface(s)",
                     kctx != NULL ? kctx->count : (size_t)0);
            return kbdd_reply_text(resp, buf);
        }
        case KBDD_IPC_OP_READ_PROFILE:
            return kbdd_reply_text(resp, "error: read-profile not implemented");
        case KBDD_IPC_OP_READ_CONFIG: {
            const char *msg = "config: <XDG_CONFIG_HOME>/kbdd";
            size_t n = strlen(msg);
            resp->payload = malloc(n + 1);
            if (resp->payload == NULL) {
                return -1;
            }
            memcpy(resp->payload, msg, n);
            resp->payload[n] = '\0';
            resp->payload_len = (uint32_t)(n + 1);
            return 0;
        }
        case KBDD_IPC_OP_ENABLE_NOTIFICATIONS: {
            const char *msg = "notifications enabled";
            size_t n = strlen(msg);
            resp->payload = malloc(n + 1);
            if (resp->payload == NULL) {
                return -1;
            }
            memcpy(resp->payload, msg, n);
            resp->payload[n] = '\0';
            resp->payload_len = (uint32_t)(n + 1);
            return 0;
        }
        default: {
            const char *msg = "error: unsupported command";
            size_t n = strlen(msg);
            resp->payload = malloc(n + 1);
            if (resp->payload == NULL) {
                return -1;
            }
            memcpy(resp->payload, msg, n);
            resp->payload[n] = '\0';
            resp->payload_len = (uint32_t)(n + 1);
            return 0;
        }
    }
}

int kbdd_run(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* signal handling */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = kbdd_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int lock_fd = kbdd_acquire_lock();
    if (lock_fd < 0) {
        fprintf(stderr, "kbdd: another instance is running or runtime dir missing\n");
        return 1;
    }

    /* enumerate devices (graceful if none) */
    kbdd_hid_device_t *devices = NULL;
    size_t device_count = 0;
    kbdd_hidraw_enumerate(KBDD_MODELS, sizeof(KBDD_MODELS) / sizeof(KBDD_MODELS[0]),
                          &devices, &device_count);

    /* open devices */
    for (size_t i = 0; i < device_count; i++) {
        kbdd_hidraw_open(&devices[i]);
    }

    /* uinput (optional, mockable) */
    /* Off by default: nothing uses the virtual keyboard yet, and creating it
     * needs /dev/uinput access. Opt in with KBDD_ENABLE_UINPUT=1. */
    kbdd_uinput_t *uinput = NULL;
    const char *want_uinput = getenv("KBDD_ENABLE_UINPUT");
    if (want_uinput != NULL && strcmp(want_uinput, "1") == 0) {
        uinput = kbdd_uinput_open(NULL);
        if (uinput != NULL) {
            kbdd_uinput_create_keyboard(uinput);
        }
    }

    /* ipc server (optional if runtime dir present) */
    kbdd_ipc_server_t *ipc = kbdd_ipc_server_create();

    char cdir[512];
    if (kbdd_config_dir(cdir, sizeof(cdir)) == 0) {
        /* ensure 0700 */
    }

    kbdd_ctx_t kctx = {devices, device_count};

    /* main loop */
    while (!kbdd_stop_flag) {
        if (ipc != NULL) {
            kbdd_ipc_server_poll(ipc, 50, kbdd_ipc_handle, &kctx);
        } else {
            /* sleep briefly */
            struct timespec ts = {0, 50000000L};
            nanosleep(&ts, NULL);
        }

        /* read input reports from devices (nonblocking) */
        for (size_t i = 0; i < device_count; i++) {
            if (devices[i].role != 1) {
                continue;
            }
            if (devices[i].fd < 0) {
                continue;
            }
            uint8_t buf[256];
            ssize_t r = kbdd_hidraw_read(&devices[i], buf, sizeof(buf));
            if (r > 0) {
                /* parse input report via libkbcore? skip logging */
            }
        }
    }

    /* cleanup */
    if (ipc != NULL) {
        kbdd_ipc_server_destroy(ipc);
    }
    if (uinput != NULL) {
        kbdd_uinput_close(uinput);
    }
    kbdd_hidraw_free(devices, device_count);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }

    return 0;
}