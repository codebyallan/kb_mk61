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
    kbdd_hid_device_t *devices;
    size_t count;
    int allow_led;                       /* KBDD_ENABLE_LED=1 */
    int allow_profile_write;             /* KBDD_ENABLE_PROFILE_WRITE=1 */
    int led_hold;                        /* streaming active */
    int led_active;                      /* a host-controlled frame is installed */
    int led_anim;                        /* 0 none, else kbc_led_anim */
    uint32_t led_anim_period;
    uint8_t led_anim_rgb[3];
    size_t led_anim_count;
    long long led_anim_start_ms;
    uint8_t led_frame[KBC_LED_BUFFER_SIZE];
    uint8_t led_base_frame[KBC_LED_BUFFER_SIZE];
    uint8_t led_brightness;              /* host-frame scale, 0..10 */
    size_t led_stride;
    int led_fail;
    long long led_next_ms;
    uint8_t config_mode;                 /* 0 none, 1 effect, 2 solid, 3 positions */
    uint8_t config_effect;
    uint8_t config_speed[KBC_LED_EFFECT_COUNT];
    uint8_t config_brightness;
    uint8_t config_solid[3];
    uint8_t config_positions[KBC_MAX_MATRIX_SIZE][3];
    size_t config_stride;
    uint8_t config_anim;
    uint32_t config_anim_period;
    uint8_t config_anim_rgb[3];
    int restore_pending;
    unsigned restore_attempts;
    long long restore_next_ms;
    kbdd_ipc_server_t *ipc;
} kbdd_ctx_t;

#define KBDD_CONFIG_NONE 0u
#define KBDD_CONFIG_EFFECT 1u
#define KBDD_CONFIG_SOLID 2u
#define KBDD_CONFIG_POSITIONS 3u
#define KBDD_CONFIG_ANIMATION 4u

static int kbdd_save_config(const kbdd_ctx_t *k);
static int kbdd_load_config(kbdd_ctx_t *k);
static int kbdd_restore_config(kbdd_ctx_t *k);
static long long kbdd_now_ms(void);

static void kbdd_enable_device_notifications(kbdd_hid_device_t *devices, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        kbdd_dev_io_t io;
        if (devices[i].role != 0 || devices[i].fd < 0) continue;
        kbdd_dev_io_init_hidraw(&io, &devices[i]);
        if (kbdd_dev_enable_notifications(&io) != KBDD_DEV_OK) {
            fprintf(stderr, "kbdd: could not enable keyboard notifications on %s\n",
                    devices[i].path);
        }
    }
}

static int kbdd_same_devices(const kbdd_hid_device_t *a, size_t acount,
                             const kbdd_hid_device_t *b, size_t bcount)
{
    if (acount != bcount) {
        return 0;
    }
    for (size_t i = 0; i < acount; i++) {
        if (a[i].fd < 0 ||
            strcmp(a[i].path, b[i].path) != 0 ||
            a[i].vid != b[i].vid ||
            a[i].pid != b[i].pid ||
            a[i].usage_page != b[i].usage_page ||
            a[i].usage != b[i].usage ||
            a[i].role != b[i].role) {
            return 0;
        }
    }
    return 1;
}

static int kbdd_refresh_devices(kbdd_ctx_t *k)
{
    kbdd_hid_device_t *found = NULL;
    size_t found_count = 0;

    if (kbdd_hidraw_enumerate(KBDD_MODELS,
                              sizeof(KBDD_MODELS) / sizeof(KBDD_MODELS[0]),
                              &found, &found_count) != 0) {
        return 0;
    }
    if (kbdd_same_devices(k->devices, k->count, found, found_count)) {
        kbdd_hidraw_free(found, found_count);
        return 0;
    }

    for (size_t i = 0; i < found_count; i++) {
        (void)kbdd_hidraw_open(&found[i]);
    }
    kbdd_enable_device_notifications(found, found_count);
    kbdd_hidraw_free(k->devices, k->count);
    k->devices = found;
    k->count = found_count;
    k->led_hold = 0;
    k->led_active = 0;
    k->led_fail = 0;
    fprintf(stderr, "kbdd: device set changed, now tracking %zu interface(s)\n",
            found_count);
    if (found_count != 0 && k->config_mode != KBDD_CONFIG_NONE) {
        k->restore_pending = 1;
        k->restore_attempts = 0;
        k->restore_next_ms = kbdd_now_ms() + 300;
    }
    return 1;
}

