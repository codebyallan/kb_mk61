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
#include "kbctl_parse.h"

#define KBCTL_VERSION KBCORE_VERSION_STRING
#define KBCTL_MAX_FILE (4 * 1024 * 1024)

static void usage(FILE *f)
{
    fputs("Usage: kbctl <command> [arguments]\n\n", f);
    fputs("Commands:\n", f);
    fputs("  list                          List devices known to kbdd\n", f);
    fputs("  status                        Show daemon status\n", f);
    fputs("  read-profile [profile]        Read a profile (profile 1-4)\n", f);
    fputs("  restore-profile <profile> <file> Restore a 264-byte profile backup (profile 1-4)\n", f);
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
    fputs("  led key <keys> <RRGGBB>       Color keys by name: w,a,s,d | wasd | esc,numbers (see: keys)\n", f);
    fputs("  remap [--layer 0|1|2] <key>=<code>... | reset   Remap keys (normal/Fn/custom layer) of the active profile\n", f);
    fputs("  macro [--layer 0|1] [--repeat N] <key> <step>...   Bind a macro (0xB2 + 0x05) to a key\n", f);
    fputs("  macro list [--profile 1-4]          List saved macros (offline)\n", f);
    fputs("  keys                          List key names and matrix positions (offline; 'led keys' also works)\n", f);
    fputs("  led custom [--base RRGGBB] <keys>=<RRGGBB>...  Store per-key colors in the keyboard (flash)\n", f);
    fputs("  led custom clear              Leave custom mode and commit to flash (effect 15 -> wave)\n", f);
    fputs("  led release                   Stop streaming frames\n", f);
    fputs("  led effects                   List the keyboard built-in effects (offline)\n", f);
    fputs("  led color <effect> <RRGGBB>.. Set the colors of a built-in effect in the active profile\n", f);
    fputs("  led save                      Commit the active profile (effect/brightness/speed) to flash\n", f);
    fputs("    --save on 'led effect|brightness|speed' saves right after the change\n", f);
    fputs("  led effect <name|index>       Set the active profile effect; 15 = stored custom layout\n", f);
    fputs("  led brightness <0-10>         Set brightness (also scales solid colors; 0 = off)\n", f);
    fputs("  led speed <2-102>             Set the effect speed (raw, lower = faster; shared by all effects)\n", f);
    fputs("  led direction <0-3>           Set animation direction\n", f);
    fputs("  led report-rate <0-254>       Set report-rate index (stored as index + 1 in byte 8)\n", f);
    fputs("    opts: --once (send one frame, no streaming)  --stride N (frame layout, default = matrix size)\n", f);
    fputs("          sweep: --delay MS  --color RRGGBB  --from N\n", f);
    fputs("  opts <dir 0|1> <winlock 0|1>  Set 0x0B options (transposed arrows, Win lock)\n", f);
    fputs("  caps [led|write|all] [on|off]  Show or change runtime capabilities (LED, profile write)\n", f);
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
        fputs("Usage: kbctl read-profile [profile]\n\nRead a hardware profile (1-4). With no profile, read profile 1.\n", f);
    } else if (strcmp(command, "restore-profile") == 0) {
        fputs("Usage: kbctl restore-profile <profile> <file>\n\nRestore a 264-byte profile backup to hardware profile 1-4. The block is verified by read-back and rolled back on failure. Requires\n"
              "KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
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
    } else if (strcmp(command, "macro") == 0) {
        fputs("Usage: kbctl macro [--layer 0|1|normal|fn] [--repeat N] <key> <step> [...]\n"
              "       kbctl macro [--layer L] <key> clear\n"
              "       kbctl macro list [--profile 1-4]\n\n"
              "Bind a macro to <key> of the active profile: the key matrix gets the 0xB2 marker and the\n"
              "macro data is written separately (0x05). <step> is +code[:ms] (key down), -code[:ms] (key up)\n"
              "or code[:ms] (down + up); code is a HID name or 0xNN, ms 20..255 (default 20), up to 80 steps,\n"
              "N 0..1000 repetitions (default 1). Example: kbctl macro f1 lctrl+ ... : kbctl macro f1 +lctrl c -lctrl\n"
              "'clear' restores the factory code of that key. Needs KBDD_ENABLE_PROFILE_WRITE=1.\n"
              "'list' shows the macros saved locally for each profile (offline; no daemon needed).\n"
              "A profile can hold up to 255 macros, one per slot index.\\n", f);
    } else if (strcmp(command, "remap") == 0) {
        fputs("Usage: kbctl remap [--layer 0|1|2|normal|fn|custom] <key>=<code> [...]\n"
              "       kbctl remap reset\n\n"
              "Write the 3 x 70 key matrix (0x03) of the active profile. <key> is a name from\n"
              "'kbctl keys'; <code> is a HID name (a, f5, lctrl, play, mouse1...) or 0xNN.\n"
              "Keys not listed keep their previous remap (last matrix written by kbctl for the\n"
              "profile, kept in $XDG_STATE_HOME/kbctl); 'reset' restores the factory map.\n"
              "Needs KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(command, "led") == 0) {
        fputs("Usage: kbctl led <subcommand> [arguments]\n\nUse 'kbctl help led <subcommand>' for a specific LED command.\n", f);
    } else if (strcmp(command, "version") == 0) {
        fputs("Usage: kbctl version\n\nPrint the kbctl and libkbcore versions.\n", f);
    } else if (strcmp(command, "caps") == 0) {
        fputs("Usage: kbctl caps\n"
              "       kbctl caps <led|write|all> <on|off>\n\n"
              "With no arguments, prints the current runtime capability state:\n"
              "  LED control:    enabled/disabled  (KBDD_ENABLE_LED)\n"
              "  Profile write:  enabled/disabled  (KBDD_ENABLE_PROFILE_WRITE)\n\n"
              "With a target and action, changes a capability at runtime:\n"
              "  kbctl caps led on     Enable LED frame control (requires interactive confirmation)\n"
              "  kbctl caps led off    Disable LED frame control\n"
              "  kbctl caps write on   Enable profile/flash writes (requires interactive confirmation)\n"
              "  kbctl caps write off  Disable profile/flash writes\n"
              "  kbctl caps all on     Enable both capabilities (each is confirmed separately)\n"
              "  kbctl caps all off    Disable both capabilities\n\n"
              "Enabling a capability that is already enabled is a no-op. Enabling a disabled\n"
              "capability prompts 'Are you sure ...? (y/n): ' and only proceeds on 'y'.\n"
              "Changes apply to the running kbdd only; they are not persisted across daemon\n"
              "restarts unless the matching environment variable is also set.\n", f);
    } else {
        fprintf(f, "kbctl: no help available for '%s'\n", command);
    }
}

static void usage_led_color_slots(FILE *f)
{
    unsigned m;

    fputs("Color slots (accepted colors: 1..N; fewer colors repeat):\n", f);
    for (m = 0; m < KBC_LED_EFFECT_COUNT; m++) {
        size_t count;
        if (kbc_effect_color_slots((uint8_t)m, NULL, &count)) {
            fprintf(f, "  %-10s 1..%zu\n", kbc_led_effect_name((uint8_t)m), count);
        } else {
            fprintf(f, "  %-10s none\n", kbc_led_effect_name((uint8_t)m));
        }
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
              "  anim <breath|cycle|rainbow> [opts]  Stream a host animation\n"
              "  key <keys> <RRGGBB>         Color keys by name\n"
              "  release                     Stop periodic frame streaming\n"
              "  effects                     List built-in keyboard effects\n"
              "  color <effect> <RRGGBB>..  Set built-in effect colors\n"
              "  save                        Commit the active profile to flash\n"
              "  effect <name|index> [--save] Set the active profile effect (15 = custom)\n"
              "  brightness <0-10> [--save] Set brightness\n"
              "  speed <2-102> [--save]     Set effect speed\n\n"
              "  direction <0-3> [--save]  Set animation direction\n"
              "  report-rate <0-254> [--save] Set report-rate index\n\n"
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
              "their colors. Names are case-insensitive; 'kbctl keys' lists them. Needs KBDD_ENABLE_LED=1.\n", f);
    } else if (strcmp(subcommand, "color") == 0) {
        fputs("Usage: kbctl led color <effect> <RRGGBB> [RRGGBB ...] [--save]\n\n"
              "Set the colors stored in the keyboard for a built-in effect (breath, starlight, encounter,\n"
              "rain, pulse, cross, trigger, ripple, blossom, lighting, wave). With fewer colors than the\n"
              "effect has slots, they repeat. It does not switch effects: use 'led effect'. Cycle and\n"
              "game mode have no colors. The nine per-effect rainbow flags are cleared, like the official\n"
              "driver does. RAM only unless --save (or a later 'led save'). Needs KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
        usage_led_color_slots(f);
    } else if (strcmp(subcommand, "sweep") == 0) {
        fputs("Usage: kbctl led sweep [--delay MS] [--color RRGGBB] [--from N]\n\n"
              "Light one position at a time to map matrix positions. Press Ctrl+C to stop.\n", f);
    } else if (strcmp(subcommand, "release") == 0) {
        fputs("Usage: kbctl led release\n\nStop periodic LED frame streaming.\n", f);
    } else if (strcmp(subcommand, "effects") == 0) {
        fputs("Usage: kbctl led effects\n\nList supported built-in effect names, numeric indexes, and color slot counts.\n", f);
        usage_led_color_slots(f);
    } else if (strcmp(subcommand, "effect") == 0) {
        fputs("Usage: kbctl led effect <name|index> [--save]\n\n"
              "Set the active profile effect. Index 15 ('custom') re-selects the per-key layout\n"
              "already stored by 'led custom' without writing flash; --save is then a no-op.\n"
              "Append --save for built-in effects to commit them to flash.\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "brightness") == 0) {
        fputs("Usage: kbctl led brightness <0-10> [--save]\n\n"
              "Set profile brightness; 0 is off and 10 is maximum. Append --save to commit it to flash.\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "speed") == 0) {
        fputs("Usage: kbctl led speed <2-102> [--save]\n\n"
              "Set raw effect speed; lower values are faster. Append --save to commit it to flash.\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "direction") == 0) {
        fputs("Usage: kbctl led direction <0-3> [--save]\n\n"
              "Set animation direction in profile byte 5 (report byte 13).\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "report-rate") == 0) {
        fputs("Usage: kbctl led report-rate <0-254> [--save]\n\n"
              "Set the report-rate index; the stored value is index + 1 in profile byte 0 (report byte 8).\n"
              "Requires KBDD_ENABLE_PROFILE_WRITE=1.\n", f);
    } else if (strcmp(subcommand, "custom") == 0) {
        fputs("Usage: kbctl led custom [--base RRGGBB] <keys>=<RRGGBB> [<keys>=<RRGGBB> ...]\n"
              "       kbctl led custom clear\n\n"
              "Store per-key colors in the keyboard (custom slot 0, effect 15) and commit them to flash,\n"
              "so they survive replug with no software running. Keys not listed get --base (default 000000).\n"
              "<keys> uses the same names/groups as 'led key' (e.g. wasd=ff0000 esc=00ff00).\n"
              "Backs up the profile block first; slot data is write-only and is not backed up. Needs\n"
              "KBDD_ENABLE_PROFILE_WRITE=1. 'clear' leaves custom mode (effect 15 -> wave, other\n"
              "effects preserved) and commits it. 'led effect 15' re-selects a stored layout\n"
              "without re-uploading or writing flash. 'led save' is refused while custom data exists.\n", f);
    } else if (strcmp(subcommand, "save") == 0) {
        fputs("Usage: kbctl led save\n\n"
              "Commit the active profile's effect, brightness, and speed to flash.\n"
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

/* Sets SO_RCVTIMEO/SO_SNDTIMEO on fd. ms == 0 means "no timeout" (blocking). */
static void set_socket_timeouts(int fd, unsigned long ms)
{
    struct timeval tv;
    if (ms == 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    } else {
        tv.tv_sec = (time_t)(ms / 1000UL);
        tv.tv_usec = (suseconds_t)((ms % 1000UL) * 1000UL);
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static unsigned long kbctl_recv_timeout_ms(void)
{
    const char *env = getenv("KBCTL_RECV_TIMEOUT_MS");
    unsigned long wait_ms = 30000UL; /* 30 s default */
    if (env != NULL && env[0] != '\0') {
        char *end = NULL;
        unsigned long v = strtoul(env, &end, 10);
        if (end != NULL && *end == '\0' && v <= 300000UL) {
            wait_ms = v;
        }
    }
    return wait_ms;
}
/* Connects to the kbdd socket, retrying while the daemon is still starting
 * (socket missing or refusing connections). KBCTL_CONNECT_TIMEOUT_MS: total wait,
 * default 3000, 0 = single attempt. Returns an fd, or -1 with errno set. */
static int connect_daemon(const char *path)
{
    struct sockaddr_un addr;
    struct timespec t0, now, ts = {0, 100000000L};
    unsigned long wait_ms = 3000;
    const char *env = getenv("KBCTL_CONNECT_TIMEOUT_MS");

    if (env != NULL && env[0] != '\0') {
        char *end = NULL;
        unsigned long v = strtoul(env, &end, 10);
        if (end != NULL && *end == '\0' && v <= 60000UL) wait_ms = v;
    }
    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int err;
        long elapsed;
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
        err = errno;
        close(fd);
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (long)((now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L);
        if ((err != ENOENT && err != ECONNREFUSED) || elapsed >= (long)wait_ms) {
            errno = err;
            return -1;
        }
        nanosleep(&ts, NULL);
    }
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

    int fd = connect_daemon(path);
    if (fd < 0) {
        fprintf(stderr, "kbctl: cannot connect to kbdd at %s: %s (is kbdd running? try: systemctl --user status kbdd)\n",
                path, strerror(errno));
        return -1;
    }
    set_socket_timeouts(fd, kbctl_recv_timeout_ms());

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
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "kbctl: timed out waiting for daemon response (KBCTL_RECV_TIMEOUT_MS=%lu)\n",
                    kbctl_recv_timeout_ms());
        } else {
            fprintf(stderr, "kbctl: recv: %s\n", strerror(errno));
        }
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "kbctl: timed out waiting for daemon response (KBCTL_RECV_TIMEOUT_MS=%lu)\n",
                        kbctl_recv_timeout_ms());
            } else {
                fprintf(stderr, "kbctl: recv payload: %s\n", strerror(errno));
            }
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
    int fd;

    if (runtime == NULL || runtime[0] == '\0' ||
        snprintf(path, sizeof(path), "%s/kbdd.sock", runtime) >= (int)sizeof(path)) {
        fprintf(stderr, "kbctl: cannot locate kbdd socket\n");
        return -1;
    }
    fd = connect_daemon(path);
    if (fd < 0) {
        fprintf(stderr, "kbctl: cannot connect to kbdd: %s (is kbdd running?)\n", strerror(errno));
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
            if (kbctl_parse_uint(argv[++i], 144, &o->stride) != 0) return -1;
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            if (kbctl_parse_uint(argv[++i], 60000, &o->delay) != 0) return -1;
        } else if (strcmp(argv[i], "--period") == 0 && i + 1 < argc) {
            if (kbctl_parse_uint(argv[++i], KBC_ANIM_PERIOD_MAX, &o->period) != 0) return -1;
        } else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            if (kbctl_parse_uint(argv[++i], 143, &o->from) != 0) return -1;
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (kbctl_parse_rgb(argv[++i], &o->r, &o->g, &o->b) != 0) return -1;
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
    if (kbctl_parse_uint(a, 15, &v) == 0) return (int)v;
    for (m = 0; m < KBC_LED_EFFECT_COUNT; m++) {
        if (strcasecmp(a, kbc_led_effect_name((uint8_t)m)) == 0) return m;
    }
    if (strcasecmp(a, "custom") == 0) return KBC_LED_EFFECT_CUSTOM;
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

/* "--save" as the last argument: commit to flash after the change. */
static int wants_save(int *argc, char **argv)
{
    if (*argc >= 5 && strcmp(argv[*argc - 1], "--save") == 0) {
        (*argc)--;
        return 1;
    }
    return 0;
}

static int save_to_flash(void)
{
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    int rc;
    if (ipc_request(KBDD_IPC_OP_SAVE_BLOCK, NULL, 0, &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    rc = print_reply(resp, rlen);
    free(resp);
    return rc;
}

/* led custom [--base RRGGBB] <keys>=<RRGGBB> ... : per-key colours stored in flash. */
static int cmd_led_custom(int argc, char **argv)
{
    uint8_t payload[3 * KBC_KEY_POSITIONS];
    uint8_t br = 0, bg = 0, bb = 0;
    int i, nspec = 0, have_base = 0;
    size_t k;

    if (argc == 4 && strcmp(argv[3], "clear") == 0) {
        return profile_write(KBDD_IPC_OP_CLEAR_CUSTOM, NULL, 0);
    }

    for (i = 3; i < argc; i++) {
        char spec[256], *eq, *tok, *save = NULL;
        uint8_t r, g, b;
        int keys = 0;
        if (strcmp(argv[i], "--base") == 0) {
            if (i + 1 >= argc || nspec != 0 || have_base ||
                kbctl_parse_rgb(argv[i + 1], &br, &bg, &bb) != 0) goto usage;
            have_base = 1;
            i++;
            continue;
        }
        if (nspec == 0) {                      /* defaults apply to every key before overrides */
            for (k = 0; k < KBC_KEY_POSITIONS; k++) { payload[3*k] = br; payload[3*k+1] = bg; payload[3*k+2] = bb; }
        }
        if (strlen(argv[i]) >= sizeof(spec)) goto usage;
        snprintf(spec, sizeof(spec), "%s", argv[i]);
        eq = strrchr(spec, '=');
        if (eq == NULL || eq == spec || kbctl_parse_rgb(eq + 1, &r, &g, &b) != 0) goto usage;
        *eq = '\0';
        for (tok = strtok_r(spec, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
            int tmp[KBC_KEY_POSITIONS];
            size_t m = kbc_key_group(tok, tmp, KBC_KEY_POSITIONS), j;
            int p = kbc_key_position(tok);
            if (m == 0 && p >= 0) { tmp[0] = p; m = 1; }
            if (m == 0) {
                fprintf(stderr, "kbctl: unknown key '%s' (see: kbctl keys)\n", tok);
                return 2;
            }
            for (j = 0; j < m; j++) {
                payload[3 * (size_t)tmp[j]] = r; payload[3 * (size_t)tmp[j] + 1] = g; payload[3 * (size_t)tmp[j] + 2] = b;
            }
            keys++;
        }
        if (keys == 0) goto usage;             /* e.g. ",=ff0000": no key names */
        nspec++;
    }
    if (nspec == 0) goto usage;
    return profile_write(KBDD_IPC_OP_SAVE_CUSTOM, payload, sizeof(payload));
usage:
    fprintf(stderr, "kbctl: led custom [--base RRGGBB] <keys>=<RRGGBB> [...]  (--base must come first)\n");
    return 2;
}

/* Resolve the directory that holds the persisted state:
 * $XDG_CONFIG_HOME/kbdd or ~/.config/kbdd. Pure path computation: callers
 * that write must create the directory themselves. */
static int state_dir(char *out, size_t outsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;

    if (xdg != NULL && xdg[0] != '\0') {
        n = snprintf(out, outsz, "%s/kbdd", xdg);
    } else if (home != NULL && home[0] != '\0') {
        n = snprintf(out, outsz, "%s/.config/kbdd", home);
    } else {
        errno = ENOENT;
        return -1;
    }
    if (n < 0 || (size_t)n >= outsz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/* Path of the persisted macro for a profile, beside the keymap file:
 * $XDG_CONFIG_HOME/kbdd/macro-profile<N>.bin  (default ~/.config/kbdd/).
 * Format: 16-byte header (magic, version, key_index, data_len) followed by
 * KBC_MACRO_SIZE (256) bytes of macro data. Matches the daemon's on-disk
 * layout so kbdd can read it back for restore. Read-only: no mkdir here, so
 * `kbctl macro list` never mutates the filesystem. */
static int macro_state_path(unsigned prof, char *out, size_t outsz)
{
    char dir[512];
    int n;

    if (state_dir(dir, sizeof(dir)) != 0) return -1;
    n = snprintf(out, outsz, "%s/macro-profile%u.bin", dir, prof);
    if (n < 0 || (size_t)n >= outsz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/* Path of the persisted macro for a profile with a specific index.
 * idx: 1-255, where 1 is the first macro, 2 the second, etc.
 * Format: $XDG_CONFIG_HOME/kbdd/macro-profile<N>-MMM.bin */
static int macro_state_path_idx(unsigned prof, int idx, char *out, size_t outsz)
{
    char dir[512];
    int n;

    if (state_dir(dir, sizeof(dir)) != 0) return -1;
    n = snprintf(out, outsz, "%s/macro-profile%u-%03u.bin", dir, prof, idx);
    if (n < 0 || (size_t)n >= outsz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/* Entry for one macro loaded from a profile's files. */
typedef struct macro_entry {
    int idx;          /* macro index (1-based) */
    uint8_t key_index; /* key position from header */
    uint8_t data[KBC_MACRO_SIZE]; /* macro data from file */
} macro_entry_t;

/* Path of the persisted keymap for a profile (shared by remap and macro):
 * $XDG_CONFIG_HOME/kbdd/keymap-profile<N>.bin  (default ~/.config/kbdd/).
 * Format: 16-byte header (magic, version, layer_count, positions_per_layer)
 * followed by 3 x KBC_KEY_POSITIONS (210) bytes of keymap. */
static int keymap_state_path(unsigned prof, char *out, size_t outsz)
{
    char dir[512];
    int n;

    if (state_dir(dir, sizeof(dir)) != 0) return -1;
    if (mkdir_p(dir) != 0) return -1;
    n = snprintf(out, outsz, "%s/keymap-profile%u.bin", dir, prof);
    if (n < 0 || (size_t)n >= outsz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

#define KBDD_MACRO_MAGIC_CLI 0x4D435244u   /* "MACD" */
#define KBDD_MACRO_VERSION_CLI 1u

/* Load persisted macro data (key_index + 256 bytes) for `prof`.
 * Returns:
 *    0  success (key_index/data filled)
 *    1  no file exists yet (ENOENT)
 *   -1  I/O error (path, open, read)
 *   -2  the file exists but is not a valid regular macro state
 *
 * O_NOFOLLOW rejects a symlink and O_NONBLOCK keeps a FIFO from blocking the
 * open; fstat() then rejects anything that is not a regular file. Opening
 * once also avoids the access()-then-open() TOCTOU window. */
/* Read one persisted macro state from an explicit path. Returns:
 *    0  success (key_index/data filled)
 *    1  no file exists yet (ENOENT)
 *   -1  the file exists but is not a valid regular macro state
 *   -2  I/O error (open refused, fstat failed, read failed)
 *
 * O_NOFOLLOW rejects a symlink and O_NONBLOCK keeps a FIFO from blocking the
 * open; fstat() then rejects anything that is not a regular file. Opening
 * once also avoids an access()/open() TOCTOU window. */
static int macro_state_read_file(const char *path, uint8_t *key_index,
                                 uint8_t data[KBC_MACRO_SIZE])
{
    FILE *f;
    struct stat st;
    uint32_t hdr[4];
    size_t n;
    int fd;
    int err;

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return (errno == ENOENT) ? 1 : -2;
    }
    if (fstat(fd, &st) != 0) {
        err = errno;
        close(fd);
        errno = err;
        return -2;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    f = fdopen(fd, "rb");
    if (f == NULL) {
        err = errno;
        close(fd);
        errno = err;
        return -2;
    }
    n = fread(hdr, 1, sizeof(hdr), f);
    if (n != sizeof(hdr)) {
        if (ferror(f)) {
            err = errno != 0 ? errno : EIO;
            fclose(f);
            errno = err;
            return -2;
        }
        fclose(f);
        return -1;
    }
    if (hdr[0] != KBDD_MACRO_MAGIC_CLI ||
        hdr[1] != KBDD_MACRO_VERSION_CLI ||
        hdr[2] >= 2u * KBC_KEY_POSITIONS ||
        hdr[3] != KBC_MACRO_SIZE) {
        fclose(f);
        return -1;
    }
    *key_index = (uint8_t)hdr[2];
    n = fread(data, 1, KBC_MACRO_SIZE, f);
    if (ferror(f)) {
        err = errno != 0 ? errno : EIO;
        fclose(f);
        errno = err;
        return -2;
    }
    fclose(f);
    if (n != KBC_MACRO_SIZE) return -1;
    return 0;
}


/* Returns 1 when a state file already exists at `path`, 0 when it does not. */
static int macro_state_exists(const char *path)
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0) return 0;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}

/* Returns 1 when macro slot `s` (>= 1) of `prof` is already occupied:
 * slot 1 lives in the legacy macro-profile<N>.bin file, slots 2..255 in the
 * indexed macro-profile<N>-MMM.bin files. */
static int macro_state_slot_taken(unsigned prof, int s)
{
    char p[512];

    if (s < 1 || s > 255) return 1;
    if (s == 1) {
        if (macro_state_path(prof, p, sizeof(p)) != 0) return 0;
        return macro_state_exists(p);
    }
    if (macro_state_path_idx(prof, s, p, sizeof(p)) != 0) return 0;
    return macro_state_exists(p);
}

/* Persist macro data (key_index + 256 bytes) for `prof` atomically: a unique
 * mkstemp() file (created 0600, never following a planted symlink) is written
 * and renamed over the target. Uses the next free macro slot (auto-increment):
 * slot 1 is the legacy macro-profile<N>.bin, slots 2..255 the indexed
 * macro-profile<N>-MMM.bin files (a profile holds up to 255 macros). */
static int macro_state_load_all(unsigned prof, macro_entry_t **out_entries,
                                int *out_count, int *out_corrupt)
{
    char dir[512];
    macro_entry_t *entries = NULL;
    int count = 0;
    int corrupt = 0;
    int idx;

    *out_entries = NULL;
    *out_count = 0;
    *out_corrupt = 0;

    if (state_dir(dir, sizeof(dir)) != 0) return -1;

    for (idx = 1; idx <= 255; idx++) {
        char path[512];
        uint8_t key_index = 0;
        uint8_t data[KBC_MACRO_SIZE];
        int rc;

        if (idx == 1) {
            if (macro_state_path(prof, path, sizeof(path)) != 0) {
                free(entries);
                return -1;
            }
        } else {
            if (macro_state_path_idx(prof, idx, path, sizeof(path)) != 0) {
                free(entries);
                return -1;
            }
        }
        rc = macro_state_read_file(path, &key_index, data);
        if (rc == -2) {
            /* Real error: open/read refused (e.g. a planted symlink). */
            free(entries);
            return -1;
        }
        if (rc == -1) {
            /* Present but invalid: corrupt. */
            corrupt = 1;
            break;
        }
        if (rc == 1) {
            continue; /* free slot: skip, keep scanning for gaps */
        }
        entries = (macro_entry_t *)realloc(entries, (count + 1) * sizeof(macro_entry_t));
        if (entries == NULL) {
            free(entries);
            return -1;
        }
        entries[count].idx = idx;
        entries[count].key_index = key_index;
        memcpy(entries[count].data, data, KBC_MACRO_SIZE);
        count++;
    }

    /* Mantenha os macros válidos já carregados mesmo quando um arquivo
     * corrompido é encontrado: eles ainda podem ser listados e restaurados.
     * Apenas sinalize que houve corrupção para o caller decidir o que mostrar. */
    *out_entries = entries;
    *out_count = count;
    if (out_corrupt != NULL) *out_corrupt = corrupt;
    return 0;
}


/* Find which on-disk slot (1..255) already holds a macro for `key_index`
 * of `prof`. Returns 0 and stores the slot in *out_slot, or -1 when not
 * found. Used by macro_state_save() to overwrite an existing macro instead
 * of creating a duplicate slot for the same key. */
static int macro_state_slot_for_key(unsigned prof, uint8_t key_index,
                                    int *out_slot)
{
    macro_entry_t *entries = NULL;
    int count = 0;
    int corrupt = 0;
    int s;

    if (out_slot == NULL) return -1;

    if (macro_state_load_all(prof, &entries, &count, &corrupt) != 0) {
        return -1;
    }
    for (s = 0; s < count; s++) {
        if ((uint8_t)entries[s].key_index == key_index) {
            *out_slot = entries[s].idx;
            free(entries);
            return 0;
        }
    }
    free(entries);
    return -1;
}

static int macro_state_save(unsigned prof, uint8_t key_index,
                            const uint8_t data[KBC_MACRO_SIZE])
{
    char spath[512], tmp[560];
    char dir[512];
    FILE *f;
    uint32_t hdr[4];
    int fd;
    int s;

    if (state_dir(dir, sizeof(dir)) != 0) return -1;
    if (mkdir_p(dir) != 0) return -1;

    int existing_slot = -1;

    /* Check if a macro for this key_index already exists. */
    if (macro_state_slot_for_key(prof, key_index, &existing_slot) == 0) {
        s = existing_slot;
    } else {
        for (s = 1; ; s++) {
            if (s > 255) {
                errno = E2BIG;
                return -1;
            }
            if (!macro_state_slot_taken(prof, s)) break;
        }
    }

    if (s == 1) {
        if (macro_state_path(prof, spath, sizeof(spath)) != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else {
        if (macro_state_path_idx(prof, s, spath, sizeof(spath)) != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", spath) >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    hdr[0] = KBDD_MACRO_MAGIC_CLI;
    hdr[1] = KBDD_MACRO_VERSION_CLI;
    hdr[2] = key_index;
    hdr[3] = KBC_MACRO_SIZE;
    fd = mkstemp(tmp);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int err = errno;
        close(fd);
        unlink(tmp);
        errno = err;
        return -1;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        int err = errno;
        close(fd);
        unlink(tmp);
        errno = err;
        return -1;
    }
    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        fwrite(data, 1, KBC_MACRO_SIZE, f) != KBC_MACRO_SIZE) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    if (fclose(f) != 0 || rename(tmp, spath) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* Load all macros saved for `prof` into a malloc'd array (caller frees).
 *
 * A profile holds up to 255 macros. Slot 1 lives in the legacy
 * macro-profile<N>.bin file; slots 2..255 live in the indexed
 * macro-profile<N>-MMM.bin files. The scan walks slots upward and stops at the
 * first free one.
 *
 * Returns 0 on success, -1 on a real error (path resolution, read, alloc).
 * On success *out_entries holds *out_count loaded macros (NULL when none were
 * found) and *out_corrupt is set when a present-but-invalid state file was hit. */


static int active_profile(unsigned *prof)
{
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    int ok;

    if (ipc_request(KBDD_IPC_OP_READ_ACTIVE, NULL, 0, &resp, &rlen) != 0) {
        free(resp);
        return -1;
    }
    ok = resp != NULL && rlen >= 17 && sscanf((const char *)resp, "active profile: %u", prof) == 1;
    if (!ok) (void)print_reply(resp, rlen);
    free(resp);
    return ok ? 0 : -1;
}

/* A saved 0xB2 (macro) cannot be rewritten by remap: fall back to the factory code. */
static void macro_markers_to_default(uint8_t *payload)
{
    uint8_t def[KBC_KEY_POSITIONS];
    size_t l, j;

    for (l = 0; l < 3; l++) {
        (void)kbc_keymap_default(l, def);
        for (j = 0; j < KBC_KEY_POSITIONS; j++) {
            if (payload[l * KBC_KEY_POSITIONS + j] == 0xB2) payload[l * KBC_KEY_POSITIONS + j] = def[j];
        }
    }
}

/* ---- Macro listing ------------------------------------------------------- */

/* Format one macro step into `out`. Returns the number of bytes written
 * (excluding NUL) or -1 if `out` is too small. */
static int macro_step_format(int key_down, uint8_t keycode, uint8_t delay,
                             char *out, size_t outsz)
{
    const char *dir = key_down ? "+" : "-";
    int n;

    if (outsz == 0) return -1;
    n = snprintf(out, outsz, "%s0x%02x %ums", dir, keycode, (unsigned)delay);
    if (n < 0 || (size_t)n >= outsz) return -1;
    return n;
}

/* Worst case: 80 steps * "+0xFF 255ms" (11 bytes) + 79 separators + NUL
 * = 960 bytes, so 2048 leaves a wide margin. */
#define MACRO_RENDER_SIZE 2048

/* Render a decoded macro into a human-readable, single-line summary.
 * Returns 0 on success, -1 on truncation. `out` must have at least 1 byte. */
static int macro_render(const kbc_macro *m, char *out, size_t outsz)
{
    size_t i, nsteps;
    size_t used = 0;
    int r;

    if (m == NULL || out == NULL || outsz == 0) return -1;
    nsteps = kbc_macro_step_count(m);
    if (nsteps == 0) {
        r = snprintf(out, outsz, "<empty>");
        if (r < 0 || (size_t)r >= outsz) return -1;
        return 0;
    }
    used = 0;
    for (i = 0; i < nsteps; i++) {
        uint8_t delay, keycode;
        int key_down;
        char step[32];
        size_t sep = (i > 0) ? 1u : 0u;

        if (kbc_macro_get_step(m, i, &delay, &key_down, &keycode) != KBC_OK) {
            return -1;
        }
        r = macro_step_format(key_down, keycode, delay, step, sizeof(step));
        if (r < 0) return -1;
        if (used + sep + (size_t)r + 1 > outsz) return -1;
        if (sep) out[used++] = ' ';
        memcpy(out + used, step, (size_t)r);
        used += (size_t)r;
    }
    out[used] = '\0';
    return 0;
}

/* Format a key index (0..139) as "name" or "fn+name". */
static int macro_key_name(uint8_t key_index, char *out, size_t outsz)
{
    /* Positions 0..69 = normal layer; 70..139 = Fn layer. */
    if (key_index < KBC_KEY_POSITIONS) {
        const char *name = kbc_key_name((size_t)key_index);
        if (name != NULL) {
            int n = snprintf(out, outsz, "%s", name);
            if (n < 0 || (size_t)n >= outsz) return -1;
            return 0;
        }
        int n = snprintf(out, outsz, "?");
        if (n < 0 || (size_t)n >= outsz) return -1;
        return 0;
    }
    /* Fn layer: prefix "fn+" and resolve the relative position (0..69). */
    size_t base = (size_t)(key_index - KBC_KEY_POSITIONS);
    const char *name = kbc_key_name(base);
    if (name != NULL) {
        int n = snprintf(out, outsz, "fn+%s", name);
        if (n < 0 || (size_t)n >= outsz) return -1;
        return 0;
    }
    int n = snprintf(out, outsz, "fn+%zu", base);
    if (n < 0 || (size_t)n >= outsz) return -1;
    return 0;
}

/* Print one loaded macro entry: decode, render, resolve key name. */
static void macro_print_entry(unsigned prof, const macro_entry_t *e)
{
    kbc_macro *m = kbc_macro_create();
    char rendered[MACRO_RENDER_SIZE];

    if (m == NULL) {
        fprintf(stderr, "kbctl: out of memory\n");
        return;
    }
    if (kbc_macro_decode(m, e->data, sizeof(e->data)) != KBC_OK) {
        /* Header is valid but the payload is not a decodable macro. */
        printf("profile %u: macro: <corrupt>\n", prof);
        kbc_macro_destroy(m);
        return;
    }
    if (macro_render(m, rendered, sizeof(rendered)) != 0) {
        snprintf(rendered, sizeof(rendered), "<render error>");
    }
    {
        char keyname[64];
        int krc = macro_key_name((uint8_t)e->key_index, keyname, sizeof(keyname));
        const char *name = (krc == 0) ? keyname : NULL;

        printf("profile %u: key: %s (position %u), reps: %u, steps: %zu, macro: %s\n",
               prof, (name != NULL) ? name : "?",
               (unsigned)e->key_index,
               (unsigned)kbc_macro_get_repetitions(m),
               kbc_macro_step_count(m), rendered);
    }
    kbc_macro_destroy(m);
}

static int cmd_macro_list(int argc, char **argv)
{
    unsigned long prof = 0;
    int have_profile = 0;
    int i;
    int rc = 0;
    macro_entry_t *entries = NULL;
    int count = 0;
    int corrupt = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "kbctl: --profile requires a value (1-4)\n");
                return 2;
            }
            if (kbctl_parse_uint(argv[++i], 4, &prof) != 0 || prof < 1 || prof > 4) {
                fprintf(stderr, "kbctl: invalid profile (expected 1-4)\n");
                return 2;
            }
            have_profile = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage_command(stdout, "macro");
            printf("\nmacro list [--profile 1-4]\n\n"
                   "List the macros saved locally for each profile (offline; no daemon needed).\n"
                   "A profile can hold up to 255 macros, one file macro-profile<N>-MMM.bin each.\n"
                   "Old format macro-profile<N>.bin is treated as index 1.\n"
                   "Without --profile, all four profiles are scanned and saved/corrupt ones are shown in a compact\n"
                   "table. With --profile, a detailed view is shown for that profile only.\n\n"
                   "Exit status: 0 on success (including no saved macro), 1 on a read error,\n"
                   "2 on a usage error.\n\n"
                   "Files: $XDG_CONFIG_HOME/kbdd/macro-profile<N>-MMM.bin (mode 0600).\n");
            return 0;
        } else {
            fprintf(stderr, "kbctl: unexpected argument '%s' (see: kbctl macro list --help)\n",
                    argv[i]);
            return 2;
        }
    }

    if (have_profile) {
        rc = macro_state_load_all((unsigned)prof, &entries, &count, &corrupt);
        if (rc < 0) {
            fprintf(stderr, "kbctl: cannot read macro state for profile %lu: %s\n",
                    prof, strerror(errno));
            return 1;
        }
        if (corrupt) {
            printf("profile %lu: macro: <corrupt>\n", prof);
        }
        if (count == 0) {
            if (!corrupt) {
                printf("profile %lu: no macro saved\n", prof);
            }
            free(entries);
            return 0;
        }
        for (i = 0; i < count; i++) {
            macro_print_entry((unsigned)prof, &entries[i]);
        }
        free(entries);
        return 0;
    }

    /* Without --profile: scan all four profiles. */
    {
        int any = 0;

        for (i = 1; i <= 4; i++) {
            macro_entry_t *pentries = NULL;
            int pcount = 0;
            int pcorrupt = 0;
            int prc = macro_state_load_all((unsigned)i, &pentries, &pcount, &pcorrupt);

            if (prc < 0) {
                fprintf(stderr, "kbctl: cannot read macro state for profile %u: %s\n",
                        i, strerror(errno));
                rc = 1;
                free(pentries);
                continue;
            }
            if (pcorrupt) {
                printf("profile %u: macro: <corrupt>\n", i);
                any = 1;
            }
            if (pcount > 0) {
                int j;

                for (j = 0; j < pcount; j++) {
                    macro_print_entry((unsigned)i, &pentries[j]);
                }
                any = 1;
            }
            free(pentries);
        }
        if (rc == 0 && !any) {
            printf("(no macros saved for any profile)\n");
        }
    }
    return rc;
}

/* macro [--layer 0|1] [--repeat N] <key> <step>... | <key> clear | list */
static int cmd_macro(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[2], "list") == 0) {
        return cmd_macro_list(argc - 2, argv + 2);
    }
    uint8_t payload[KBDD_MACRO_PAYLOAD_SIZE];
    uint8_t *map = payload, *mac = payload + 3 * KBC_KEY_POSITIONS + 1;
    uint8_t def[KBC_KEY_POSITIONS];
    size_t layer = 0, nsteps = 0, l;
    unsigned prof = 0, reps = 1;
    char spath[512], spath_macro[512];
    int i, pos = -1, clear = 0, rc, have_path, have_macro_path;
    FILE *f;

    if (active_profile(&prof) != 0) return 1;
    have_path = keymap_state_path(prof, spath, sizeof(spath)) == 0;
    have_macro_path = macro_state_path(prof, spath_macro, sizeof(spath_macro)) == 0;
    memset(payload, 0, sizeof(payload));
    for (l = 0; l < 3; l++) (void)kbc_keymap_default(l, map + l * KBC_KEY_POSITIONS);
    if (have_path && (f = fopen(spath, "rb")) != NULL) {
        uint8_t saved[3 * KBC_KEY_POSITIONS];
        if (fread(saved, 1, sizeof(saved), f) == sizeof(saved)) memcpy(map, saved, sizeof(saved));
        fclose(f);
    }
    /* Carregar todos os macros salvos deste perfil para preservar
     * seus marcadores 0xB2 no keymap antes de modificar qualquer tecla. */
    macro_entry_t *saved_entries = NULL;
    int saved_count = 0;
    int saved_corrupt = 0;
    if (macro_state_load_all((unsigned)prof, &saved_entries, &saved_count,
                             &saved_corrupt) != 0) {
        saved_entries = NULL;
        saved_count = 0;
    }
    /* Limpar todos os marcadores 0xB2 orfãos (restaurando o código de fábrica)
     * antes de vincular ou limpar, para que uma reatribuição não deixe uma
     * segunda tecla apontando ao mesmo blob e um clear apenas de keymap não
     * deixe um 0xB2 órfão (o daemon o rejeita). Depois, restaurar os marcadores
     * 0xB2 de todos os macros já salvos, exceto a tecla que está sendo
     * modificada (será tratada abaixo). */
    macro_markers_to_default(map);
    for (int j = 0; j < saved_count; j++) {
        if ((int)saved_entries[j].key_index == pos) continue; /* tecla em modificação */
        size_t mlayer = saved_entries[j].key_index / KBC_KEY_POSITIONS;
        size_t mpos   = saved_entries[j].key_index % KBC_KEY_POSITIONS;
        map[mlayer * KBC_KEY_POSITIONS + mpos] = 0xB2;
    }
    /* options first, then the key, then steps */
    for (i = 2; i < argc && pos < 0; i++) {
        if (strcmp(argv[i], "--layer") == 0) {
            const char *v = i + 1 < argc ? argv[++i] : "";
            if (strcmp(v, "0") == 0 || strcmp(v, "normal") == 0) layer = 0;
            else if (strcmp(v, "1") == 0 || strcmp(v, "fn") == 0) layer = 1;
            else goto usage;
        } else if (strcmp(argv[i], "--repeat") == 0) {
            char *end;
            long v = i + 1 < argc ? strtol(argv[++i], &end, 10) : -1;
            if (v < 0 || v > KBC_MACRO_MAX_REPETITIONS || *end != '\0') goto usage;
            reps = (unsigned)v;
        } else {
            pos = kbc_key_position(argv[i]);
            if (pos < 0) {
                fprintf(stderr, "kbctl: unknown key '%s' (see: kbctl keys)\n", argv[i]);
                return 2;
            }
        }
    }
    if (pos < 0 || i >= argc) goto usage;
    if (i == argc - 1 && strcmp(argv[i], "clear") == 0) clear = 1;

    if (clear) {
        (void)kbc_keymap_default(layer, def);
        map[layer * KBC_KEY_POSITIONS + (size_t)pos] = def[pos];
        rc = profile_write(KBDD_IPC_OP_SET_KEYMAP, map, 3 * KBC_KEY_POSITIONS);
    } else {
        kbc_macro *m = kbc_macro_create();
        if (m == NULL) { free(saved_entries); return 1; }
        if (kbc_macro_set_repetitions(m, (uint16_t)reps) != KBC_OK) { kbc_macro_destroy(m); goto usage; }
        for (; i < argc; i++) {
            char spec[64], *colon;
            const char *name = spec;
            int code, mode = 2;          /* 0 up, 1 down, 2 press */
            long ms = KBC_MACRO_MIN_DELAY;

            if (strlen(argv[i]) >= sizeof(spec)) { kbc_macro_destroy(m); goto usage; }
            snprintf(spec, sizeof(spec), "%s", argv[i]);
            if (spec[0] == '+') { mode = 1; name++; }
            else if (spec[0] == '-' && spec[1] != '\0') { mode = 0; name++; }
            colon = strrchr(spec, ':');
            if (colon != NULL) {
                char *end;
                *colon = '\0';
                ms = strtol(colon + 1, &end, 10);
                if (*end != '\0' || ms < KBC_MACRO_MIN_DELAY || ms > 255) { kbc_macro_destroy(m); goto usage; }
            }
            code = kbc_hid_code(name);
            if (code < 0 || code > 0xFF) {
                fprintf(stderr, "kbctl: unknown or unsupported code '%s'\n", name);
                kbc_macro_destroy(m);
                return 2;
            }
            if (mode != 0 && kbc_macro_add_step(m, (uint8_t)ms, 1, (uint8_t)code) != KBC_OK) goto bad_step;
            if (mode != 1 && kbc_macro_add_step(m, (uint8_t)ms, 0, (uint8_t)code) != KBC_OK) goto bad_step;
            nsteps++;
            continue;
bad_step:
            fprintf(stderr, "kbctl: invalid step '%s' or more than %d steps\n", argv[i], KBC_MACRO_MAX_STEPS);
            kbc_macro_destroy(m);
            return 2;
        }
        if (nsteps == 0 || kbc_macro_encode(m, mac, KBC_MACRO_SIZE) != KBC_OK) { kbc_macro_destroy(m); goto usage; }
        kbc_macro_destroy(m);
        map[layer * KBC_KEY_POSITIONS + (size_t)pos] = 0xB2;
        payload[3 * KBC_KEY_POSITIONS] = (uint8_t)(layer * KBC_KEY_POSITIONS + (size_t)pos);
        rc = profile_write(KBDD_IPC_OP_SET_MACRO, payload, sizeof(payload));
    }
    if (rc == 0 && have_path && (f = fopen(spath, "wb")) != NULL) {
        (void)fwrite(map, 1, 3 * KBC_KEY_POSITIONS, f);
        fclose(f);
        (void)chmod(spath, 0600);
    }
    if (rc == 0 && have_macro_path) {
        if (clear) {
            /* Remove apenas o arquivo do macro da tecla sendo limpa.
             * Encontrar o slot cujo key_index corresponde a pos. */
            char cpath[512];
            for (int j = 0; j < saved_count; j++) {
                if ((int)saved_entries[j].key_index != pos) continue;
                if (saved_entries[j].idx == 1) {
                    if (macro_state_path((unsigned)prof, cpath, sizeof(cpath)) != 0) continue;
                } else {
                    if (macro_state_path_idx((unsigned)prof, saved_entries[j].idx,
                                             cpath, sizeof(cpath)) != 0) continue;
                }
                (void)remove(cpath);
            }
        } else if (pos >= 0) {
            /* Persist macro key index + data para restore. Encontra o próximo
             * slot livre (auto-incremento). macro_state_save trata disso. */
            uint8_t mkey = (uint8_t)(layer * KBC_KEY_POSITIONS + (size_t)pos);
            if (macro_state_save(prof, mkey, mac) != 0) {
                fprintf(stderr, "kbctl: macro written to device but local state not saved\n");
            }
        }
    }
    free(saved_entries);
    return rc;
usage:
    free(saved_entries);
    fprintf(stderr, "kbctl: macro [--layer 0|1] [--repeat N] <key> <step>... | macro <key> clear  (see: kbctl help macro)\n");
    return 2;
}

/* remap [--layer L] <key>=<code> ... | reset : 3 x 70 key matrix of the active profile.
 * Starts from the last matrix written by kbctl for this profile (factory map if none). */
static int cmd_remap(int argc, char **argv)
{
    uint8_t payload[3 * KBC_KEY_POSITIONS];
    size_t l, layer = 0;
    unsigned prof = 0;
    char spath[512];
    int i, n = 0, rc, have_path;
    FILE *f;

    if (active_profile(&prof) != 0) return 1;
    have_path = keymap_state_path(prof, spath, sizeof(spath)) == 0;
    for (l = 0; l < 3; l++) (void)kbc_keymap_default(l, payload + l * KBC_KEY_POSITIONS);
    if (argc == 3 && strcmp(argv[2], "reset") == 0) {
        rc = profile_write(KBDD_IPC_OP_SET_KEYMAP, payload, sizeof(payload));
        if (rc == 0 && have_path) (void)remove(spath);
        return rc;
    }
    if (have_path && (f = fopen(spath, "rb")) != NULL) {
        uint8_t saved[sizeof(payload)];
        if (fread(saved, 1, sizeof(saved), f) == sizeof(saved)) memcpy(payload, saved, sizeof(saved));
        fclose(f);
        macro_markers_to_default(payload);
    }
    for (i = 2; i < argc; i++) {
        char spec[128], *eq;
        int pos, code;
        if (strcmp(argv[i], "--layer") == 0) {
            const char *v = i + 1 < argc ? argv[++i] : "";
            if (strcmp(v, "0") == 0 || strcmp(v, "normal") == 0) layer = 0;
            else if (strcmp(v, "1") == 0 || strcmp(v, "fn") == 0) layer = 1;
            else if (strcmp(v, "2") == 0 || strcmp(v, "custom") == 0) layer = 2;
            else goto usage;
            continue;
        }
        if (strlen(argv[i]) >= sizeof(spec)) goto usage;
        snprintf(spec, sizeof(spec), "%s", argv[i]);
        eq = strrchr(spec, '=');
        if (eq == NULL || eq == spec) goto usage;
        *eq = '\0';
        pos = kbc_key_position(spec);
        code = kbc_hid_code(eq + 1);
        if (pos < 0) {
            fprintf(stderr, "kbctl: unknown key '%s' (see: kbctl keys)\n", spec);
            return 2;
        }
        if (code < 0) {
            fprintf(stderr, "kbctl: unknown or unsupported code '%s'\n", eq + 1);
            return 2;
        }
        payload[layer * KBC_KEY_POSITIONS + (size_t)pos] = (uint8_t)code;
        n++;
    }
    if (n == 0) goto usage;
    rc = profile_write(KBDD_IPC_OP_SET_KEYMAP, payload, sizeof(payload));
    if (rc == 0 && have_path && (f = fopen(spath, "wb")) != NULL) {
        (void)fwrite(payload, 1, sizeof(payload), f);
        fclose(f);
        (void)chmod(spath, 0600);
    }
    return rc;
usage:
    fprintf(stderr, "kbctl: remap [--layer 0|1|2] <key>=<code> [...] | remap reset\n");
    return 2;
}

static int cmd_led_color(int argc, char **argv)
{
    uint8_t payload[2 + 3 * 14];
    int m, save = 0, rc;
    int n = 0, i;

    if (argc >= 6 && strcmp(argv[argc - 1], "--save") == 0) {
        save = 1;
        argc--;
    }
    if (argc < 5 || (m = effect_from_arg(argv[3])) < 0 ||
        m >= KBC_LED_EFFECT_COUNT || argc - 4 > 14) {
        fprintf(stderr, "kbctl: led color <effect> <RRGGBB> [RRGGBB ...] [--save]  (max 14 colors)\n");
        return 2;
    }
    payload[0] = (uint8_t)m;
    for (i = 4; i < argc; i++, n++) {
        if (kbctl_parse_rgb(argv[i], &payload[2 + 3 * n], &payload[3 + 3 * n], &payload[4 + 3 * n]) != 0) {
            fprintf(stderr, "kbctl: invalid color '%s' (use RRGGBB)\n", argv[i]);
            return 2;
        }
    }
    payload[1] = (uint8_t)n;
    rc = profile_write(KBDD_IPC_OP_SET_COLORS, payload, (size_t)(2 + 3 * n));
    return (rc == 0 && save) ? save_to_flash() : rc;
}

static int cmd_led_effect(int argc, char **argv)
{
    uint8_t mode;
    int m, save = wants_save(&argc, argv), rc;
    if (argc != 4 || (m = effect_from_arg(argv[3])) < 0 ||
        (m >= KBC_LED_EFFECT_COUNT && m != KBC_LED_EFFECT_CUSTOM)) {
        fprintf(stderr, "kbctl: led effect <name|index> [--save]  (see: kbctl led effects)\n");
        return 2;
    }
    mode = (uint8_t)m;
    rc = profile_write(KBDD_IPC_OP_SET_EFFECT, &mode, 1);
    if (rc != 0) return rc;
    if (save) {
        if (mode == KBC_LED_EFFECT_CUSTOM) {
            /* The slot data is already in flash; 'led save' refuses a custom block. */
            printf("note: custom layout is already stored in flash; nothing to save\n");
        } else {
            rc = save_to_flash();
        }
    }
    return rc;
}

static int cmd_led_field(int argc, char **argv, uint8_t field, unsigned long lo,
                         unsigned long hi, const char *hint)
{
    unsigned long v;
    uint8_t payload[2];
    int save = wants_save(&argc, argv), rc;
    if (argc != 4 || kbctl_parse_uint(argv[3], hi, &v) != 0 || v < lo) {
        fprintf(stderr, "kbctl: led %s <%lu-%lu> [--save]  %s\n", argv[2], lo, hi, hint);
        return 2;
    }
    payload[0] = field;
    payload[1] = (uint8_t)v;
    rc = profile_write(KBDD_IPC_OP_SET_FIELD, payload, 2);
    return (rc == 0 && save) ? save_to_flash() : rc;
}

static int cmd_keys(void)
{
    size_t i;
    for (i = 0; i < KBC_KEY_POSITIONS; i++) {
        if (kbc_key_name(i) != NULL) printf("%2zu  %s\n", i, kbc_key_name(i));
    }
    puts("\nFn layer: F1..F12 are Fn+1..Fn+= (use --layer fn <key>).");
    return 0;
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
        printf("index  effect\n");
        for (m = 0; m < KBC_LED_EFFECT_COUNT; m++) {
            size_t count;
            if (kbc_effect_color_slots((uint8_t)m, NULL, &count)) {
                printf("%5u   %-10s colors 1..%zu%s\n", m,
                       kbc_led_effect_name((uint8_t)m), count,
                       m == KBC_LED_EFFECT_LIGHTING ? " (cor fixa)" : "");
            } else {
                printf("%5u   %-10s colors none\n", m,
                       kbc_led_effect_name((uint8_t)m));
            }
        }
        printf("   15   custom\n");
        printf("speed %d..%d, brightness %d..%d\n", KBC_LED_SPEED_MIN,
               KBC_LED_SPEED_MAX, KBC_LED_BRIGHTNESS_MIN, KBC_LED_BRIGHTNESS_MAX);
        return 0;
    }
    if (strcmp(sub, "save") == 0) {
        if (argc != 3) { usage(stderr); return 2; }
        return profile_write(KBDD_IPC_OP_SAVE_BLOCK, NULL, 0);
    }
    if (strcmp(sub, "effect") == 0) return cmd_led_effect(argc, argv);
    if (strcmp(sub, "color") == 0) return cmd_led_color(argc, argv);
    if (strcmp(sub, "brightness") == 0)
        return cmd_led_field(argc, argv, 2, 0, KBC_BRIGHTNESS_RAW_MAX, "(0 = off, 10 = max)");
    if (strcmp(sub, "speed") == 0)
        return cmd_led_field(argc, argv, 3, KBC_SPEED_RAW_MIN, KBC_SPEED_RAW_MAX,
                             "(raw; lower = faster; one value shared by all effects)");
    if (strcmp(sub, "direction") == 0)
        return cmd_led_field(argc, argv, KBC_FIELD_DIRECTION, 0, 3,
                             "(0..3; animation direction)");
    if (strcmp(sub, "report-rate") == 0)
        return cmd_led_field(argc, argv, KBC_FIELD_REPORT_RATE, 0, 254,
                             "(index; stored as index + 1 in profile byte 0)");
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
            if (argc < 4 || kbctl_parse_rgb(argv[3], &r, &g, &b) != 0) {
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
        if (argc < 5 || kbctl_parse_uint(argv[3], 143, &pos) != 0 ||
            kbctl_parse_rgb(argv[4], &r, &g, &b) != 0) {
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
    if (strcmp(sub, "custom") == 0) return cmd_led_custom(argc, argv);
    if (strcmp(sub, "keys") == 0) return cmd_keys();
    if (strcmp(sub, "key") == 0) {
        int pos[KBC_KEY_POSITIONS];
        size_t n = 0, i, j;
        uint8_t r, g, b;
        char spec[256], *tok, *save = NULL;
        if (argc < 5 || kbctl_parse_rgb(argv[4], &r, &g, &b) != 0 || strlen(argv[3]) >= sizeof(spec)) {
            fprintf(stderr, "kbctl: led key <k1[,k2,...]> <RRGGBB>  (see: kbctl keys)\n");
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
                fprintf(stderr, "kbctl: unknown key '%s' (see: kbctl keys)\n", tok);
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
        printf("Note which key lights up at each position. Press Ctrl+C to stop.\n");
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
                printf("position %lu%s%s\n", i, kn ? "  " : "", kn ? kn : "");
            }
            fflush(stdout);
            sleep_ms(o.delay);
        }
        (void)led_call(0, 0, KBDD_LED_MODE_RELEASE, 0, 0, 0, 0, msg, sizeof(msg));
        printf("sweep complete\n");
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

/* Path of the systemd drop-in that kbctl writes to persist capabilities,
 * mirroring what ./install.sh creates:
 * ~/.config/systemd/user/kbdd.service.d/led.conf
 * with lines like: Environment=KBDD_ENABLE_LED=1 */
static int kbctl_dropin_path(char *out, size_t outsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[512];

    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/systemd/user/kbdd.service.d", xdg);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/.config/systemd/user/kbdd.service.d", home);
    } else {
        return -1;
    }
    if (mkdir_p(dir) != 0) return -1;
    if (snprintf(out, outsz, "%s/led.conf", dir) >= (int)outsz) return -1;
    return 0;
}

/* Write (or overwrite) the drop-in with the given Environment lines.
 * Mode 0600, owned by the current user. */
static int kbctl_dropin_write(int led, int write)
{
    char path[512];
    FILE *f;

    if (kbctl_dropin_path(path, sizeof(path)) != 0) return -1;

    f = fopen(path, "w");
    if (f == NULL) return -1;
    if (fchmod(fileno(f), 0600) != 0) {
        fclose(f);
        return -1;
    }
    fprintf(f, "[Service]\n");
    if (led) fprintf(f, "Environment=KBDD_ENABLE_LED=1\n");
    if (write) fprintf(f, "Environment=KBDD_ENABLE_PROFILE_WRITE=1\n");
    if (fclose(f) != 0) return -1;
    return 0;
}

/* Remove a single "Environment=KBDD_ENABLE_<NAME>=1" line from the drop-in,
 * preserving all other lines. Returns 0 if the line was not present. */
static int kbctl_dropin_remove_env(const char *name)
{
    char path[512];
    FILE *f, *tmp;
    char line[256];
    char want[128];
    int found = 0;

    if (kbctl_dropin_path(path, sizeof(path)) != 0) return -1;

    f = fopen(path, "r");
    if (f == NULL) return errno == ENOENT ? 0 : -1;

    snprintf(want, sizeof(want), "Environment=KBDD_ENABLE_%s=1", name);

    tmp = tmpfile();
    if (tmp == NULL) { fclose(f); return -1; }

    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, want) == 0) {
            found = 1;
            continue;
        }
        fprintf(tmp, "%s\n", line);
    }
    fclose(f);

    if (!found) {
        fclose(tmp);
        return 0;
    }

    /* Rewrite the file without the removed line. */
    rewind(tmp);
    f = fopen(path, "w");
    if (f == NULL) { fclose(tmp); return -1; }
    if (fchmod(fileno(f), 0600) != 0) { fclose(f); fclose(tmp); return -1; }
    {
        int ch;
        while ((ch = fgetc(tmp)) != EOF) fputc(ch, f);
    }
    if (fclose(f) != 0 || fclose(tmp) != 0) return -1;
    return 0;
}

/* Apply persisted capability changes: reload systemd and restart kbdd so the
 * new Environment= lines take effect immediately. */
static int kbctl_systemd_apply(void)
{
    if (system("systemctl --user daemon-reload") != 0) {
        fprintf(stderr, "kbctl: warning: 'systemctl --user daemon-reload' failed\n");
        return -1;
    }
    if (system("systemctl --user restart kbdd") != 0) {
        fprintf(stderr, "kbctl: warning: 'systemctl --user restart kbdd' failed\n");
        return -1;
    }
    return 0;
}

/* Confirms a capability-changing action on stdin.
 * Returns 1 if the user answered 'y'/'Y', 0 otherwise (including EOF). */
static int confirm_action(const char *prompt)
{
    char buf[16];
    for (;;) {
        fputs(prompt, stdout);
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            return 0; /* EOF -> treat as no */
        }
        if (buf[0] == 'y' || buf[0] == 'Y') {
            return 1;
        }
        if (buf[0] == 'n' || buf[0] == 'N') {
            return 0;
        }
        /* Invalid input: reprompt. */
    }
}

/* kbctl caps [led|write|all] [on|off]
 *
 * With no arguments, prints the current capability state.
 * With a target and action, enables or disables the given capability.
 * Enabling a capability that is already enabled is a no-op.
 * Enabling a disabled capability requires interactive confirmation (y/n). */
static int cmd_caps(int argc, char **argv)
{
    uint8_t *resp = NULL;
    uint32_t rlen = 0;
    int cur_led = 0, cur_write = 0;

    /* Read current state first. */
    if (ipc_request(KBDD_IPC_OP_GET_CAPS, NULL, 0, &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    if (rlen < 1) {
        free(resp);
        fprintf(stderr, "kbctl: daemon did not return capability state\n");
        return 1;
    }
    cur_led   = (resp[0] & KBDD_CAPS_MASK_LED) ? 1 : 0;
    cur_write = ((resp[0] >> 1) & KBDD_CAPS_MASK_WRITE) ? 1 : 0;
    free(resp);
    resp = NULL;

    /* Get-only mode. */
    if (argc == 2) {
        printf("LED control:    %s\n", cur_led   ? "enabled" : "disabled");
        printf("Profile write:  %s\n", cur_write ? "enabled" : "disabled");
        return 0;
    }

    /* Set mode: kbctl caps <led|write|all> <on|off> */
    if (argc != 4) {
        fprintf(stderr, "Usage: kbctl caps [led|write|all] [on|off]\n");
        return 2;
    }

    const char *target = argv[2];
    const char *action = argv[3];

    int want_led = 0, want_write = 0;
    if (strcmp(target, "led") == 0) {
        want_led = 1;
    } else if (strcmp(target, "write") == 0) {
        want_write = 1;
    } else if (strcmp(target, "all") == 0) {
        want_led = 1;
        want_write = 1;
    } else {
        fprintf(stderr, "kbctl: invalid target '%s' (use led, write or all)\n",
                target);
        return 2;
    }

    int enable;
    if (strcmp(action, "on") == 0) {
        enable = 1;
    } else if (strcmp(action, "off") == 0) {
        enable = 0;
    } else {
        fprintf(stderr, "kbctl: invalid action '%s' (use on or off)\n", action);
        return 2;
    }

    uint8_t mask = 0, value = 0;
    int changed = 0;
    int new_led = cur_led, new_write = cur_write;

    if (want_led) {
        if (enable && cur_led) {
            printf("LED control is already enabled.\n");
        } else if (enable) {
            if (!confirm_action("Are you sure you want to enable LED control? (y/n): ")) {
                printf("Aborted.\n");
                return 0;
            }
            mask |= KBDD_CAPS_MASK_LED;
            value |= KBDD_CAPS_MASK_LED;
            new_led = 1;
            changed = 1;
        } else if (!enable && !cur_led) {
            printf("LED control is already disabled.\n");
        } else {
            /* disable while enabled: no confirmation needed */
            mask |= KBDD_CAPS_MASK_LED;
            new_led = 0;
            changed = 1;
        }
    }

    if (want_write) {
        if (enable && cur_write) {
            printf("Profile write is already enabled.\n");
        } else if (enable) {
            if (!confirm_action("Are you sure you want to enable profile writes? (y/n): ")) {
                printf("Aborted.\n");
                return 0;
            }
            mask |= KBDD_CAPS_MASK_WRITE;
            value |= (uint8_t)(KBDD_CAPS_MASK_WRITE << 1);
            new_write = 1;
            changed = 1;
        } else if (!enable && !cur_write) {
            printf("Profile write is already disabled.\n");
        } else {
            mask |= KBDD_CAPS_MASK_WRITE;
            new_write = 0;
            changed = 1;
        }
    }

    if (!changed) {
        return 0;
    }

    uint8_t payload[2] = { mask, value };
    if (ipc_request(KBDD_IPC_OP_SET_CAPS, payload, sizeof(payload), &resp, &rlen) != 0) {
        free(resp);
        return 1;
    }
    int perr = print_reply(resp, rlen);
    free(resp);
    if (perr != 0) return perr;

    /* Persist to the systemd drop-in (same file ./install.sh creates) so the
     * flag survives a 'systemctl --user restart kbdd'. */
    if (enable) {
        if (kbctl_dropin_write(new_led, new_write) != 0) {
            fprintf(stderr, "kbctl: warning: could not write systemd drop-in\n");
        } else {
            if (kbctl_systemd_apply() != 0) {
                fprintf(stderr,
                    "kbctl: warning: could not apply systemd changes automatically\n");
            }
        }
    } else {
        if (want_led) kbctl_dropin_remove_env("LED");
        if (want_write) kbctl_dropin_remove_env("WRITE");
        if (!new_led && !new_write) {
            char path[512];
            if (kbctl_dropin_path(path, sizeof(path)) == 0) (void)unlink(path);
        }
        if (want_led || want_write) {
            if (kbctl_systemd_apply() != 0) {
                fprintf(stderr,
                    "kbctl: warning: could not apply systemd changes automatically\n");
            }
        }
    }

    return perr;
}

static int cmd_dump_block(int argc, char **argv)
{
    unsigned long prof;
    uint8_t payload[1];
    uint8_t *resp = NULL;
    uint32_t rlen = 0;

    if (argc < 3 || argc > 4 || kbctl_parse_uint(argv[2], 4, &prof) != 0 || prof < 1) {
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
    printf("profile %lu, 264 bytes (header 0..7, data 8..263)\n", prof);
    hexdump_block(resp, rlen);
    {
        const char *effect = kbc_led_effect_name(resp[11]);
        printf("fields (profile block offsets; report bytes in brackets):\n"
               "  report rate=[8]=%u (index %u)\n"
               "  effect=[11]=%u (%s)  brightness=[12]=%u  direction=[13]=%u  speed=[19]=%u\n",
               resp[8], resp[8] > 0 ? resp[8] - 1 : 0, resp[11],
               effect ? effect : "?", resp[12], resp[13], resp[19]);
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
        printf("saved to %s\n", argv[3]);
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
    printf("%zu byte(s) differ\n", diffs);
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
        long idx = 1;
        if (argc == 3) {
            if (kbctl_parse_index(argv[2], &idx) != 0) {
                fprintf(stderr, "kbctl: invalid profile (expected 1-4)\n");
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
        int perr;
        if (rlen == KBC_FEATURE_REPORT_SIZE && resp[0] == KBC_FEATURE_REPORT_ID) {
            printf("profile %ld, 264 bytes "
                   "(header 0..7, data 8..263)\n", idx);
            hexdump_block(resp, rlen);
            perr = 0;
        } else {
            perr = print_reply(resp, rlen);
        }
        free(resp);
        return perr;
    }

    if (strcmp(cmd, "restore-profile") == 0) {
        if (argc != 4) {
            usage(stderr);
            return 2;
        }
        long profile;
        uint8_t *filebuf = NULL;
        size_t filelen = 0;
        if (kbctl_parse_index(argv[2], &profile) != 0) {
            fprintf(stderr, "kbctl: invalid profile (expected 1-4)\n");
            return 2;
        }
        if (read_file(argv[3], &filebuf, &filelen) != 0) {
            fprintf(stderr, "kbctl: cannot read '%s': %s\n", argv[3], strerror(errno));
            return 1;
        }
        if (filelen != KBC_FEATURE_REPORT_SIZE) {
            fprintf(stderr, "kbctl: recovery file must contain exactly 264 bytes\n");
            free(filebuf);
            return 2;
        }
        uint8_t *payload = malloc(1 + filelen);
        if (payload == NULL) {
            fprintf(stderr, "kbctl: out of memory\n");
            free(filebuf);
            return 1;
        }
        payload[0] = (uint8_t)profile;
        memcpy(payload + 1, filebuf, filelen);
        free(filebuf);

        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        int rc = ipc_request(KBDD_IPC_OP_RESTORE_PROFILE, payload,
                             (uint32_t)(1 + filelen), &resp, &rlen);
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
        if (argc != 3 || kbctl_parse_uint(argv[2], 4, &p) != 0 || p < 1) {
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
    if (strcmp(cmd, "opts") == 0) {
        unsigned long d, w;
        uint8_t payload[2];
        uint8_t *resp = NULL;
        uint32_t rlen = 0;
        int perr;
        if (argc != 4 || kbctl_parse_uint(argv[2], 1, &d) != 0 ||
            kbctl_parse_uint(argv[3], 1, &w) != 0) {
            fprintf(stderr, "kbctl: opts <direction 0|1> <winlock 0|1>\n");
            return 2;
        }
        payload[0] = (uint8_t)d; payload[1] = (uint8_t)w;
        if (ipc_request(KBDD_IPC_OP_SET_OPTS, payload, 2, &resp, &rlen) != 0) {
            free(resp);
            return 1;
        }
        perr = print_reply(resp, rlen);
        free(resp);
        return perr;
    }
    if (strcmp(cmd, "caps") == 0) return cmd_caps(argc, argv);
    if (strcmp(cmd, "dump-block") == 0) return cmd_dump_block(argc, argv);
    if (strcmp(cmd, "diff-block") == 0) return cmd_diff_block(argc, argv);
    if (strcmp(cmd, "keys") == 0) return cmd_keys();
    if (strcmp(cmd, "remap") == 0) return cmd_remap(argc, argv);
    if (strcmp(cmd, "macro") == 0) return cmd_macro(argc, argv);
    if (strcmp(cmd, "led") == 0) return cmd_led(argc, argv);

    fprintf(stderr, "kbctl: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}