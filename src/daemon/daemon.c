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


static long long kbdd_now_ms(void);
static const kbdd_hid_device_t *kbdd_control_dev(const kbdd_ctx_t *k);
static void kbdd_yield_input_reports(void *kctx_ptr);

static int kbdd_same_devices(const kbdd_hid_device_t *a, size_t acount,
                             const kbdd_hid_device_t *b, size_t bcount)
{
    if (acount != bcount) {
        return 0;
    }
    for (size_t i = 0; i < acount; i++) {
        if (strcmp(a[i].path, b[i].path) != 0 ||
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
        /* Same device set — try to re-open any interfaces whose fd is
         * still closed (open() may have failed on a previous scan). */
        for (size_t i = 0; i < k->count; i++) {
            if (k->devices[i].fd < 0) {
                (void)kbdd_hidraw_open(&k->devices[i]);
            }
        }
        return 0;
    }

    for (size_t i = 0; i < found_count; i++) {
        (void)kbdd_hidraw_open(&found[i]);
    }
    kbdd_hidraw_free(k->devices, k->count);
    k->devices = found;
    k->count = found_count;
    for (size_t i = 0; i < found_count; i++) {
        found[i].kctx = k;
        found[i].yield = kbdd_yield_input_reports;
    }
    k->led_hold = 0;
    k->led_active = 0;
    k->led_fail = 0;
    /* A frame queued before the hotplug must not bring streaming back. */
    kbdd_op_cancel_led();
    for (size_t i = 0; i < found_count; i++) {
        if (found[i].role == 0 && found[i].fd >= 0) {
            (void)kbdd_op_queue_enable_notif(NULL, &found[i]);
        }
    }
    fprintf(stderr, "kbdd: device set changed, now tracking %zu interface(s)\n",
            found_count);
    if (found_count != 0 && ((k->config_mode != KBDD_CONFIG_NONE &&
        k->config_mode != KBDD_CONFIG_CUSTOM) || kbdd_config_has_colors(k) ||
        k->config_led_direction_set || k->config_report_rate_set ||
        k->config_opts_set || k->config_opts_direction || k->config_opts_win_lock ||
        k->config_keymap_set)) {
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

static void kbdd_yield_input_reports(void *kctx_ptr)
{
    kbdd_ctx_t *k = (kbdd_ctx_t *)kctx_ptr;
    if (k == NULL) {
        return;
    }
    for (size_t i = 0; i < k->count; i++) {
        if (k->devices[i].role != 1 || k->devices[i].fd < 0) {
            continue;
        }
        uint8_t buf[256];
        ssize_t r = kbdd_hidraw_read(&k->devices[i], buf, sizeof(buf));
        if (r > 0) {
            kbdd_publish_input_event(k, buf, (size_t)r);
        } else if (r < 0 &&
                   (errno == EBADF || errno == EIO ||
                    errno == ENODEV || errno == ENXIO)) {
            kbdd_hidraw_close(&k->devices[i]);
        }
    }
}

static long long kbdd_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

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

const char *kbdd_dev_err(int rc)
{
    switch (rc) {
    case KBDD_DEV_EINVAL: return "error: invalid request";
    case KBDD_DEV_EIO:    return "error: device I/O failed (is the keyboard connected and the udev rule installed?)";
    case KBDD_DEV_EPROTO: return "error: unexpected response from the device (layout check failed, nothing written)";
    case KBDD_DEV_ENOSAVE: return "error: this profile cannot be saved safely (custom LED data present, or effect 14/15)";
    case KBDD_DEV_EVERIFY: return "error: write not confirmed by read-back; original block restored";
    case KBDD_DEV_ENOCUSTOM: return "error: no custom LED layout is stored in this profile (run 'kbctl led custom ...' first)";
    default:              return "error: unknown";
    }
}


#define KBDD_OP_FLAG_ANIM 0x80u

static int kbdd_queue_busy(kbdd_ipc_message_t *resp)
{
    return kbdd_reply_text(resp, "error: device queue full, try again shortly");
}

int kbdd_handle_led(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                    kbdd_ipc_message_t *resp)
{
    const uint8_t *p = req->payload;
    const kbdd_hid_device_t *dev;
    size_t matrix, stride, count;
    uint8_t flags, mode, pos, r, g, b;

    if (!k->allow_led) {
        return kbdd_reply_text(resp,
            "error: LED control is disabled. Start kbdd with KBDD_ENABLE_LED=1");
    }
    if (p == NULL || req->payload_len != KBDD_LED_PAYLOAD_SIZE) {
        return kbdd_reply_text(resp, "error: bad LED payload");
    }
    flags = p[0]; mode = p[2]; pos = p[3]; r = p[4]; g = p[5]; b = p[6];

    k->led_anim = 0;
    if (mode == KBDD_LED_MODE_RELEASE) {
        kbdd_op_cancel_led();
        k->led_hold = 0;
        k->led_active = 0;
        return kbdd_reply_text(resp, "ok: LED streaming stopped");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    matrix = dev->model->matrix_size;
    stride = p[1] != 0 ? (size_t)p[1] : matrix;
    if (kbc_led_frame_page_count(stride) == 0) {
        return kbdd_reply_text(resp, "error: invalid stride (1..144)");
    }
    count = matrix < stride ? matrix : stride;

    if (mode == KBDD_LED_MODE_CLEAR) {
        memset(k->led_frame, 0, sizeof(k->led_frame));
        memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
    } else if (mode == KBDD_LED_MODE_SOLID) {
        memset(k->led_frame, 0, sizeof(k->led_frame));
        memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
        kbc_led_frame_fill(k->led_base_frame, stride, count, r, g, b);
    } else if (mode == KBDD_LED_MODE_POS) {
        if (pos >= count) {
            return kbdd_reply_text(resp, "error: position out of range");
        }
        if (!k->led_active || k->led_stride != stride) {
            memset(k->led_frame, 0, sizeof(k->led_frame));
            memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
        }
        kbc_led_frame_set(k->led_base_frame, stride, pos, r, g, b);
    } else {
        return kbdd_reply_text(resp, "error: unknown LED mode");
    }

    memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
    if (kbc_led_frame_apply_brightness(k->led_frame, stride, k->led_brightness) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid LED brightness");
    }
    /* Supersede any frame still in flight so it cannot overwrite this one. */
    kbdd_op_cancel_led();
    return kbdd_op_queue_frame_from_base(req, dev, k->led_frame, stride, count,
                                         (uint8_t)(flags & KBDD_LED_FLAG_HOLD)) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

int kbdd_handle_effect(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                       kbdd_ipc_message_t *resp)
{
    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 1) {
        return kbdd_reply_text(resp, "error: payload must be [mode]");
    }
    if (kbdd_control_dev(k) == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    k->led_hold = 0;
    k->led_active = 0;
    k->led_anim = 0;
    kbdd_op_cancel_led();
    return kbdd_op_queue_effect(req, kbdd_control_dev(k)) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

int kbdd_handle_profile(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                        kbdd_ipc_message_t *resp)
{
    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile switching is disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 1 || req->payload[0] < 1 || req->payload[0] > 4) {
        return kbdd_reply_text(resp, "error: payload must be [profile 1..4]");
    }
    if (kbdd_control_dev(k) == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    k->led_hold = 0;
    k->led_active = 0;
    kbdd_op_cancel_led();
    return kbdd_op_queue_switch(req, kbdd_control_dev(k)) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

int kbdd_handle_anim(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                            kbdd_ipc_message_t *resp)
{
    const uint8_t *p = req->payload;
    const kbdd_hid_device_t *dev;
    size_t matrix, stride, count;
    uint32_t period;

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
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    matrix = dev->model->matrix_size;
    stride = p[1] != 0 ? (size_t)p[1] : matrix;
    if (kbc_led_frame_page_count(stride) == 0) {
        return kbdd_reply_text(resp, "error: invalid stride (1..144)");
    }
    count = matrix < stride ? matrix : stride;
    period = ((uint32_t)p[2] << 8) | p[3];
    if (period == 0) {
        period = kbc_led_anim_default_period((kbc_led_anim)p[0]);
    }

    memset(k->led_base_frame, 0, sizeof(k->led_base_frame));
    if (kbc_led_anim_render((kbc_led_anim)p[0], 0, period, p[4], p[5], p[6],
                            k->led_base_frame, stride, count) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid animation period (200..60000 ms)");
    }
    memcpy(k->led_frame, k->led_base_frame, sizeof(k->led_frame));
    if (kbc_led_frame_apply_brightness(k->led_frame, stride, k->led_brightness) != KBC_OK) {
        return kbdd_reply_text(resp, "error: invalid LED brightness");
    }
    kbdd_op_cancel_led();
    {
        /* Persist the resolved default period, not the zero the client sent. */
        uint8_t norm[KBDD_ANIM_PAYLOAD_SIZE];
        kbdd_ipc_message_t normalized = *req;
        memcpy(norm, p, sizeof(norm));
        norm[2] = (uint8_t)(period >> 8);
        norm[3] = (uint8_t)(period & 0xFFu);
        normalized.payload = norm;
        normalized.payload_len = (uint32_t)sizeof(norm);
        return kbdd_op_queue_frame_from_base(&normalized, dev, k->led_frame,
                                             stride, count,
                                             (uint8_t)(KBDD_LED_FLAG_HOLD | KBDD_OP_FLAG_ANIM)) == 0
                   ? KBDD_HANDLER_DEFER
                   : kbdd_queue_busy(resp);
    }
}

int kbdd_handle_colors(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                              kbdd_ipc_message_t *resp)
{
    const uint8_t *p = req->payload;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (p == NULL || req->payload_len < 5 || p[1] < 1 || p[1] > 14 ||
        req->payload_len != 2u + 3u * p[1]) {
        return kbdd_reply_text(resp, "error: payload must be [effect, count 1..14, r,g,b * count]");
    }
    if (kbdd_control_dev(k) == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    k->led_hold = 0;
    k->led_active = 0;
    kbdd_op_cancel_led();
    return kbdd_op_queue_colors(req, kbdd_control_dev(k)) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

/* Flash-wear guard: 0 = allowed (and arms the cooldown), else ms to wait. */
static long long kbdd_save_cooldown_take(kbdd_ctx_t *k)
{
    long long now = kbdd_now_ms();

    if (now < k->save_next_ms) {
        return k->save_next_ms - now;
    }
    k->save_next_ms = now + KBDD_SAVE_COOLDOWN_MS;
    return 0;
}

static int kbdd_save_rate_limited(kbdd_ipc_message_t *resp, long long wait_ms)
{
    char buf[128];

    snprintf(buf, sizeof(buf),
             "error: save rate-limited to protect flash; try again in %lld s",
             (wait_ms + 999) / 1000);
    return kbdd_reply_text(resp, buf);
}

int kbdd_handle_save(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                     kbdd_ipc_message_t *resp)
{
    const kbdd_hid_device_t *dev;
    long long wait_ms;
    uint8_t was_hold;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    wait_ms = kbdd_save_cooldown_take(k);
    if (wait_ms != 0) {
        return kbdd_save_rate_limited(resp, wait_ms);
    }
    was_hold = (uint8_t)(k->led_hold ? 1 : 0);
    k->led_hold = 0;
    if (kbdd_op_queue_save(req, dev, was_hold) != 0) {
        k->save_next_ms = 0;   /* nothing was written: do not charge the cooldown */
        return kbdd_queue_busy(resp);
    }
    return KBDD_HANDLER_DEFER;
}

static int kbdd_handle_custom(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                              kbdd_ipc_message_t *resp, int clear)
{
    const kbdd_hid_device_t *dev;
    long long wait_ms;
    uint8_t was_hold, was_active, was_anim;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (clear ? req->payload_len != 0 : (req->payload == NULL || req->payload_len != 3u * KBC_CUSTOM_KEYS)) {
        return kbdd_reply_text(resp, clear ? "error: clear takes no payload"
                                           : "error: payload must be 70 x [r,g,b]");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    wait_ms = kbdd_save_cooldown_take(k);          /* same flash-wear guard as SAVE_BLOCK */
    if (wait_ms != 0) {
        return kbdd_save_rate_limited(resp, wait_ms);
    }
    was_hold = (uint8_t)(k->led_hold ? 1 : 0);
    was_active = (uint8_t)(k->led_active ? 1 : 0);
    was_anim = (uint8_t)(k->led_anim ? 1 : 0);
    k->led_hold = 0;
    k->led_active = 0;
    k->led_anim = 0;
    /* Invalidate frames queued before this save.  Without the epoch bump a
     * stale completion would re-arm streaming after we just stopped it. */
    kbdd_op_cancel_led();
    if ((clear ? kbdd_op_queue_clear_custom(req, dev, 0)
               : kbdd_op_queue_save_custom(req, dev, 0)) != 0) {
        k->led_hold = was_hold;
        k->led_active = was_active;
        k->led_anim = was_anim;
        k->save_next_ms = 0;
        return kbdd_queue_busy(resp);
    }
    return KBDD_HANDLER_DEFER;
}

int kbdd_handle_keymap(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                       kbdd_ipc_message_t *resp)
{
    const kbdd_hid_device_t *dev;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != KBDD_KEYMAP_PAYLOAD_SIZE) {
        return kbdd_reply_text(resp, "error: payload must be 3 x 70 HID codes");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room() || kbdd_op_queue_keymap(req, dev, 0) != 0) {
        return kbdd_queue_busy(resp);
    }
    return KBDD_HANDLER_DEFER;
}

int kbdd_handle_macro(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                      kbdd_ipc_message_t *resp)
{
    const kbdd_hid_device_t *dev;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != KBDD_MACRO_PAYLOAD_SIZE) {
        return kbdd_reply_text(resp, "error: payload must be 210 keymap + 1 key + 256 macro bytes");
    }
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room() || kbdd_op_queue_macro(req, dev, 0) != 0) {
        return kbdd_queue_busy(resp);
    }
    return KBDD_HANDLER_DEFER;
}

int kbdd_handle_save_custom(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                            kbdd_ipc_message_t *resp)
{
    return kbdd_handle_custom(k, req, resp, 0);
}

int kbdd_handle_clear_custom(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                             kbdd_ipc_message_t *resp)
{
    return kbdd_handle_custom(k, req, resp, 1);
}

int kbdd_handle_field(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                             kbdd_ipc_message_t *resp)
{
    int field, resume_led;
    const kbdd_hid_device_t *dev;
    const uint8_t *base = NULL;

    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != 2 || req->payload[0] < 1 || req->payload[0] > 5) {
        return kbdd_reply_text(resp, "error: payload must be [field 1..5, value]");
    }
    field = req->payload[0];
    dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    resume_led = (field == KBC_FIELD_BRIGHTNESS) ? k->led_hold : 0;
    k->led_hold = 0;
    if (field != KBC_FIELD_BRIGHTNESS) {
        k->led_active = 0;
        kbdd_op_cancel_led();
    }
    if (field == KBC_FIELD_BRIGHTNESS && k->led_active && k->led_stride != 0) {
        base = k->led_base_frame;
    }
    return kbdd_op_queue_field(req, dev, resume_led ? k->led_stride : 0,
                               resume_led ? (uint8_t)resume_led : 0,
                               base) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

int kbdd_handle_opts(kbdd_ctx_t *k, const kbdd_ipc_message_t *req,
                     kbdd_ipc_message_t *resp)
{
    if (!k->allow_profile_write) {
        return kbdd_reply_text(resp,
            "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
    }
    if (req->payload == NULL || req->payload_len != KBDD_OPTS_PAYLOAD_SIZE ||
        req->payload[0] > 1 || req->payload[1] > 1) {
        return kbdd_reply_text(resp, "error: payload must be [direction 0/1, win_lock 0/1]");
    }
    if (kbdd_control_dev(k) == NULL) {
        return kbdd_reply_text(resp, "error: no keyboard control interface available");
    }
    if (!kbdd_op_has_room()) {
        return kbdd_queue_busy(resp);
    }
    k->led_hold = 0;
    k->led_active = 0;
    kbdd_op_cancel_led();
    return kbdd_op_queue_opts(req, kbdd_control_dev(k)) == 0
               ? KBDD_HANDLER_DEFER
               : kbdd_queue_busy(resp);
}

void kbdd_restore_tick(kbdd_ctx_t *k)
{
    if (k == NULL || !k->restore_pending ||
        kbdd_now_ms() < k->restore_next_ms) {
        return;
    }
    if ((k->config_mode == KBDD_CONFIG_NONE ||
         k->config_mode == KBDD_CONFIG_CUSTOM) && !kbdd_config_has_colors(k) &&
        !k->config_led_direction_set && !k->config_report_rate_set &&
        !k->config_opts_set &&
        !(k->config_opts_direction || k->config_opts_win_lock) &&
        !k->config_keymap_set) {
        k->restore_pending = 0;
        return;
    }
    const kbdd_hid_device_t *dev = kbdd_control_dev(k);
    if (dev == NULL) {
        return;
    }

    if (kbdd_op_pending()) {
        return; /* retry once the worker is idle */
    }
    if (kbdd_op_queue_restore_config(k, dev) == 0) {
        k->restore_next_ms = kbdd_now_ms() + 10000;
    }
}

static void kbdd_led_tick(kbdd_ctx_t *k)
{
    const kbdd_hid_device_t *dev;
    long long now;

    if (k->led_anim && !k->led_active) k->led_anim = 0;   /* another mode took over */
    if (!k->led_hold) return;
    now = kbdd_now_ms();
    if (now < k->led_next_ms) return;
    if (kbdd_op_pending()) {
        return; /* device busy with another request; retry on the next loop */
    }
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
    /* Stream through the worker so a slow write never stalls other
     * clients; the next frame is queued only once this one completes. */
    if (kbdd_op_queue_frame_from_base(NULL, dev, k->led_frame, k->led_stride,
                                      k->led_anim ? k->led_anim_count : k->led_stride,
                                      KBDD_LED_FLAG_HOLD) == 0) {
        k->led_next_ms = now + 40;
    }
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

int kbdd_ipc_handle(const kbdd_ipc_message_t *req, kbdd_ipc_message_t *resp,
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
            const char *msg = "kbdd " KBCORE_VERSION_STRING;
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
        case KBDD_IPC_OP_STATUS: {
            char buf[96];
            snprintf(buf, sizeof(buf), "kbdd " KBCORE_VERSION_STRING ": running, %zu hid interface(s)",
                     kctx != NULL ? kctx->count : (size_t)0);
            return kbdd_reply_text(resp, buf);
        }
        case KBDD_IPC_OP_READ_PROFILE:
        {
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);

            if (req->payload == NULL || req->payload_len != 1 ||
                req->payload[0] < 1 || req->payload[0] > 4) {
                return kbdd_reply_text(resp,
                    "error: payload must be a hardware profile from 1 to 4");
            }
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            return kbdd_op_queue_read_block(req, dev) == 0
                       ? KBDD_HANDLER_DEFER
                       : kbdd_queue_busy(resp);
        }
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
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            return kbdd_op_queue_enable_notif(req, dev) == 0
                       ? KBDD_HANDLER_DEFER
                       : kbdd_queue_busy(resp);
        }
        case KBDD_IPC_OP_WATCH:
            return kbdd_reply_text(resp,
                "watching keyboard system events (Ctrl+C to stop)");
        case KBDD_IPC_OP_READ_ACTIVE: {
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            return kbdd_op_queue_read_active(req, dev) == 0
                       ? KBDD_HANDLER_DEFER
                       : kbdd_queue_busy(resp);
        }
        case KBDD_IPC_OP_READ_BLOCK: {
            const kbdd_hid_device_t *dev = kbdd_control_dev(kctx);
            if (req->payload == NULL || req->payload_len != 1) {
                return kbdd_reply_text(resp, "error: payload must be [profile]");
            }
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            return kbdd_op_queue_read_block(req, dev) == 0
                       ? KBDD_HANDLER_DEFER
                       : kbdd_queue_busy(resp);
        }
        case KBDD_IPC_OP_RESTORE_PROFILE: {
            const kbdd_hid_device_t *dev;

            if (!kctx->allow_profile_write) {
                return kbdd_reply_text(resp,
                    "error: profile writes are disabled. Start kbdd with KBDD_ENABLE_PROFILE_WRITE=1");
            }
            if (req->payload == NULL || req->payload_len != 265 ||
                req->payload[0] < 1 || req->payload[0] > 4) {
                return kbdd_reply_text(resp,
                    "error: payload must be [profile 1..4, 264-byte read block]");
            }
            dev = kbdd_control_dev(kctx);
            if (dev == NULL) {
                return kbdd_reply_text(resp, "error: no keyboard control interface available");
            }
            kctx->led_hold = 0;
            kctx->led_active = 0;
            kctx->led_anim = 0;
            kbdd_op_cancel_led();
            return kbdd_op_queue_restore(req, dev) == 0
                       ? KBDD_HANDLER_DEFER
                       : kbdd_queue_busy(resp);
        }
        case KBDD_IPC_OP_LED_SET:
            return kbdd_handle_led(kctx, req, resp);
        case KBDD_IPC_OP_SET_EFFECT:
            return kbdd_handle_effect(kctx, req, resp);
        case KBDD_IPC_OP_SET_PROFILE:
            return kbdd_handle_profile(kctx, req, resp);
        case KBDD_IPC_OP_LED_ANIM:
            return kbdd_handle_anim(kctx, req, resp);
        case KBDD_IPC_OP_SET_COLORS:
            return kbdd_handle_colors(kctx, req, resp);
        case KBDD_IPC_OP_SAVE_BLOCK:
            return kbdd_handle_save(kctx, req, resp);
        case KBDD_IPC_OP_SAVE_CUSTOM:
            return kbdd_handle_save_custom(kctx, req, resp);
        case KBDD_IPC_OP_CLEAR_CUSTOM:
            return kbdd_handle_clear_custom(kctx, req, resp);
        case KBDD_IPC_OP_SET_KEYMAP:
            return kbdd_handle_keymap(kctx, req, resp);
        case KBDD_IPC_OP_SET_MACRO:
            return kbdd_handle_macro(kctx, req, resp);
        case KBDD_IPC_OP_SET_FIELD:
            return kbdd_handle_field(kctx, req, resp);
        case KBDD_IPC_OP_SET_OPTS:
            return kbdd_handle_opts(kctx, req, resp);
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

static void kbdd_op_client_closed_cb(int fd, void *ctx)
{
    kbdd_op_client_closed((kbdd_ctx_t *)ctx, fd);
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
    /* Notifications will be enabled via the async path after context init */

    /* uinput (optional, mockable) */
    /* Off by default: nothing uses the virtual keyboard yet, and creating it
     * needs /dev/uinput access. Opt in with KBDD_ENABLE_UINPUT=1. */
    kbdd_uinput_t *uinput = NULL;
    const char *want_uinput = getenv("KBDD_ENABLE_UINPUT");
    if (want_uinput != NULL && strcmp(want_uinput, "1") == 0) {
        uinput = kbdd_uinput_open(NULL);
        if (uinput != NULL) {
            kbdd_uinput_create_keyboard(uinput);
            fprintf(stderr, "kbdd: uinput virtual keyboard enabled (keystroke injection active)\n");
        } else {
            fprintf(stderr, "kbdd: KBDD_ENABLE_UINPUT=1 but /dev/uinput unavailable\n");
        }
    }

    /* ipc server (required for client communication) */
    kbdd_ipc_server_t *ipc = kbdd_ipc_server_create();
    if (ipc == NULL) {
        fprintf(stderr,
                "kbdd: could not create IPC socket "
                "(is XDG_RUNTIME_DIR set and valid?)\n");
        kbdd_hidraw_free(devices, device_count);
        if (uinput != NULL) {
            kbdd_uinput_close(uinput);
        }
        if (lock_fd >= 0) {
            flock(lock_fd, LOCK_UN);
            close(lock_fd);
        }
        return 1;
    }

    kbdd_ctx_t kctx;
    memset(&kctx, 0, sizeof(kctx));
    kctx.led_brightness = KBC_BRIGHTNESS_RAW_MAX;
    kctx.devices = devices;
    kctx.count = device_count;
    for (size_t i = 0; i < device_count; i++) {
        devices[i].kctx = &kctx;
        devices[i].yield = kbdd_yield_input_reports;
    }
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
    if (kctx.count != 0 && (kctx.config_mode != KBDD_CONFIG_CUSTOM ||
        kbdd_config_has_colors(&kctx) || kctx.config_led_direction_set ||
        kctx.config_report_rate_set || kctx.config_opts_set ||
        kctx.config_opts_direction || kctx.config_opts_win_lock ||
        kctx.config_keymap_set)) {
        kctx.restore_pending = 1;
        kctx.restore_next_ms = kbdd_now_ms() + 300;
    }
    kbdd_ipc_server_set_close_cb(kbdd_op_client_closed_cb, &kctx);

    /* Enable notifications on all control interfaces via async path */
    for (size_t i = 0; i < kctx.count; i++) {
        if (kctx.devices[i].role == 0 && kctx.devices[i].fd >= 0) {
            (void)kbdd_op_queue_enable_notif(NULL, &kctx.devices[i]);
        }
    }

    /* main loop */
    long long next_device_scan_ms = kbdd_now_ms();
    long long device_scan_interval_ms = 500;
    while (!kbdd_stop_flag) {
        {
            int ipc_timeout = kctx.led_hold ? 20 : 50;
            if (kbdd_op_pending()) {
                ipc_timeout = 10; /* a worker result may be waiting */
            }
            kbdd_ipc_server_poll(ipc, ipc_timeout, kbdd_ipc_handle, &kctx);
        }
        kbdd_op_poll(&kctx);

        if (kbdd_now_ms() >= next_device_scan_ms) {
            int changed = kbdd_refresh_devices(&kctx);
            if (changed) {
                device_scan_interval_ms = 500;
            } else {
                /* Check whether any device fd is still closed. If so,
                 * back off the scan interval to avoid log spam when
                 * open() keeps failing after a successful enumerate. */
                int any_closed = 0;
                for (size_t i = 0; i < kctx.count; i++) {
                    if (kctx.devices[i].fd < 0) {
                        any_closed = 1;
                        break;
                    }
                }
                if (any_closed && device_scan_interval_ms < 30000) {
                    device_scan_interval_ms *= 2;
                    if (device_scan_interval_ms > 30000)
                        device_scan_interval_ms = 30000;
                } else if (!any_closed) {
                    device_scan_interval_ms = 500;
                }
            }
            next_device_scan_ms = kbdd_now_ms() + device_scan_interval_ms;
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
    kbdd_op_shutdown();
    kbdd_ipc_server_destroy(ipc);
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