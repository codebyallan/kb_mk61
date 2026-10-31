#include "kbdd.h"

#include "kbcore.h"

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
    int allow_led;                       /* KBDD_ENABLE_LED=1 */
    int allow_profile_write;             /* KBDD_ENABLE_PROFILE_WRITE=1 */
    int led_hold;                        /* streaming active */
    int led_active;                      /* a host-controlled frame is installed */
    uint8_t led_frame[KBC_LED_BUFFER_SIZE];
    uint8_t led_base_frame[KBC_LED_BUFFER_SIZE];
    uint8_t led_brightness;              /* host-frame scale, 0..10 */
    size_t led_stride;
    int led_fail;
    long long led_next_ms;
} kbdd_ctx_t;

static int kbdd_reply_text(kbdd_ipc_message_t *resp, const char *msg);

static long long kbdd_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

/* First control interface (usage ff01/0001) of a known model, opened. */
static const kbdd_hid_device_t *kbdd_control_dev(const kbdd_ctx_t *k)
{
    size_t i;
    if (k == NULL) return NULL;
    for (i = 0; i < k->count; i++) {
        const kbdd_hid_device_t *d = &k->devices[i];
        if (d->role == 0 && d->fd >= 0 && d->model != NULL) return d;
    }
    return NULL;
}

static int kbdd_reply_bytes(kbdd_ipc_message_t *resp, const uint8_t *data, size_t n)
{
    resp->payload = malloc(n > 0 ? n : 1);
    if (resp->payload == NULL) return -1;
    memcpy(resp->payload, data, n);
    resp->payload_len = (uint32_t)n;
    return 0;
}

static const char *kbdd_dev_err(int rc)
{
    switch (rc) {
    case KBDD_DEV_EINVAL: return "error: invalid request";
    case KBDD_DEV_EIO:    return "error: device I/O failed (is the keyboard connected and the udev rule installed?)";
    case KBDD_DEV_EPROTO: return "error: unexpected response from the device (layout check failed, nothing written)";
    case KBDD_DEV_EVERIFY: return "error: write not confirmed by read-back; original block restored";
    default:              return "error: unknown";
    }
}

