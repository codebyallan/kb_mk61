#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "kbcore.h"
#include "kbipc.h"

#define KBCTL_VERSION "0.3.2"
#define KBCTL_MAX_FILE (4 * 1024 * 1024)

static void usage(FILE *f)
{
    fputs("Usage: kbctl <command> [arguments]\n\n", f);
    fputs("Commands:\n", f);
    fputs("  list                          List devices known to kbdd\n", f);
    fputs("  status                        Show daemon status\n", f);
    fputs("  read-profile [index]          Read a profile (index 0-3)\n", f);
    fputs("  write-profile <index> <file>  Write a profile file (index 0-3)\n", f);
    fputs("  read-config                   Read daemon configuration summary\n", f);
    fputs("  enable-notifications          Enable input notifications\n", f);
    fputs("  watch                         Print keyboard profile/layer/effect events\n", f);
    fputs("  profile <1-4>                 Switch the active profile (needs KBDD_ENABLE_PROFILE_WRITE=1)\n", f);
    fputs("  active-profile                Ask the keyboard which profile is active (0x81)\n", f);
    fputs("  dump-block <1-4> [file]       Read the profile parameter block (0x82), hex dump\n", f);
    fputs("  diff-block <a.bin> <b.bin>    Compare two saved dump-block files\n", f);
    fputs("  led solid <RRGGBB> [opts]     All keys one colour (needs KBDD_ENABLE_LED=1)\n", f);
    fputs("  led off [opts]                All keys black\n", f);
    fputs("  led pos <n> <RRGGBB> [opts]   Set one position, preserving other colors\n", f);
    fputs("  led clear [opts]              Clear all customized colors\n", f);
    fputs("  led sweep [opts]              Light positions one by one to map keys\n", f);
    fputs("  led anim <breath|cycle|rainbow> [opts]  Animate colors from the computer (25 fps)\n", f);
    fputs("  led key <keys> <RRGGBB>       Color keys by name: w,a,s,d | wasd | esc,numbers (see: led keys)\n", f);
    fputs("  led keys                      List key names and matrix positions (offline)\n", f);
    fputs("  led release                   Stop streaming frames\n", f);
    fputs("  led effects                   List the keyboard built-in effects (offline)\n", f);
    fputs("  led effect <name|index>       Set the active profile effect (needs KBDD_ENABLE_PROFILE_WRITE=1)\n", f);
    fputs("  led brightness <0-10>         Set brightness (also scales solid colors; 0 = off)\n", f);
    fputs("  led speed <2-102>             Set the effect speed (raw, lower = faster; shared by all effects)\n", f);
    fputs("    opts: --once (send one frame, no streaming)  --stride N (frame layout, default = matrix size)\n", f);
    fputs("          sweep: --delay MS  --color RRGGBB  --from N\n", f);
    fputs("  help                          Show this help\n", f);
    fputs("  version                       Show version\n", f);
    fputs("\nUse 'kbctl help <command>' for detailed help.\n", f);
}