static int kbdd_reply_text(kbdd_ipc_message_t *resp, const char *msg);

static void kbdd_publish_input_event(kbdd_ctx_t *k, const uint8_t *buf, size_t len)
{
    kbc_input_report event;
    char text[160];

    if (k == NULL || k->ipc == NULL ||
        kbc_parse_input_report(buf, len, &event) != KBC_OK ||
        event.marker != 0xE1) {
        return;
    }
    if (event.system_subtype == 0x01) {
        snprintf(text, sizeof(text), "profile changed: %u", (unsigned)event.value);
    } else if (event.system_subtype == 0x02) {
        snprintf(text, sizeof(text), "layer changed: %u", (unsigned)event.value);
    } else if (event.system_subtype == 0x08) {
        snprintf(text, sizeof(text), "effect changed: %u (%s)",
                 (unsigned)event.value, kbc_led_effect_name(event.value));
    } else if (event.system_subtype == 0x11 || event.system_subtype == 0x12) {
        snprintf(text, sizeof(text), "custom effect changed: subtype 0x%02x value %u",
                 (unsigned)event.system_subtype, (unsigned)event.value);
    } else {
        snprintf(text, sizeof(text), "keyboard event: subtype 0x%02x value %u",
                 (unsigned)event.system_subtype, (unsigned)event.value);
    }
    (void)kbdd_ipc_server_publish(k->ipc, text);
}

static long long kbdd_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

static int kbdd_config_path(char *path, size_t size)
{
    char dir[512];
    if (kbdd_config_dir(dir, sizeof(dir)) != 0 ||
        snprintf(path, size, "%s/config", dir) >= (int)size) {
        return -1;
    }
    return 0;
}

static void kbdd_config_defaults(kbdd_ctx_t *k)
{
    size_t i;
    memset(k->config_solid, 0, sizeof(k->config_solid));
    memset(k->config_positions, 0, sizeof(k->config_positions));
    k->config_mode = KBDD_CONFIG_NONE;
    k->config_effect = 0;
    k->config_brightness = KBC_BRIGHTNESS_RAW_MAX;
    k->config_stride = 0;
    k->config_anim = 0;
    k->config_anim_period = 0;
    memset(k->config_anim_rgb, 0, sizeof(k->config_anim_rgb));
    for (i = 0; i < KBC_LED_EFFECT_COUNT; i++) {
        k->config_speed[i] = 50;
    }
}