static int kbdd_handle_led(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                           kbdd_ipc_message_t *resp)
{
    const uint8_t *p = req->payload;
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    size_t matrix, stride, count;
    uint8_t flags, mode, pos, r, g, b;
    int rc;

    if (!k->allow_led) {
        return kbdd_reply_text(resp,
            "error: LED control is disabled. Start kbdd with KBDD_ENABLE_LED=1");
    }
    if (p == NULL || req->payload_len != KBDD_LED_PAYLOAD_SIZE) {
        return kbdd_reply_text(resp, "error: bad LED payload");
    }
    flags = p[0]; mode = p[2]; pos = p[3]; r = p[4]; g = p[5]; b = p[6];

    if (mode == KBDD_LED_MODE_RELEASE) {
        k->led_hold = 0;
        k->led_active = 0;
        return kbdd_reply_text(resp, "ok: LED streaming stopped");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    matrix = dev->model->matrix_size;
    stride = p[1] != 0 ? (size_t)p[1] : matrix;
    if (kbc_led_frame_page_count(stride) == 0) {
        return kbdd_reply_text(resp, "error: invalid stride (1..144)");
    }
    count = matrix < stride ? matrix : stride;

    memset(k->led_frame, 0, sizeof(k->led_frame));
    memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
    if (mode == KBDD_LED_MODE_SOLID) {
        kbc_led_frame_fill(k->led_base_frame, stride, count, r, g, b);
    } else if (mode == KBDD_LED_MODE_POS) {
        if (pos >= count) {
            return kbdd_reply_text(resp, "error: position out of range");
        }
        kbc_led_frame_set(k->led_base_frame, stride, pos, r, g, b);
    } else {
        return kbdd_reply_text(resp, "error: unknown LED mode");
    }
    memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
    if (kbc_led_frame_apply_brightness(k->led_frame, stride, k->led_brightness) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid LED brightness");
    }

    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_send_frame(&io, k->led_frame, stride);
    if (rc != KBDD_DEV_OK) {
        k->led_hold = 0;
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    k->led_stride = stride;
    k->led_active = 1;
    k->led_fail = 0;
    k->led_next_ms = kbdd_now_ms() + 40;
    k->led_hold = (flags & KBDD_LED_FLAG_HOLD) ? 1 : 0;
    return kbdd_reply_text(resp, k->led_hold
        ? "ok: LED frame sent, streaming every 40 ms (kbctl led release to stop)"
        : "ok: LED frame sent once");
}

static int kbdd_handle_effect(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                              kbdd_ipc_message_t *resp)
{
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    uint8_t prof = 0;
    char buf[160];
    int rc;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 1) {
        return kbdd_reply_text(resp, "error: payload must be [mode]");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    k->led_hold = 0; /* stop host streaming so the effect is visible */
    k->led_active = 0;
    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_set_effect(&io, req->payload[0], &prof);
    if (rc != KBDD_DEV_OK) {
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    snprintf(buf, sizeof(buf), "ok: effect %s (%u) set on profile %u, verified by read-back",
             kbc_led_effect_name(req->payload[0]), (unsigned)req->payload[0], (unsigned)prof);
    return kbdd_reply_text(resp, buf);
}

static int kbdd_handle_field(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                             kbdd_ipc_message_t *resp)
{
    static const char *const names[] = {"?", "effect", "brightness", "speed"};
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    uint8_t prof = 0;
    char buf[160];
    int rc, field, resume_led;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 2 || req->payload[0] < 1 || req->payload[0] > 3) {
        return kbdd_reply_text(resp, "error: payload must be [field 1..3, value]");
    }
    field = req->payload[0];
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    resume_led = (field == KBC_FIELD_BRIGHTNESS) ? k->led_hold : 0;
    k->led_hold = 0;
    if (field != KBC_FIELD_BRIGHTNESS) {
        k->led_active = 0;
    }
    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_set_field(&io, field, req->payload[1], &prof);
    if (rc != KBDD_DEV_OK) {
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    if (field == KBC_FIELD_BRIGHTNESS) {
        k->led_brightness = req->payload[1];
        if (k->led_active) {
            memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
            if (kbc_led_frame_apply_brightness(k->led_frame, k->led_stride,
                                               k->led_brightness) != KBC_OK ||
                kbdd_dev_send_frame(&io, k->led_frame, k->led_stride) != KBDD_DEV_OK) {
                k->led_active = 0;
                return kbdd_reply_text(resp, "error: brightness changed, but solid LED frame could not be refreshed");
            }
            k->led_next_ms = kbdd_now_ms() + 40;
            k->led_hold = resume_led;
        }
    }
    snprintf(buf, sizeof(buf), "ok: %s set to %u on profile %u, verified by read-back",
             names[field], (unsigned)req->payload[1], (unsigned)prof);
    return kbdd_reply_text(resp, buf);
}

static void kbdd_led_tick(kbdd_ctx_t *k)
{
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    long long now;

    if (!k->led_hold) return;
    now = kbdd_now_ms();
    if (now < k->led_next_ms) return;
    dev = kbdd_control_dev(k);
    if (dev == NULL) { k->led_hold = 0; return; }
    kbdd_dev_io_init_hidraw(&io, dev);
    if (kbdd_dev_send_frame(&io, k->led_frame, k->led_stride) != KBDD_DEV_OK) {
        if (++k->led_fail >= 5) {
            k->led_hold = 0;
            fprintf(stderr, "kbdd: LED streaming stopped after repeated I/O errors\n");
        }
    } else {
        k->led_fail = 0;
    }
    k->led_next_ms = now + 40;
}

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
    kbdd_ctx_t *kctx = (kbdd_ctx_t *)ctx;
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
        case KBDD_IPC_OP_READ_ACTIVE: {
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);
            kbdd_dev_io_t io;
            uint8_t out[264];
            char buf[160];
            int rc;
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            kbdd_dev_io_init_hidraw(&io, dev);
            rc = kbdd_dev_query(&io, KBC_OP_READ_ACTIVE_PROFILE, 0, out);
            if (rc != KBDD_DEV_OK) {
                return kbdd_reply_text(resp, kbdd_dev_err(rc));
            }
            /* hardware reply: 07 81 <profile> ... (profile in byte 2) */
            if (out[1] == KBC_OP_READ_ACTIVE_PROFILE && out[2] >= 1 && out[2] <= 4) {
                snprintf(buf, sizeof(buf), "active profile: %u", (unsigned)out[2]);
            } else {
                snprintf(buf, sizeof(buf),
                         "unexpected reply; first bytes: "
                         "%02x %02x %02x %02x %02x %02x %02x %02x",
                         out[0], out[1], out[2], out[3], out[4],
                         out[5], out[6], out[7]);
            }
            return kbdd_reply_text(resp, buf);
        }
        case KBDD_IPC_OP_READ_BLOCK: {
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);
            kbdd_dev_io_t io;
            uint8_t out[264];
            int rc;
            if (req->payload == NULL || req->payload_len != 1) {
                return kbdd_reply_text(resp, "error: payload must be [profile]");
            }
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            kbdd_dev_io_init_hidraw(&io, dev);
            rc = kbdd_dev_query(&io, KBC_OP_READ_PROFILE_BLOCK, req->payload[0], out);
            if (rc != KBDD_DEV_OK) {
                return kbdd_reply_text(resp, kbdd_dev_err(rc));
            }
            return kbdd_reply_bytes(resp, out, sizeof(out));
        }
        case KBDD_IPC_OP_LED_SET:
            return kbdd_handle_led(kctx, req, resp);
        case KBDD_IPC_OP_SET_EFFECT:
            return kbdd_handle_effect(kctx, req, resp);
        case KBDD_IPC_OP_SET_FIELD:
            return kbdd_handle_field(kctx, req, resp);
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

    kbdd_ctx_t kctx;
    memset(&kctx, 0, sizeof(kctx));
    kctx.led_brightness = KBC_BRIGHTNESS_RAW_MAX;
    kctx.devices = devices;
    kctx.count = device_count;
    {
        const char *want_led = getenv("KBDD_ENABLE_LED");
        const char *want_pw = getenv("KBDD_ENABLE_PROFILE_WRITE");
        kctx.allow_led = (want_led != NULL && strcmp(want_led, "1") == 0);
        kctx.allow_profile_write = (want_pw != NULL && strcmp(want_pw, "1") == 0);
    }

    /* main loop */
    while (!kbdd_stop_flag) {
        if (ipc != NULL) {
            kbdd_ipc_server_poll(ipc, kctx.led_hold ? 20 : 50, kbdd_ipc_handle, &kctx);
        } else {
            /* sleep briefly */
            struct timespec ts = {0, 50000000L};
            nanosleep(&ts, NULL);
        }

        kbdd_led_tick(&kctx);

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