static void usage_command(FILE *f, const char *command)
{
    if (strcmp(command, "list") == 0) {
        fputs("Usage: kbctl list\n\nList the HID devices discovered by kbdd, including model and path.\n", f);
    } else if (strcmp(command, "status") == 0) {
        fputs("Usage: kbctl status\n\nShow daemon status, connected devices, and enabled capabilities.\n", f);
    } else if (strcmp(command, "read-profile") == 0) {
        fputs("Usage: kbctl read-profile [index]\n\nRead a profile by index (0-3). With no index, read profile 0.\n", f);
    } else if (strcmp(command, "write-profile") == 0) {
        fputs("Usage: kbctl write-profile <index> <file>\n\nWrite a 264-byte profile file to index 0-3. Profile writes require\n"
              "KBDD_ENABLE_PROFILE_WRITE=1 and are backed up by kbctl first.\n", f);
    } else if (strcmp(command, "read-config") == 0) {
        fputs("Usage: kbctl read-config\n\nRead the daemon configuration summary.\n", f);
    } else if (strcmp(command, "enable-notifications") == 0) {
        fputs("Usage: kbctl enable-notifications\n\nEnable keyboard input notifications through kbdd.\n", f);
    } else if (strcmp(command, "watch") == 0) {
        fputs("Usage: kbctl watch\n\nPrint keyboard profile, layer, and effect events until Ctrl+C.\n", f);
    } else if (strcmp(command, "profile") == 0) {
        fputs("Usage: kbctl profile <1-4>\n\nSwitch the keyboard's active profile (hardware numbering 1-4) and confirm it with\n"
              "a read-back. Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(command, "active-profile") == 0) {
        fputs("Usage: kbctl active-profile\n\nQuery the keyboard for its active profile.\n", f);
    } else if (strcmp(command, "dump-block") == 0) {
        fputs("Usage: kbctl dump-block <1-4> [file]\n\nRead and hex-dump one raw 264-byte profile parameter block. If file\n"
              "is supplied, save the raw block there.\n", f);
    } else if (strcmp(command, "diff-block") == 0) {
        fputs("Usage: kbctl diff-block <a.bin> <b.bin>\n\nCompare two saved 264-byte profile blocks and print changed offsets.\n", f);
    } else if (strcmp(command, "led") == 0) {
        fputs("Usage: kbctl led <subcommand> [arguments]\n\nUse 'kbctl help led <subcommand>' for a specific LED command.\n", f);
    } else if (strcmp(command, "version") == 0) {
        fputs("Usage: kbctl version\n\nPrint the kbctl and libkbcore versions.\n", f);
    } else {
        fprintf(f, "kbctl: no help available for '%s'\n", command);
    }
}

static void usage_led(FILE *f, const char *subcommand)
{
    if (subcommand == NULL || strcmp(subcommand, "led") == 0) {
        fputs("Usage: kbctl led <subcommand> [arguments]\n\n"
              "Subcommands:\n"
              "  solid <RRGGBB> [opts]       Fill all matrix positions\n"
              "  off [opts]                  Turn all positions off\n"
              "  pos <n> <RRGGBB> [opts]     Set one position, preserving others\n"
              "  clear [opts]                Clear the host-controlled frame\n"
              "  sweep [opts]                Light positions sequentially\n"
              "  release                     Stop periodic frame streaming\n"
              "  effects                     List built-in keyboard effects\n"
              "  effect <name|index>         Set the active profile effect\n"
              "  brightness <0-10>           Set brightness\n"
              "  speed <2-102>               Set effect speed\n\n"
              "Options for solid/off/pos/clear: --once, --stride N\n"
              "Options for sweep: --delay MS, --color RRGGBB, --from N\n"
              "Requires KBDD_ENABLE_LED=1 for frame commands.\n", f);
        return;
    }
    if (strcmp(subcommand, "solid") == 0) {
        fputs("Usage: kbctl led solid <RRGGBB> [--once] [--stride N]\n\n"
              "Set every matrix position to one RGB color. RRGGBB is hexadecimal.\n", f);
    } else if (strcmp(subcommand, "off") == 0) {
        fputs("Usage: kbctl led off [--once] [--stride N]\n\nTurn every matrix position off.\n", f);
    } else if (strcmp(subcommand, "pos") == 0) {
        fputs("Usage: kbctl led pos <n> <RRGGBB> [--once] [--stride N]\n\n"
              "Set position n (zero-based) to a color while preserving other positions.\n", f);
    } else if (strcmp(subcommand, "clear") == 0) {
        fputs("Usage: kbctl led clear [--once] [--stride N]\n\nClear every position in the host-controlled frame.\n", f);
    } else if (strcmp(subcommand, "anim") == 0) {
        fputs("Usage: kbctl led anim <breath|cycle|rainbow> [--color RRGGBB] [--period MS] [--stride N]\n\n"
              "Stream a host-computed animation at 25 fps (period 200-60000 ms; --color applies to breath,\n"
              "default white). Brightness scales it. The selected animation is saved and restored after reconnect;\n"
              "use 'led clear' to remove the saved visual configuration.\n"
              "Requires KBDD_ENABLE_LED=1.\n", f);
    } else if (strcmp(subcommand, "key") == 0) {
        fputs("Usage: kbctl led key <k1[,k2,...]> <RRGGBB> [--stride N]\n\n"
              "Set the color of keys by name or group (wasd, numbers, letters, mods, all). Other keys keep\n"
              "their colors. Names are case-insensitive; 'kbctl led keys' lists them. Needs KBDD_ENABLE_LED=1.\n", f);
    } else if (strcmp(subcommand, "sweep") == 0) {
        fputs("Usage: kbctl led sweep [--delay MS] [--color RRGGBB] [--from N]\n\n"
              "Light one position at a time to map matrix positions. Press Ctrl+C to stop.\n", f);
    } else if (strcmp(subcommand, "release") == 0) {
        fputs("Usage: kbctl led release\n\nStop periodic LED frame streaming.\n", f);
    } else if (strcmp(subcommand, "effects") == 0) {
        fputs("Usage: kbctl led effects\n\nList supported built-in effect names and numeric indexes.\n", f);
    } else if (strcmp(subcommand, "effect") == 0) {
        fputs("Usage: kbctl led effect <name|index>\n\nSet the active profile effect. Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "brightness") == 0) {
        fputs("Usage: kbctl led brightness <0-10>\n\nSet profile brightness; 0 is off and 10 is maximum.\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "speed") == 0) {
        fputs("Usage: kbctl led speed <2-102>\n\nSet raw effect speed; lower values are faster.\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else {
        fprintf(f, "kbctl: no LED help available for '%s'\n", subcommand);
    }
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

static int print_reply(const uint8_t *data, uint32_t len);

static int ipc_open_socket(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char path[512];
    struct sockaddr_un addr;
    int fd;

    if (runtime == NULL || runtime[0] == '\0' ||
        snprintf(path, sizeof(path), "%s/kbdd.sock", runtime) >= (int)sizeof(path)) {
        fprintf(stderr, "kbctl: cannot locate kbdd socket\n");
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "kbctl: cannot connect to kbdd: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int ipc_send_request_fd(int fd, uint16_t opcode)
{
    uint8_t header[KBDD_IPC_HEADER_SIZE] = {0};
    uint32_t magic = KBDD_IPC_MAGIC;
    header[0] = (uint8_t)magic; header[1] = (uint8_t)(magic >> 8);
    header[2] = (uint8_t)(magic >> 16); header[3] = (uint8_t)(magic >> 24);
    header[4] = KBDD_IPC_VERSION;
    header[12] = (uint8_t)opcode; header[13] = (uint8_t)(opcode >> 8);
    header[14] = 1;
    return send_all(fd, header, sizeof(header));
}

static int ipc_watch(void)
{
    uint8_t header[KBDD_IPC_HEADER_SIZE];
    uint8_t *payload;
    uint32_t len;
    int fd = ipc_open_socket();

    if (fd < 0 || ipc_send_request_fd(fd, KBDD_IPC_OP_WATCH) != 0) {
        if (fd >= 0) close(fd);
        return 1;
    }
    if (recv_all(fd, header, sizeof(header)) != 0 ||
        rd32(header) != KBDD_IPC_MAGIC) {
        close(fd);
        return 1;
    }
    len = rd32(header + 8);
    if (len > KBDD_IPC_MAX_PAYLOAD) {
        close(fd);
        return 1;
    }
    payload = len > 0 ? malloc(len) : NULL;
    if (len > 0 && (payload == NULL || recv_all(fd, payload, len) != 0)) {
        free(payload);
        close(fd);
        return 1;
    }
    if (print_reply(payload, len) != 0) {
        free(payload);
        close(fd);
        return 1;
    }
    free(payload);
    for (;;) {
        if (recv_all(fd, header, sizeof(header)) != 0) break;
        if (rd32(header) != KBDD_IPC_MAGIC ||
            (uint16_t)(header[12] | (header[13] << 8)) != KBDD_IPC_OP_EVENT) {
            break;
        }
        len = rd32(header + 8);
        if (len == 0 || len > KBDD_IPC_MAX_PAYLOAD) break;
        payload = malloc(len);
        if (payload == NULL || recv_all(fd, payload, len) != 0) {
            free(payload);
            break;
        }
        print_reply(payload, len);
        free(payload);
    }
    close(fd);
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

/* ---- LED / read helpers ------------------------------------------------ */

static int parse_rgb(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned v = 0;
    size_t i, n;
    if (s != NULL && s[0] == '#') s++;
    if (s == NULL) return -1;
    n = strlen(s);
    if (n != 6) return -1;
    for (i = 0; i < n; i++) {
        char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10u;
        else return -1;
        v = (v << 4) | d;
    }
    *r = (uint8_t)(v >> 16);
    *g = (uint8_t)(v >> 8);
    *b = (uint8_t)v;
    return 0;
}

static int parse_uint(const char *s, unsigned long max, unsigned long *out)
{
    char *end = NULL;
    unsigned long v;
    if (s == NULL || s[0] == '\0') return -1;
    v = strtoul(s, &end, 10);
    if (end == NULL || *end != '\0' || v > max) return -1;
    *out = v;
    return 0;
}

static void sleep_ms(unsigned long ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000UL);
    ts.tv_nsec = (long)(ms % 1000UL) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Sends LED_SET. Returns 0 ok, 1 daemon error (message in msg), -1 transport. */
static int led_call(unsigned flags, unsigned stride, unsigned mode, unsigned pos,
                    uint8_t r, uint8_t g, uint8_t b, char *msg, size_t msgsz)
{
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    int is_err;

    payload[0] = (uint8_t)flags;
    payload[1] = (uint8_t)stride;
    payload[2] = (uint8_t)mode;
    payload[3] = (uint8_t)pos;
    payload[4] = r;
    payload[5] = g;
    payload[6] = b;
    if (ipc_request(KBDD_IPC_OP_LED_SET, payload, sizeof(payload), &resp, &rlen) != 0) {
        free(resp);
        return -1;
    }
    msg[0] = '\0';
    if (resp != NULL && rlen > 0) {
        size_t n = rlen < msgsz ? rlen : msgsz - 1;
        memcpy(msg, resp, n);
        msg[n] = '\0';
    }
    is_err = (strncmp(msg, "error:", 6) == 0);
    free(resp);
    return is_err ? 1 : 0;
}

struct led_opts {
    unsigned flags;
    unsigned long stride;
    unsigned long delay;
    unsigned long from;
    unsigned long period;
    uint8_t r, g, b;
    int have_color;
};

/* Parses --once/--stride/--delay/--color/--from from argv[start..]. */
static int led_parse_opts(int argc, char **argv, int start, struct led_opts *o)
{
    int i;
    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            o->flags = 0;
        } else if (strcmp(argv[i], "--stride") == 0 && i + 1 < argc) {
            if (parse_uint(argv[++i], 144, &o->stride) != 0) return -1;
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            if (parse_uint(argv[++i], 60000, &o->delay) != 0) return -1;
        } else if (strcmp(argv[i], "--period") == 0 && i + 1 < argc) {
            if (parse_uint(argv[++i], KBC_ANIM_PERIOD_MAX, &o->period) != 0) return -1;
        } else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            if (parse_uint(argv[++i], 143, &o->from) != 0) return -1;
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (parse_rgb(argv[++i], &o->r, &o->g, &o->b) != 0) return -1;
            o->have_color = 1;
        } else {
            return -1;
        }
    }
    return 0;
}


static int effect_from_arg(const char *a)
{
    unsigned long v;
    int m;
    if (parse_uint(a, 15, &v) == 0) return (int)v;
    for (m = 0; m < KBC_LED_EFFECT_COUNT; m++) {
        if (strcasecmp(a, kbc_led_effect_name((uint8_t)m)) == 0) return m;
    }
    return -1;
}

static int mkdir_p(char *path)
{
    char *p;
    for (p = path + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; (void)mkdir(path, 0700); *p = '/'; }
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST ? 0 : -1;
}

/* Saves the current block of profile `prof` to the state dir. 0 = ok. */
static int backup_block(unsigned prof, char *outpath, size_t outsz)
{
    uint8_t payload[1] = {(uint8_t)prof};
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    const char *base = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    char dir[400];
    FILE *f;

    if (ipc_request(KBDD_IPC_OP_READ_BLOCK, payload, 1, &resp, &rlen) != 0) {
        free(resp);
        return -1;
    }
    if (rlen != 264 || resp[0] != 0x07) { free(resp); return -1; }
    if (base != NULL && base[0] == '/') snprintf(dir, sizeof(dir), "%s/kbctl", base);
    else if (home != NULL) snprintf(dir, sizeof(dir), "%s/.local/state/kbctl", home);
    else { free(resp); return -1; }
    if (mkdir_p(dir) != 0) { free(resp); return -1; }
    snprintf(outpath, outsz, "%s/profile%u-%ld.bin", dir, prof, (long)time(NULL));
    f = fopen(outpath, "wb");
    if (f == NULL || fwrite(resp, 1, rlen, f) != rlen) {
        if (f != NULL) fclose(f);
        free(resp);
        return -1;
    }
    fclose(f);
    free(resp);
    return 0;
}

/* Backup of the active profile block, then one profile write through kbdd. */
static int profile_write(uint8_t op, const uint8_t *payload, size_t plen)
{
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    unsigned prof = 0;
    char path[512];
    int rc;

    if (ipc_request(KBDD_IPC_OP_READ_ACTIVE, NULL, 0, &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    if (resp == NULL || rlen < 17 || sscanf((const char *)resp, "active profile: %u", &prof) != 1) {
        (void)print_reply(resp, rlen);
        free(resp);
        return 1;
    }
    free(resp);
    if (backup_block(prof, path, sizeof(path)) != 0) {
        fprintf(stderr, "kbctl: could not back up the current profile block; aborting\n");
        return 1;
    }
    printf("backup: %s\n", path);
    resp = NULL;
    if (ipc_request(op, payload, plen, &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    rc = print_reply(resp, rlen);
    free(resp);
    return rc;
}

static int cmd_led_effect(int argc, char **argv)
{
    uint8_t mode;
    int m;
    if (argc != 4 || (m = effect_from_arg(argv[3])) < 0) {
        fprintf(stderr, "kbctl: led effect <name|index>  (see: kbctl led effects)\n");
        return 2;
    }
    mode = (uint8_t)m;
    return profile_write(KBDD_IPC_OP_SET_EFFECT, &mode, 1);
}

static int cmd_led_field(int argc, char **argv, uint8_t field, unsigned long lo,
                         unsigned long hi, const char *hint)
{
    unsigned long v;
    uint8_t payload[2];
    if (argc != 4 || parse_uint(argv[3], hi, &v) != 0 || v < lo) {
        fprintf(stderr, "kbctl: led %s <%lu-%lu>  %s\n", argv[2], lo, hi, hint);
        return 2;
    }
    payload[0] = field;
    payload[1] = (uint8_t)v;
    return profile_write(KBDD_IPC_OP_SET_FIELD, payload, 2);
}

static int cmd_led(int argc, char **argv)
{
    struct led_opts o;
    char msg[256];
    const char *sub = (argc >= 3) ? argv[2] : "";
    int rc;

    if (strcmp(sub, "help") == 0 || strcmp(sub, "--help") == 0 ||
        strcmp(sub, "-h") == 0) {
        usage_led(stdout, "led");
        return 0;
    }
    if (argc >= 4 && (strcmp(argv[3], "help") == 0 ||
                      strcmp(argv[3], "--help") == 0 ||
                      strcmp(argv[3], "-h") == 0)) {
        usage_led(stdout, sub);
        return 0;
    }

    memset(&o, 0, sizeof(o));
    o.flags = KBDD_LED_FLAG_HOLD;
    o.delay = 800;

    if (strcmp(sub, "effects") == 0) {
        unsigned m;
        printf("indice  efeito\n");
        for (m = 0; m < KBC_LED_EFFECT_COUNT; m++) {
            printf("%5u   %s%s\n", m, kbc_led_effect_name((uint8_t)m),
                   m == KBC_LED_EFFECT_LIGHTING ? "  (cor fixa)" : "");
        }
        printf("   15   custom\n");
        printf("velocidade %d..%d, brilho %d..%d\n", KBC_LED_SPEED_MIN,
               KBC_LED_SPEED_MAX, KBC_LED_BRIGHTNESS_MIN, KBC_LED_BRIGHTNESS_MAX);
        return 0;
    }
    if (strcmp(sub, "effect") == 0) return cmd_led_effect(argc, argv);
    if (strcmp(sub, "brightness") == 0)
        return cmd_led_field(argc, argv, 2, 0, KBC_BRIGHTNESS_RAW_MAX, "(0 = off, 10 = max)");
    if (strcmp(sub, "speed") == 0)
        return cmd_led_field(argc, argv, 3, KBC_SPEED_RAW_MIN, KBC_SPEED_RAW_MAX,
                             "(raw; lower = faster; one value shared by all effects)");
    if (strcmp(sub, "release") == 0) {
        rc = led_call(0, 0, KBDD_LED_MODE_RELEASE, 0, 0, 0, 0, msg, sizeof(msg));
        if (rc < 0) return 1;
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        return rc;
    }
    if (strcmp(sub, "solid") == 0 || strcmp(sub, "off") == 0) {
        uint8_t r = 0, g = 0, b = 0;
        int next = 3;
        if (strcmp(sub, "solid") == 0) {
            if (argc < 4 || parse_rgb(argv[3], &r, &g, &b) != 0) {
                fprintf(stderr, "kbctl: led solid needs a colour like FF8800\n");
                return 2;
            }
            next = 4;
        }
        if (led_parse_opts(argc, argv, next, &o) != 0) { usage(stderr); return 2; }
        rc = led_call(o.flags, (unsigned)o.stride, KBDD_LED_MODE_SOLID, 0, r, g, b,
                      msg, sizeof(msg));
        if (rc < 0) return 1;
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        return rc;
    }
    if (strcmp(sub, "clear") == 0) {
        if (led_parse_opts(argc, argv, 3, &o) != 0) { usage(stderr); return 2; }
        rc = led_call(o.flags, (unsigned)o.stride, KBDD_LED_MODE_CLEAR, 0,
                      0, 0, 0, msg, sizeof(msg));
        if (rc < 0) return 1;
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        return rc;
    }
    if (strcmp(sub, "pos") == 0) {
        unsigned long pos;
        uint8_t r, g, b;
        if (argc < 5 || parse_uint(argv[3], 143, &pos) != 0 ||
            parse_rgb(argv[4], &r, &g, &b) != 0) {
            fprintf(stderr, "kbctl: led pos <n> <RRGGBB>\n");
            return 2;
        }
        if (led_parse_opts(argc, argv, 5, &o) != 0) { usage(stderr); return 2; }
        rc = led_call(o.flags, (unsigned)o.stride, KBDD_LED_MODE_POS, (unsigned)pos,
                      r, g, b, msg, sizeof(msg));
        if (rc < 0) return 1;
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        return rc;
    }
    if (strcmp(sub, "anim") == 0) {
        static const char *const kinds[] = {"", "breath", "cycle", "rainbow"};
        uint8_t payload[KBDD_ANIM_PAYLOAD_SIZE];
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        int kind = 0, k, perr;
        for (k = 1; k <= 3 && argc >= 4; k++) {
            if (strcasecmp(argv[3], kinds[k]) == 0) kind = k;
        }
        if (kind == 0 || led_parse_opts(argc, argv, 4, &o) != 0) {
            fprintf(stderr, "kbctl: led anim <breath|cycle|rainbow> [--color RRGGBB] [--period MS] [--stride N]\n");
            return 2;
        }
        if (!o.have_color) { o.r = 255; o.g = 255; o.b = 255; }
        payload[0] = (uint8_t)kind;
        payload[1] = (uint8_t)o.stride;
        payload[2] = (uint8_t)(o.period >> 8);
        payload[3] = (uint8_t)o.period;
        payload[4] = o.r; payload[5] = o.g; payload[6] = o.b;
        if (ipc_request(KBDD_IPC_OP_LED_ANIM, payload, sizeof(payload), &resp, &rlen) != 0) {
            free(resp);
            return 1;
        }
        perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }
    if (strcmp(sub, "keys") == 0) {
        size_t i;
        for (i = 0; i < KBC_KEY_POSITIONS; i++) {
            if (kbc_key_name(i) != NULL) printf("%2zu  %s\n", i, kbc_key_name(i));
        }
        return 0;
    }
    if (strcmp(sub, "key") == 0) {
        int pos[KBC_KEY_POSITIONS];
        size_t n = 0, i, j;
        uint8_t r, g, b;
        char spec[256], *tok, *save = NULL;
        if (argc < 5 || parse_rgb(argv[4], &r, &g, &b) != 0 || strlen(argv[3]) >= sizeof(spec)) {
            fprintf(stderr, "kbctl: led key <k1[,k2,...]> <RRGGBB>  (see: kbctl led keys)\n");
            return 2;
        }
        if (led_parse_opts(argc, argv, 5, &o) != 0) { usage(stderr); return 2; }
        snprintf(spec, sizeof(spec), "%s", argv[3]);
        for (tok = strtok_r(spec, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
            int tmp[KBC_KEY_POSITIONS];
            size_t m = kbc_key_group(tok, tmp, KBC_KEY_POSITIONS);
            int p = kbc_key_position(tok);
            if (m == 0 && p >= 0) { tmp[0] = p; m = 1; }
            if (m == 0) {
                fprintf(stderr, "kbctl: unknown key '%s' (see: kbctl led keys)\n", tok);
                return 2;
            }
            for (j = 0; j < m; j++) {
                int dup = 0;
                for (i = 0; i < n; i++) if (pos[i] == tmp[j]) dup = 1;
                if (!dup && n < KBC_KEY_POSITIONS) pos[n++] = tmp[j];
            }
        }
        for (i = 0; i < n; i++) {
            rc = led_call(KBDD_LED_FLAG_HOLD, (unsigned)o.stride, KBDD_LED_MODE_POS,
                          (unsigned)pos[i], r, g, b, msg, sizeof(msg));
            if (rc < 0) return 1;
            if (rc == 1) { fprintf(stderr, "%s\n", msg); return 1; }
        }
        printf("ok: %zu key(s) set\n", n);
        return 0;
    }
    if (strcmp(sub, "sweep") == 0) {
        unsigned long i;
        if (led_parse_opts(argc, argv, 3, &o) != 0) { usage(stderr); return 2; }
        if (!o.have_color) { o.r = 255; o.g = 255; o.b = 255; }
        printf("Anote qual tecla acende em cada posicao. Ctrl+C para parar.\n");
        for (i = o.from; i <= 143; i++) {
            rc = led_call(KBDD_LED_FLAG_HOLD, (unsigned)o.stride, KBDD_LED_MODE_POS,
                          (unsigned)i, o.r, o.g, o.b, msg, sizeof(msg));
            if (rc < 0) return 1;
            if (rc == 1) {
                if (strstr(msg, "out of range") != NULL) break; /* end of matrix */
                fprintf(stderr, "%s\n", msg);
                return 1;
            }
            {
                const char *kn = kbc_key_name(i);
                printf("posicao %lu%s%s\n", i, kn ? "  " : "", kn ? kn : "");
            }
            fflush(stdout);
            sleep_ms(o.delay);
        }
        (void)led_call(0, 0, KBDD_LED_MODE_RELEASE, 0, 0, 0, 0, msg, sizeof(msg));
        printf("fim do sweep\n");
        return 0;
    }
    fprintf(stderr, "kbctl: unknown led subcommand '%s'\n", sub);
    usage(stderr);
    return 2;
}

static void hexdump_block(const uint8_t *d, size_t n)
{
    size_t off, j;
    for (off = 0; off < n; off += 16) {
        printf("%04zx: ", off);
        for (j = 0; j < 16; j++) {
            if (off + j < n) printf("%02x ", d[off + j]);
            else printf("   ");
            if (j == 7) printf(" ");
        }
        printf("\n");
    }
}

static int cmd_dump_block(int argc, char **argv)
{
    unsigned long prof;
    uint8_t payload[1];
    uint8_t *resp = NULL;
    uint32_t rlen = 0;

    if (argc < 3 || argc > 4 || parse_uint(argv[2], 4, &prof) != 0 || prof < 1) {
        fprintf(stderr, "kbctl: dump-block <1-4> [file]\n");
        return 2;
    }
    payload[0] = (uint8_t)prof;
    if (ipc_request(KBDD_IPC_OP_READ_BLOCK, payload, 1, &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    if (rlen != 264 || resp[0] != 0x07) {
        int e = print_reply(resp, rlen);
        free(resp);
        return e ? 1 : 1;
    }
    printf("perfil %lu, 264 bytes (cabecalho 0..7, dados 8..263)\n", prof);
    hexdump_block(resp, rlen);
    {
        const char *na = kbc_led_effect_name(resp[7]);
        const char *nb = kbc_led_effect_name(resp[11]);
        printf("campos (efeito/brilho/velocidade confirmados em hardware; [8] inferido):\n"
               "  A: rate=[4]=%u  efeito=[7]=%u (%s)\n"
               "  B: efeito=[11]=%u (%s)  brilho=[12]=%u  veloc(wave)=[19]=%u  rate?=[8]=%u\n",
               resp[4], resp[7], na ? na : "?", resp[11], nb ? nb : "?",
               resp[12], resp[19], resp[8]);
    }
    if (argc == 4) {
        FILE *f = fopen(argv[3], "wb");
        if (f == NULL || fwrite(resp, 1, rlen, f) != rlen) {
            fprintf(stderr, "kbctl: cannot write %s: %s\n", argv[3], strerror(errno));
            if (f != NULL) fclose(f);
            free(resp);
            return 1;
        }
        fclose(f);
        printf("salvo em %s\n", argv[3]);
    }
    free(resp);
    return 0;
}

static int cmd_diff_block(int argc, char **argv)
{
    uint8_t a[264], b[264];
    FILE *fa, *fb;
    size_t i, diffs = 0;

    if (argc != 4) { fprintf(stderr, "kbctl: diff-block <a.bin> <b.bin>\n"); return 2; }
    fa = fopen(argv[2], "rb");
    fb = fopen(argv[3], "rb");
    if (fa == NULL || fb == NULL || fread(a, 1, 264, fa) != 264 ||
        fread(b, 1, 264, fb) != 264) {
        fprintf(stderr, "kbctl: cannot read two 264-byte dumps\n");
        if (fa != NULL) fclose(fa);
        if (fb != NULL) fclose(fb);
        return 1;
    }
    fclose(fa);
    fclose(fb);
    for (i = 0; i < 264; i++) {
        if (a[i] != b[i]) {
            printf("offset %3zu (0x%02zx): 0x%02x -> 0x%02x\n", i, i, a[i], b[i]);
            diffs++;
        }
    }
    printf("%zu byte(s) diferente(s)\n", diffs);
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
        if (argc == 2) {
            usage(stdout);
        } else if (argc == 3 && strcmp(argv[2], "led") == 0) {
            usage_led(stdout, "led");
        } else if (argc == 4 && strcmp(argv[2], "led") == 0) {
            usage_led(stdout, argv[3]);
        } else if (argc == 3) {
            usage_command(stdout, argv[2]);
        } else {
            usage(stdout);
        }
        return 0;
    }

    if (argc >= 3 && (strcmp(argv[2], "--help") == 0 ||
                      strcmp(argv[2], "-h") == 0)) {
        if (strcmp(cmd, "led") == 0) {
            usage_led(stdout, "led");
        } else {
            usage_command(stdout, cmd);
        }
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

    if (strcmp(cmd, "watch") == 0) {
        if (argc != 2) {
            usage(stderr);
            return 2;
        }
        return ipc_watch();
    }

    if (strcmp(cmd, "profile") == 0) {
        unsigned long p;
        uint8_t payload[1];
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        int perr;
        if (argc != 3 || parse_uint(argv[2], 4, &p) != 0 || p < 1) {
            fprintf(stderr, "kbctl: profile <1-4>\n");
            return 2;
        }
        payload[0] = (uint8_t)p;
        if (ipc_request(KBDD_IPC_OP_SET_PROFILE, payload, 1, &resp, &rlen) != 0) {
            free(resp);
            return 1;
        }
        perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }
    if (strcmp(cmd, "active-profile") == 0) {
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        if (argc != 2) { usage(stderr); return 2; }
        if (ipc_request(KBDD_IPC_OP_READ_ACTIVE, NULL, 0, &resp, &rlen) != 0) {
            free(resp);
            return 1;
        }
        int perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }
    if (strcmp(cmd, "dump-block") == 0) return cmd_dump_block(argc, argv);
    if (strcmp(cmd, "diff-block") == 0) return cmd_diff_block(argc, argv);
    if (strcmp(cmd, "led") == 0) return cmd_led(argc, argv);

    fprintf(stderr, "kbctl: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}