static int kbdd_load_config(kbdd_ctx_t *k)
{
    char path[768];
    FILE *f;
    char key[32];
    unsigned value;
    unsigned speed_effect;
    size_t pos;
    unsigned r, g, b;

    if (k == NULL || kbdd_config_path(path, sizeof(path)) != 0) return -1;
    kbdd_config_defaults(k);
    f = fopen(path, "r");
    if (f == NULL) return errno == ENOENT ? 0 : -1;
    while (fscanf(f, "%31s", key) == 1) {
        if (strcmp(key, "mode") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBDD_CONFIG_ANIMATION) {
                fclose(f);
                kbdd_config_defaults(k);
                return -1;
            }
            k->config_mode = (uint8_t)value;
        } else if (strcmp(key, "effect") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value >= KBC_LED_EFFECT_COUNT) {
                fclose(f);
                return -1;
            }
            k->config_effect = (uint8_t)value;
        } else if (strcmp(key, "animation") == 0) {
            if (fscanf(f, "%u %u %u %u %u", &value, &speed_effect,
                       &r, &g, &b) != 5 ||
                value < KBC_ANIM_BREATH || value > KBC_ANIM_RAINBOW ||
                speed_effect < KBC_ANIM_PERIOD_MIN ||
                speed_effect > KBC_ANIM_PERIOD_MAX ||
                r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_anim = (uint8_t)value;
            k->config_anim_period = speed_effect;
            k->config_anim_rgb[0] = (uint8_t)r;
            k->config_anim_rgb[1] = (uint8_t)g;
            k->config_anim_rgb[2] = (uint8_t)b;
        } else if (strcmp(key, "brightness") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBC_BRIGHTNESS_RAW_MAX) {
                fclose(f);
                return -1;
            }
            k->config_brightness = (uint8_t)value;
        } else if (strcmp(key, "speed") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value >= KBC_LED_EFFECT_COUNT) {
                fclose(f);
                return -1;
            }
            speed_effect = value;
            if (fscanf(f, "%u", &value) != 1 ||
                value < KBC_SPEED_RAW_MIN || value > KBC_SPEED_RAW_MAX) {
                fclose(f);
                return -1;
            }
            k->config_speed[speed_effect] = (uint8_t)value;
        } else if (strcmp(key, "solid") == 0) {
            if (fscanf(f, "%u %u %u", &r, &g, &b) != 3 ||
                r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_solid[0] = (uint8_t)r;
            k->config_solid[1] = (uint8_t)g;
            k->config_solid[2] = (uint8_t)b;
        } else if (strcmp(key, "stride") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBC_MAX_MATRIX_SIZE) {
                fclose(f);
                return -1;
            }
            k->config_stride = value;
        } else if (strcmp(key, "pos") == 0) {
            if (fscanf(f, "%zu %u %u %u", &pos, &r, &g, &b) != 4 ||
                pos >= KBC_MAX_MATRIX_SIZE || r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_positions[pos][0] = (uint8_t)r;
            k->config_positions[pos][1] = (uint8_t)g;
            k->config_positions[pos][2] = (uint8_t)b;
        } else {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static int kbdd_save_config(const kbdd_ctx_t *k)
{
    char path[768], tmp[800];
    FILE *f;
    size_t i;

    if (k == NULL || kbdd_config_path(path, sizeof(path)) != 0 ||
        snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    f = fopen(tmp, "w");
    if (f == NULL) return -1;
    if (fchmod(fileno(f), 0600) != 0) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    if (fprintf(f, "mode %u\neffect %u\nbrightness %u\n",
                (unsigned)k->config_mode, (unsigned)k->config_effect,
                (unsigned)k->config_brightness) < 0) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    for (i = 0; i < KBC_LED_EFFECT_COUNT; i++) {
        if (fprintf(f, "speed %zu %u\n", i, (unsigned)k->config_speed[i]) < 0) {
            fclose(f);
            unlink(tmp);
            return -1;
        }
        if (k->config_mode == KBDD_CONFIG_ANIMATION) {
            if (fprintf(f, "animation %u %u %u %u %u\n",
                        (unsigned)k->config_anim,
                        (unsigned)k->config_anim_period,
                        (unsigned)k->config_anim_rgb[0],
                        (unsigned)k->config_anim_rgb[1],
                        (unsigned)k->config_anim_rgb[2]) < 0) {
                fclose(f);
                unlink(tmp);
                return -1;
            }
        }
    }
    if (k->config_mode == KBDD_CONFIG_SOLID) {
        fprintf(f, "solid %u %u %u\nstride %zu\n",
                (unsigned)k->config_solid[0], (unsigned)k->config_solid[1],
                (unsigned)k->config_solid[2], k->config_stride);
    } else if (k->config_mode == KBDD_CONFIG_POSITIONS) {
        fprintf(f, "stride %zu\n", k->config_stride);
        for (i = 0; i < KBC_MAX_MATRIX_SIZE; i++) {
            if (k->config_positions[i][0] != 0 ||
                k->config_positions[i][1] != 0 ||
                k->config_positions[i][2] != 0) {
                fprintf(f, "pos %zu %u %u %u\n", i,
                        (unsigned)k->config_positions[i][0],
                        (unsigned)k->config_positions[i][1],
                        (unsigned)k->config_positions[i][2]);
            }
        }
    }
    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
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

    k->led_anim = 0;      /* any non-animation LED command ends a running animation */
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

    if (mode == KBDD_LED_MODE_SOLID) {
        memset(k->led_frame, 0, sizeof(k->led_frame));
        memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
        kbc_led_frame_fill(k->led_base_frame, stride, count, r, g, b);
        k->config_mode = KBDD_CONFIG_SOLID;
        k->config_solid[0] = r;
        k->config_solid[1] = g;
        k->config_solid[2] = b;
        memset(k->config_positions, 0, sizeof(k->config_positions));
    } else if (mode == KBDD_LED_MODE_CLEAR) {
        memset(k->led_frame, 0, sizeof(k->led_frame));
        memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
        k->config_mode = KBDD_CONFIG_NONE;
        memset(k->config_solid, 0, sizeof(k->config_solid));
        memset(k->config_positions, 0, sizeof(k->config_positions));
    } else if (mode == KBDD_LED_MODE_POS) {
        if (pos >= count) {
            return kbdd_reply_text(resp, "error: position out of range");
        }
        if (!k->led_active || k->led_stride != stride) {
            memset(k->led_frame, 0, sizeof(k->led_frame));
            memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
        }
        kbc_led_frame_set(k->led_base_frame, stride, pos, r, g, b);
        if (k->config_mode != KBDD_CONFIG_POSITIONS ||
            k->config_stride != stride) {
            memset(k->config_positions, 0, sizeof(k->config_positions));
        }
        k->config_mode = KBDD_CONFIG_POSITIONS;
        k->config_positions[pos][0] = r;
        k->config_positions[pos][1] = g;
        k->config_positions[pos][2] = b;
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
    k->config_stride = stride;
    if (kbdd_save_config(k) != 0) {
        return kbdd_reply_text(resp, "error: LED sent but configuration could not be saved");
    }
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
    k->led_anim = 0;
    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_set_effect(&io, req->payload[0], &prof);
    if (rc != KBDD_DEV_OK) {
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    k->config_mode = KBDD_CONFIG_EFFECT;
    k->config_effect = req->payload[0];
    if (kbdd_save_config(k) != 0) {
        return kbdd_reply_text(resp, "error: effect set but configuration could not be saved");
    }
    snprintf(buf, sizeof(buf), "ok: effect %s (%u) set on profile %u, verified by read-back",
             kbc_led_effect_name(req->payload[0]), (unsigned)req->payload[0], (unsigned)prof);
    return kbdd_reply_text(resp, buf);
}

static int kbdd_handle_profile(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                               kbdd_ipc_message_t *resp)
{
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    char buf[120];
    int rc, changed = 0;
    unsigned p;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile switching is disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 1 || req->payload[0] < 1 || req->payload[0] > 4) {
        return kbdd_reply_text(resp, "error: payload must be [profile 1..4]");
    }
    p = req->payload[0];
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    k->led_hold = 0;      /* host frames belong to the previous profile's lighting */
    k->led_active = 0;
    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_switch_profile(&io, (uint8_t)p, &changed);
    if (rc == KBDD_DEV_EVERIFY) {
        return kbdd_reply_text(resp,
            "error: the keyboard did not confirm the profile switch; previous profile requested back");
    }
    if (rc != KBDD_DEV_OK) {
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    snprintf(buf, sizeof(buf), changed ? "ok: profile %u is now active (confirmed by 0x81)"
                                       : "ok: profile %u was already active", p);
    return kbdd_reply_text(resp, buf);
}

static int kbdd_handle_anim(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                            kbdd_ipc_message_t *resp)
{
    static const char *const names[] = {"?", "breath", "cycle", "rainbow"};
    const uint8_t *p = req->payload;
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    size_t matrix, stride, count;
    uint32_t period;
    char buf[160];
    int rc;

    if (!k->allow_led) {
        return kbdd_reply_text(resp,
            "error: LED control is disabled. Start kbdd with KBDD_ENABLE_LED=1");
    }
    if (p == NULL || req->payload_len != KBDD_ANIM_PAYLOAD_SIZE || p[0] < 1 || p[0] > 3) {
        return kbdd_reply_text(resp, "error: bad animation payload");
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
    period = ((uint32_t)p[2] << 8) | p[3];
    if (period == 0) period = kbc_led_anim_default_period((kbc_led_anim)p[0]);

    memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
    if (kbc_led_anim_render((kbc_led_anim)p[0], 0, period, p[4], p[5], p[6],
                            k->led_base_frame, stride, count) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid animation period (200..60000 ms)");
    }
    memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
    if (kbc_led_frame_apply_brightness(k->led_frame, stride, k->led_brightness) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid LED brightness");
    }
    kbdd_dev_io_init_hidraw(&io, dev);
    rc = kbdd_dev_send_frame(&io, k->led_frame, stride);
    if (rc != KBDD_DEV_OK) {
        k->led_hold = 0;
        k->led_anim = 0;
        return kbdd_reply_text(resp, kbdd_dev_err(rc));
    }
    k->led_stride = stride;
    k->led_active = 1;
    k->led_fail = 0;
    k->led_anim = p[0];
    k->led_anim_period = period;
    k->led_anim_rgb[0] = p[4];
    k->led_anim_rgb[1] = p[5];
    k->led_anim_rgb[2] = p[6];
    k->led_anim_count = count;
    k->led_anim_start_ms = kbdd_now_ms();
    k->led_next_ms = k->led_anim_start_ms + 40;
    k->led_hold = 1;
    k->config_mode = KBDD_CONFIG_ANIMATION;
    k->config_anim = p[0];
    k->config_anim_period = period;
    k->config_anim_rgb[0] = p[4];
    k->config_anim_rgb[1] = p[5];
    k->config_anim_rgb[2] = p[6];
    k->config_stride = stride;
    if (kbdd_save_config(k) != 0) {
        return kbdd_reply_text(resp,
            "error: animation started but configuration could not be saved");
    }
    snprintf(buf, sizeof(buf),
             "ok: %s animation started (25 fps, period %u ms); kbctl led release to stop",
             names[p[0]], (unsigned)period);
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
        k->config_brightness = req->payload[1];
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
    } else if (field == KBC_FIELD_SPEED) {
        k->config_speed[k->config_effect] = req->payload[1];
    } else if (field == KBC_FIELD_MODE) {
        k->config_mode = KBDD_CONFIG_EFFECT;
        k->config_effect = req->payload[1];
    }
    if (kbdd_save_config(k) != 0) {
        return kbdd_reply_text(resp, "error: profile set but configuration could not be saved");
    }
    snprintf(buf, sizeof(buf), "ok: %s set to %u on profile %u, verified by read-back",
             names[field], (unsigned)req->payload[1], (unsigned)prof);
    return kbdd_reply_text(resp, buf);
}

static int kbdd_restore_config(kbdd_ctx_t *k)
{
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    size_t matrix, stride, count, i;
    int rc;

    if (k == NULL || k->config_mode == KBDD_CONFIG_NONE) return 0;
    k->led_anim = 0;      /* restoring the saved mode replaces any animation */
    dev = kbdd_control_dev(k);
    if (dev == NULL) return 0;
    kbdd_dev_io_init_hidraw(&io, dev);
    matrix = dev->model->matrix_size;

    if (k->allow_profile_write) {
        rc = kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS,
                                k->config_brightness, NULL);
        if (rc != KBDD_DEV_OK) {
            fprintf(stderr, "kbdd: could not restore brightness (%d)\n", rc);
            return -1;
        }
        if (k->config_mode == KBDD_CONFIG_EFFECT) {
            rc = kbdd_dev_set_field(&io, KBC_FIELD_MODE,
                                    k->config_effect, NULL);
            if (rc == KBDD_DEV_OK) {
                rc = kbdd_dev_set_field(&io, KBC_FIELD_SPEED,
                                        k->config_speed[k->config_effect], NULL);
            }
            if (rc != KBDD_DEV_OK) {
                fprintf(stderr, "kbdd: could not restore effect (%d)\n", rc);
                return -1;
            }
        }
    }
    if (k->config_mode == KBDD_CONFIG_EFFECT) {
        k->led_hold = 0;
        k->led_active = 0;
        return 0;
    }
    if (!k->allow_led) return 0;

    stride = k->config_stride != 0 ? k->config_stride : matrix;
    count = matrix < stride ? matrix : stride;
    if (kbc_led_frame_page_count(stride) == 0) return -1;
    memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
    if (k->config_mode == KBDD_CONFIG_SOLID) {
        kbc_led_frame_fill(k->led_base_frame, stride, count,
                           k->config_solid[0], k->config_solid[1],
                           k->config_solid[2]);
    } else if (k->config_mode == KBDD_CONFIG_ANIMATION) {
        if (kbc_led_anim_render((kbc_led_anim)k->config_anim, 0,
                                k->config_anim_period,
                                k->config_anim_rgb[0],
                                k->config_anim_rgb[1],
                                k->config_anim_rgb[2],
                                k->led_base_frame, stride, count) != KBC_OK) {
            return -1;
        }
    } else {
        for (i = 0; i < count; i++) {
            kbc_led_frame_set(k->led_base_frame, stride, i,
                              k->config_positions[i][0],
                              k->config_positions[i][1],
                              k->config_positions[i][2]);
        }
    }
    memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
    if (kbc_led_frame_apply_brightness(k->led_frame, stride,
                                       k->config_brightness) != KBC_OK ||
        kbdd_dev_send_frame(&io, k->led_frame, stride) != KBDD_DEV_OK) {
        fprintf(stderr, "kbdd: could not restore host LED configuration\n");
        k->led_hold = 0;
        k->led_active = 0;
        return -1;
    }
    k->led_brightness = k->config_brightness;
    k->led_stride = stride;
    k->led_active = 1;
    k->led_hold = 1;
    k->led_next_ms = kbdd_now_ms() + 40;
    if (k->config_mode == KBDD_CONFIG_ANIMATION) {
        k->led_anim = k->config_anim;
        k->led_anim_period = k->config_anim_period;
        memcpy(k->led_anim_rgb, k->config_anim_rgb, sizeof(k->led_anim_rgb));
        k->led_anim_count = count;
        k->led_anim_start_ms = kbdd_now_ms();
    } else {
        k->led_anim = 0;
    }
    return 0;
}

static void kbdd_restore_tick(kbdd_ctx_t *k)
{
    int rc;

    if (k == NULL || !k->restore_pending ||
        kbdd_now_ms() < k->restore_next_ms) {
        return;
    }
    rc = kbdd_restore_config(k);
    if (rc == 0) {
        k->restore_pending = 0;
        fprintf(stderr, "kbdd: restored saved LED configuration\n");
        return;
    }
    k->restore_attempts++;
    if (k->restore_attempts >= 10) {
        k->restore_pending = 0;
        fprintf(stderr, "kbdd: could not restore saved LED configuration after %u attempts\n",
                k->restore_attempts);
        return;
    }
    k->restore_next_ms = kbdd_now_ms() + 500;
}

static void kbdd_led_tick(kbdd_ctx_t *k)
{
    const kbdd_hid_device_t *dev;
    kbdd_dev_io_t io;
    long long now;

    if (k->led_anim && !k->led_active) k->led_anim = 0;   /* another mode took over */
    if (!k->led_hold) return;
    now = kbdd_now_ms();
    if (now < k->led_next_ms) return;
    dev = kbdd_control_dev(k);
    if (dev == NULL) { k->led_hold = 0; return; }
    if (k->led_anim) {
        if (kbc_led_anim_render((kbc_led_anim)k->led_anim,
                                (uint32_t)(now - k->led_anim_start_ms), k->led_anim_period,
                                k->led_anim_rgb[0], k->led_anim_rgb[1], k->led_anim_rgb[2],
                                k->led_base_frame, k->led_stride, k->led_anim_count) == KBC_OK) {
            memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
            (void)kbc_led_frame_apply_brightness(k->led_frame, k->led_stride, k->led_brightness);
        }
    }
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
            size_t i;
            int failed = 0;
            for (i = 0; i < kctx->count; i++) {
                kbdd_dev_io_t io;
                if (kctx->devices[i].role != 0 || kctx->devices[i].fd < 0) continue;
                kbdd_dev_io_init_hidraw(&io, &kctx->devices[i]);
                if (kbdd_dev_enable_notifications(&io) != KBDD_DEV_OK) failed = 1;
            }
            return kbdd_reply_text(resp, failed
                ? "error: could not enable keyboard notifications"
                : "notifications enabled");
        }
        case KBDD_IPC_OP_WATCH:
            return kbdd_reply_text(resp,
                "watching keyboard system events (Ctrl+C to stop)");
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
        case KBDD_IPC_OP_SET_PROFILE:
            return kbdd_handle_profile(kctx, req, resp);
        case KBDD_IPC_OP_LED_ANIM:
            return kbdd_handle_anim(kctx, req, resp);
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
    kbdd_enable_device_notifications(devices, device_count);

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
    kctx.ipc = ipc;
    kbdd_config_defaults(&kctx);
    if (kbdd_load_config(&kctx) != 0) {
        fprintf(stderr, "kbdd: invalid configuration; using defaults\n");
        kbdd_config_defaults(&kctx);
    }
    kctx.led_brightness = kctx.config_brightness;
    {
        const char *want_led = getenv("KBDD_ENABLE_LED");
        const char *want_pw = getenv("KBDD_ENABLE_PROFILE_WRITE");
        kctx.allow_led = (want_led != NULL && strcmp(want_led, "1") == 0);
        kctx.allow_profile_write = (want_pw != NULL && strcmp(want_pw, "1") == 0);
    }
    if (kctx.count != 0) {
        kctx.restore_pending = 1;
        kctx.restore_next_ms = kbdd_now_ms() + 300;
    }

    /* main loop */
    long long next_device_scan_ms = kbdd_now_ms();
    while (!kbdd_stop_flag) {
        if (ipc != NULL) {
            kbdd_ipc_server_poll(ipc, kctx.led_hold ? 20 : 50, kbdd_ipc_handle, &kctx);
        } else {
            /* sleep briefly */
            struct timespec ts = {0, 50000000L};
            nanosleep(&ts, NULL);
        }

        if (kbdd_now_ms() >= next_device_scan_ms) {
            (void)kbdd_refresh_devices(&kctx);
            next_device_scan_ms = kbdd_now_ms() + 500;
        }
        kbdd_restore_tick(&kctx);
        kbdd_led_tick(&kctx);

        /* read input reports from devices (nonblocking) */
        for (size_t i = 0; i < kctx.count; i++) {
            if (kctx.devices[i].role != 1) {
                continue;
            }
            if (kctx.devices[i].fd < 0) {
                continue;
            }
            uint8_t buf[256];
            ssize_t r = kbdd_hidraw_read(&kctx.devices[i], buf, sizeof(buf));
            if (r > 0) {
                kbdd_publish_input_event(&kctx, buf, (size_t)r);
            } else if (r < 0 &&
                       (errno == EBADF || errno == EIO ||
                        errno == ENODEV || errno == ENXIO)) {
                kbdd_hidraw_close(&kctx.devices[i]);
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
    kbdd_hidraw_free(kctx.devices, kctx.count);
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }

    return 0;
}