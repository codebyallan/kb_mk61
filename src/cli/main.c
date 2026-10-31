#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "kbcore.h"
#include "kbipc.h"

#define KBCTL_VERSION "0.1.0"
#define KBCTL_MAX_FILE (4 * 1024 * 1024)

static void usage(FILE *f)
{
    fputs("Usage: kbctl <command> [arguments]\n\n", f);
    fputs("Commands:\n", f);
    fputs("  list                          List devices known to kbdd\n", f);
    fputs("  status                        Show daemon status\n", f);
    fputs("  read-profile [index]          (not implemented yet)\n", f);
    fputs("  write-profile <index> <file>  (not implemented: disabled until hardware validation)\n", f);
    fputs("  read-config                   Read daemon configuration summary\n", f);
    fputs("  enable-notifications          Enable input notifications\n", f);
    fputs("  help                          Show this help\n", f);
    fputs("  version                       Show version\n", f);
}

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t w = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, p + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            errno = ECONNRESET;
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ipc_request(uint16_t opcode, const uint8_t *payload, uint32_t plen,
                       uint8_t **out_payload, uint32_t *out_len)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == NULL || runtime[0] == '\0') {
        fprintf(stderr, "kbctl: XDG_RUNTIME_DIR is not set; cannot locate kbdd socket\n");
        return -1;
    }

    char path[512];
    int pn = snprintf(path, sizeof(path), "%s/kbdd.sock", runtime);
    if (pn < 0 || (size_t)pn >= sizeof(path)) {
        fprintf(stderr, "kbctl: socket path too long\n");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fprintf(stderr, "kbctl: socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "kbctl: socket path too long\n");
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "kbctl: cannot connect to kbdd at %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    uint8_t header[KBDD_IPC_HEADER_SIZE];
    uint32_t magic = KBDD_IPC_MAGIC;
    header[0] = (uint8_t)(magic);
    header[1] = (uint8_t)(magic >> 8);
    header[2] = (uint8_t)(magic >> 16);
    header[3] = (uint8_t)(magic >> 24);
    header[4] = (uint8_t)(KBDD_IPC_VERSION);
    header[5] = (uint8_t)(KBDD_IPC_VERSION >> 8);
    header[6] = 0;
    header[7] = 0;
    header[8] = (uint8_t)(plen);
    header[9] = (uint8_t)(plen >> 8);
    header[10] = (uint8_t)(plen >> 16);
    header[11] = (uint8_t)(plen >> 24);
    header[12] = (uint8_t)(opcode);
    header[13] = (uint8_t)(opcode >> 8);
    header[14] = 1;
    header[15] = 0;

    if (send_all(fd, header, sizeof(header)) != 0) {
        fprintf(stderr, "kbctl: send: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (plen > 0 && payload != NULL) {
        if (send_all(fd, payload, plen) != 0) {
            fprintf(stderr, "kbctl: send payload: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
    }

    uint8_t rheader[KBDD_IPC_HEADER_SIZE];
    if (recv_all(fd, rheader, sizeof(rheader)) != 0) {
        fprintf(stderr, "kbctl: recv: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    uint32_t rmagic = rd32(rheader);
    if (rmagic != KBDD_IPC_MAGIC) {
        fprintf(stderr, "kbctl: bad response magic\n");
        close(fd);
        return -1;
    }
    uint16_t rversion = (uint16_t)(rheader[4] | (rheader[5] << 8));
    if (rversion != KBDD_IPC_VERSION) {
        fprintf(stderr, "kbctl: incompatible daemon version %u\n", (unsigned)rversion);
        close(fd);
        return -1;
    }
    uint32_t rplen = rd32(rheader + 8);
    if (rplen > KBDD_IPC_MAX_PAYLOAD) {
        fprintf(stderr, "kbctl: response too large\n");
        close(fd);
        return -1;
    }

    uint8_t *rbuf = NULL;
    if (rplen > 0) {
        rbuf = malloc(rplen);
        if (rbuf == NULL) {
            fprintf(stderr, "kbctl: out of memory\n");
            close(fd);
            return -1;
        }
        if (recv_all(fd, rbuf, rplen) != 0) {
            fprintf(stderr, "kbctl: recv payload: %s\n", strerror(errno));
            free(rbuf);
            close(fd);
            return -1;
        }
    }

    close(fd);
    *out_payload = rbuf;
    *out_len = rplen;
    return 0;
}

/* Prints the reply; returns 1 if the daemon answered with an error. */
static int print_reply(const uint8_t *data, uint32_t len)
{
    int is_err = (len >= 6 && memcmp(data, "error:", 6) == 0);
    if (len == 0 || data == NULL) {
        return 0;
    }
    fwrite(data, 1, len > 0 && data[len - 1] == '\0' ? len - 1 : len, is_err ? stderr : stdout);
    fputc('\n', is_err ? stderr : stdout);
    return is_err;
}

static void print_payload(const uint8_t *data, uint32_t len)
{
    (void)print_reply(data, len);
}

static int parse_index(const char *s, long *out)
{
    if (s == NULL || s[0] == '\0') {
        return -1;
    }
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == NULL || *end != '\0') {
        return -1;
    }
    if (v < 0 || v > 3) {
        return -1;
    }
    *out = v;
    return 0;
}

static int read_file(const char *path, uint8_t **out_buf, size_t *out_len)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (st.st_size < 0 || (size_t)st.st_size > KBCTL_MAX_FILE) {
        close(fd);
        errno = EFBIG;
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    uint8_t *buf = malloc(sz > 0 ? sz : 1);
    if (buf == NULL) {
        close(fd);
        errno = ENOMEM;
        return -1;
    }
    size_t got = 0;
    while (got < sz) {
        ssize_t r = read(fd, buf + got, sz - got);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            close(fd);
            return -1;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
    }
    close(fd);
    if (got != sz) {
        free(buf);
        errno = EIO;
        return -1;
    }
    *out_buf = buf;
    *out_len = sz;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
        strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("kbctl %s (libkbcore %s)\n", KBCTL_VERSION, kbc_version_string());
        return 0;
    }

    if (strcmp(cmd, "list") == 0) {
        if (argc != 2) {
            usage(stderr);
            return 2;
        }
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (ipc_request(KBDD_IPC_OP_LIST_DEVICES, NULL, 0, &resp, &rlen) != 0) {
            return 1;
        }
        print_payload(resp, rlen);
        free(resp);
        return 0;
    }

    if (strcmp(cmd, "status") == 0) {
        if (argc != 2) {
            usage(stderr);
            return 2;
        }
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (ipc_request(KBDD_IPC_OP_STATUS, NULL, 0, &resp, &rlen) != 0) {
            return 1;
        }
        print_payload(resp, rlen);
        free(resp);
        return 0;
    }

    if (strcmp(cmd, "read-profile") == 0) {
        long idx = 0;
        if (argc == 3) {
            if (parse_index(argv[2], &idx) != 0) {
                fprintf(stderr, "kbctl: invalid profile index (expected 0-3)\n");
                return 2;
            }
        } else if (argc != 2) {
            usage(stderr);
            return 2;
        }
        uint8_t payload[1] = {(uint8_t)idx};
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (ipc_request(KBDD_IPC_OP_READ_PROFILE, payload, 1, &resp, &rlen) != 0) {
            return 1;
        }
        int perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }

    if (strcmp(cmd, "write-profile") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 2;
        }
        long idx = 0;
        if (parse_index(argv[2], &idx) != 0) {
            fprintf(stderr, "kbctl: invalid profile index (expected 0-3)\n");
            return 2;
        }
        uint8_t *filebuf = NULL;
        size_t filelen = 0;
        if (read_file(argv[3], &filebuf, &filelen) != 0) {
            fprintf(stderr, "kbctl: cannot read '%s': %s\n", argv[3], strerror(errno));
            return 1;
        }

        size_t plen = 1 + filelen;
        if (plen > KBDD_IPC_MAX_PAYLOAD) {
            fprintf(stderr, "kbctl: profile file too large\n");
            free(filebuf);
            return 1;
        }
        uint8_t *payload = malloc(plen > 0 ? plen : 1);
        if (payload == NULL) {
            fprintf(stderr, "kbctl: out of memory\n");
            free(filebuf);
            return 1;
        }
        payload[0] = (uint8_t)idx;
        if (filelen > 0) {
            memcpy(payload + 1, filebuf, filelen);
        }
        free(filebuf);

        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        int rc = ipc_request(KBDD_IPC_OP_WRITE_PROFILE, payload, (uint32_t)plen, &resp, &rlen);
        free(payload);
        if (rc != 0) {
            free(resp);
            return 1;
        }
        int perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }

    if (strcmp(cmd, "read-config") == 0) {
        if (argc != 2) {
            usage(stderr);
            return 2;
        }
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (ipc_request(KBDD_IPC_OP_READ_CONFIG, NULL, 0, &resp, &rlen) != 0) {
            return 1;
        }
        print_payload(resp, rlen);
        free(resp);
        return 0;
    }

    if (strcmp(cmd, "enable-notifications") == 0) {
        if (argc != 2) {
            usage(stderr);
            return 2;
        }
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (ipc_request(KBDD_IPC_OP_ENABLE_NOTIFICATIONS, NULL, 0, &resp, &rlen) != 0) {
            return 1;
        }
        print_payload(resp, rlen);
        free(resp);
        return 0;
    }

    fprintf(stderr, "kbctl: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}