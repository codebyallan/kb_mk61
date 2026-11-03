#include "kbdd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Device operations can block for hundreds of milliseconds (feature reports
 * need spec-mandated delays). Running them in a short-lived child keeps the
 * daemon loop responsive so other IPC clients are not stalled behind a slow
 * keyboard. A single child is in flight at a time, preserving operation
 * ordering, and the child inherits the current hidraw descriptors at fork
 * time so hotplug/rescan in the parent cannot leave a stale descriptor. */

#define KBDD_MAX_PENDING 16
#define KBDD_OP_FLAG_ANIM 0x80u
/* A wedged hidraw ioctl must not stall the daemon forever. */
#define KBDD_OP_TIMEOUT_MS 10000

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

typedef enum {
    OP_READ_ACTIVE = 1,
    OP_READ_BLOCK,
    OP_SWITCH_PROFILE,
    OP_SET_EFFECT,
    OP_SET_FIELD,
    OP_SET_COLORS,
    OP_SAVE_PROFILE,
    OP_RESTORE_PROFILE,
    OP_ENABLE_NOTIF,
    OP_SEND_FRAME,
    OP_RESTORE_CONFIG
} kbdd_op_type_t;

typedef struct kbdd_op_node {
    struct kbdd_op_node *next;
    kbdd_op_type_t type;
    int client_fd;
    uint16_t opcode;
    uint16_t request_id;
    uint8_t flags;              /* LED hold bit / save-hold resume bit */
    uint8_t mode;               /* LED mode or field */
    size_t stride;
    size_t count;
    uint64_t epoch;             /* LED generation captured at submit */
    kbdd_hid_device_t dev;      /* snapshot; refreshed from g_ctx before fork */
    int has_dev;
    uint8_t *payload;           /* request payload copy */
    size_t payload_len;
    uint8_t *data;              /* frame for SEND_FRAME / brightness refresh */
    size_t data_len;
    int discard;                /* client closed; run but do not reply */
} kbdd_op_node_t;

typedef struct {
    int rc;
    uint8_t profile;
    int changed;
    uint8_t out[KBC_FEATURE_REPORT_SIZE];
    char diag[256];
} kbdd_op_result_t;

static kbdd_ctx_t *g_ctx;
static kbdd_op_node_t *g_head;
static kbdd_op_node_t *g_tail;
static size_t g_queue_len;
static kbdd_op_node_t *g_current;
static pid_t g_child_pid = -1;
static int g_pipe_fd = -1;
static kbdd_op_result_t g_result;
static size_t g_result_size;
static long long g_op_start_ms;
static uint64_t g_led_epoch;

static void op_free(kbdd_op_node_t *op)
{
    if (op == NULL) {
        return;
    }
    free(op->payload);
    free(op->data);
    free(op);
}

static void op_enqueue(kbdd_op_node_t *op)
{
    if (op == NULL) {
        return;
    }
    op->next = NULL;
    if (g_tail == NULL) {
        g_head = op;
        g_tail = op;
    } else {
        g_tail->next = op;
        g_tail = op;
    }
    g_queue_len++;
}

static kbdd_op_node_t *op_dequeue(void)
{
    kbdd_op_node_t *head = g_head;
    if (head == NULL) {
        return NULL;
    }
    g_head = head->next;
    if (g_head == NULL) {
        g_tail = NULL;
    }
    g_queue_len--;
    head->next = NULL;
    return head;
}

static kbdd_op_node_t *op_new(kbdd_op_type_t type, const kbdd_ipc_message_t *req)
{
    kbdd_op_node_t *node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->type = type;
    node->client_fd = req != NULL ? req->client_fd : -1;
    node->opcode = req != NULL ? req->opcode : 0;
    node->request_id = req != NULL ? req->request_id : 0;
    node->epoch = g_led_epoch;
    return node;
}

static void op_attach_dev(kbdd_op_node_t *op, const kbdd_hid_device_t *dev)
{
    if (op == NULL || dev == NULL) {
        return;
    }
    op->dev = *dev;
    op->has_dev = 1;
}

static void op_set_payload(kbdd_op_node_t *op, const uint8_t *payload, size_t len)
{
    if (op == NULL) {
        return;
    }
    free(op->payload);
    op->payload = NULL;
    op->payload_len = 0;
    if (payload == NULL || len == 0) {
        return;
    }
    op->payload = malloc(len);
    if (op->payload == NULL) {
        return;
    }
    memcpy(op->payload, payload, len);
    op->payload_len = len;
}

static void op_set_data(kbdd_op_node_t *op, const uint8_t *data, size_t len)
{
    if (op == NULL) {
        return;
    }
    free(op->data);
    op->data = NULL;
    op->data_len = 0;
    if (data == NULL || len == 0) {
        return;
    }
    op->data = malloc(len);
    if (op->data == NULL) {
        return;
    }
    memcpy(op->data, data, len);
    op->data_len = len;
}

/* Refresh the device snapshot from the parent's current table. Returns 0 when
 * the device is still present (or when no table is available yet). */
static int op_refresh_dev(kbdd_op_node_t *op)
{
    if (op == NULL || !op->has_dev) {
        return -1;
    }
    if (g_ctx == NULL) {
        return 0;
    }
    for (size_t i = 0; i < g_ctx->count; i++) {
        const kbdd_hid_device_t *d = &g_ctx->devices[i];
        if (d->fd >= 0 && strcmp(d->path, op->dev.path) == 0 &&
            d->vid == op->dev.vid && d->pid == op->dev.pid &&
            d->usage_page == op->dev.usage_page &&
            d->usage == op->dev.usage && d->role == op->dev.role) {
            op->dev = *d;
            return 0;
        }
    }
    return -1;
}

static void op_child_exec(const kbdd_op_node_t *op, int out_fd)
{
    kbdd_hid_device_t dev = op->dev;
    kbdd_dev_io_t io;
    kbdd_op_result_t result;
    const char *diag;

    memset(&result, 0, sizeof(result));
    kbdd_dev_clear_diag();

    /* All input-report processing belongs to the parent now. */
    dev.kctx = NULL;
    dev.yield = NULL;
    kbdd_dev_io_init_hidraw(&io, &dev);

    switch (op->type) {
    case OP_READ_ACTIVE:
        result.rc = kbdd_dev_query(&io, KBC_OP_READ_ACTIVE_PROFILE, 0, result.out);
        break;
    case OP_READ_BLOCK:
        if (op->payload_len == 1 && op->payload != NULL) {
            result.rc = kbdd_dev_query(&io, KBC_OP_READ_PROFILE_BLOCK,
                                       op->payload[0], result.out);
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_SWITCH_PROFILE:
        if (op->payload_len == 1 && op->payload != NULL) {
            int changed = 0;
            result.rc = kbdd_dev_switch_profile(&io, op->payload[0], &changed);
            result.changed = changed;
            if (result.rc == KBDD_DEV_OK) {
                result.profile = op->payload[0];
            }
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_SET_EFFECT:
        if (op->payload_len == 1 && op->payload != NULL) {
            result.rc = kbdd_dev_set_effect(&io, op->payload[0], &result.profile);
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_SET_FIELD:
        if (op->payload_len == 2 && op->payload != NULL) {
            int field = op->payload[0];
            result.rc = kbdd_dev_set_field(&io, field, op->payload[1], &result.profile);
            if (result.rc == KBDD_DEV_OK && op->stride != 0 && op->data != NULL &&
                field == KBC_FIELD_BRIGHTNESS) {
                uint8_t frame[KBC_LED_BUFFER_SIZE];
                memcpy(frame, op->data, sizeof(frame));
                if (kbc_led_frame_apply_brightness(frame, op->stride, op->payload[1]) != KBC_OK ||
                    kbdd_dev_send_frame(&io, frame, op->stride) != KBDD_DEV_OK) {
                    result.rc = KBDD_DEV_EIO;
                }
            }
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_SET_COLORS: {
        kbdd_color_edit_t ce;
        memset(&ce, 0, sizeof(ce));
        if (op->payload == NULL || op->payload_len < 5 ||
            op->payload[1] < 1 || op->payload[1] > 14 ||
            op->payload_len != 2u + 3u * op->payload[1]) {
            result.rc = KBDD_DEV_EINVAL;
            break;
        }
        ce.effect = op->payload[0];
        ce.count = op->payload[1];
        for (size_t j = 0; j < ce.count; j++) {
            memcpy(ce.rgb[j], op->payload + 2 + 3 * j, 3);
        }
        result.rc = kbdd_dev_set_colors(&io, &ce, &result.profile);
        break;
    }
    case OP_SAVE_PROFILE:
        result.rc = kbdd_dev_save_profile(&io, &result.profile);
        break;
    case OP_RESTORE_PROFILE:
        if (op->payload_len == 265 && op->payload != NULL &&
            op->payload[0] >= 1 && op->payload[0] <= 4) {
            result.rc = kbdd_dev_restore_profile(&io, op->payload[0], op->payload + 1);
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_ENABLE_NOTIF:
        result.rc = kbdd_dev_enable_notifications(&io);
        break;
    case OP_SEND_FRAME:
        if (op->data != NULL && op->data_len == KBC_LED_BUFFER_SIZE && op->stride > 0) {
            uint8_t frame[KBC_LED_BUFFER_SIZE];
            memcpy(frame, op->data, sizeof(frame));
            result.rc = kbdd_dev_send_frame(&io, frame, op->stride);
        } else {
            result.rc = KBDD_DEV_EINVAL;
        }
        break;
    case OP_RESTORE_CONFIG: {
        int allow_pw = (op->flags & 0x01u) != 0;
        int allow_led = (op->flags & 0x02u) != 0;
        uint8_t cfg_mode = op->mode;
        uint8_t brightness = op->payload != NULL && op->payload_len >= 1 ? op->payload[0] : 0;
        uint8_t effect = op->payload != NULL && op->payload_len >= 2 ? op->payload[1] : 0;
        uint8_t speed = op->payload != NULL && op->payload_len >= 3 ? op->payload[2] : 0;

        result.rc = KBDD_DEV_OK;
        if (allow_pw) {
            result.rc = kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, brightness, NULL);
            if (result.rc == KBDD_DEV_OK && cfg_mode == KBDD_CONFIG_EFFECT) {
                result.rc = kbdd_dev_set_field(&io, KBC_FIELD_MODE, effect, NULL);
                if (result.rc == KBDD_DEV_OK) {
                    result.rc = kbdd_dev_set_field(&io, KBC_FIELD_SPEED, speed, NULL);
                }
            }
        }
        if (result.rc == KBDD_DEV_OK && cfg_mode != KBDD_CONFIG_EFFECT && allow_led) {
            if (op->data != NULL && op->data_len == KBC_LED_BUFFER_SIZE && op->stride > 0) {
                uint8_t frame[KBC_LED_BUFFER_SIZE];
                memcpy(frame, op->data, sizeof(frame));
                result.rc = kbdd_dev_send_frame(&io, frame, op->stride);
            } else {
                result.rc = KBDD_DEV_EINVAL;
            }
        }
        break;
    }
    default:
        result.rc = KBDD_DEV_EINVAL;
        break;
    }

    diag = kbdd_dev_last_diag();
    if (diag != NULL) {
        snprintf(result.diag, sizeof(result.diag), "%s", diag);
    }

    const uint8_t *ptr = (const uint8_t *)&result;
    size_t left = sizeof(result);
    while (left > 0) {
        ssize_t n = write(out_fd, ptr, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        ptr += n;
        left -= (size_t)n;
    }
}

/* The child only needs the pipe and the hidraw descriptor. Closing the rest
 * (IPC sockets, lock, other devices) prevents a slow worker from holding the
 * daemon's listening socket or single-instance lock alive. */
static void op_child_close_fds(int keep_a, int keep_b)
{
    DIR *dir = opendir("/proc/self/fd");
    if (dir != NULL) {
        struct dirent *de;
        int dfd = dirfd(dir);
        while ((de = readdir(dir)) != NULL) {
            char *end = NULL;
            long fd = strtol(de->d_name, &end, 10);
            if (end == de->d_name || *end != '\0') {
                continue;
            }
            if (fd < 3 || fd == dfd || fd == keep_a || fd == keep_b) {
                continue;
            }
            close((int)fd);
        }
        closedir(dir);
        return;
    }
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 4096) {
        max = 4096;
    }
    for (int fd = 3; fd < max; fd++) {
        if (fd == keep_a || fd == keep_b) {
            continue;
        }
        close(fd);
    }
}

static void op_reply(kbdd_ctx_t *kctx, const kbdd_op_node_t *op,
                     const void *data, uint32_t len)
{
    if (kctx == NULL || kctx->ipc == NULL || op == NULL ||
        op->client_fd < 0 || op->discard) {
        return;
    }
    kbdd_ipc_server_send(kctx->ipc, op->client_fd, op->opcode,
                         op->request_id, (const uint8_t *)data, len);
}

static void op_reply_text(kbdd_ctx_t *kctx, const kbdd_op_node_t *op,
                          const char *msg)
{
    if (msg == NULL) {
        return;
    }
    op_reply(kctx, op, msg, (uint32_t)strlen(msg) + 1);
}

static void op_send_error(kbdd_ctx_t *kctx, const kbdd_op_node_t *op,
                          const kbdd_op_result_t *r)
{
    char buf[512];

    if (r != NULL && (r->rc == KBDD_DEV_EVERIFY || r->rc == KBDD_DEV_EPROTO) &&
        r->diag[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s [%s]", kbdd_dev_err(r->rc), r->diag);
    } else {
        snprintf(buf, sizeof(buf), "%s", kbdd_dev_err(r != NULL ? r->rc : KBDD_DEV_EIO));
    }
    op_reply_text(kctx, op, buf);
}

static void op_complete(kbdd_ctx_t *kctx, kbdd_op_node_t *op,
                        const kbdd_op_result_t *r)
{
    if (op == NULL) {
        return;
    }
    if (kctx == NULL) {
        op_free(op);
        return;
    }
    /* op->discard only suppresses the reply (see op_reply): the device write
     * already happened, so LED/config bookkeeping must still be updated. */

    switch (op->type) {
    case OP_READ_ACTIVE:
        if (r->rc != KBDD_DEV_OK) {
            op_send_error(kctx, op, r);
        } else if (r->out[1] == KBC_OP_READ_ACTIVE_PROFILE &&
                   r->out[2] >= 1 && r->out[2] <= 4) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "active profile: %u", (unsigned)r->out[2]);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
        } else {
            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                             "unexpected reply; first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                             r->out[0], r->out[1], r->out[2], r->out[3],
                             r->out[4], r->out[5], r->out[6], r->out[7]);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
        }
        break;

    case OP_READ_BLOCK:
        if (r->rc == KBDD_DEV_OK) {
            op_reply(kctx, op, r->out, (uint32_t)sizeof(r->out));
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_SWITCH_PROFILE:
        if (r->rc == KBDD_DEV_OK) {
            char buf[128];
            int n = snprintf(buf, sizeof(buf),
                             r->changed ? "ok: profile %u is now active (confirmed by 0x81)"
                                        : "ok: profile %u was already active",
                             (unsigned)op->payload[0]);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
        } else if (r->rc == KBDD_DEV_EVERIFY) {
            op_reply_text(kctx, op,
                "error: the keyboard did not confirm the profile switch; previous profile requested back");
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_SET_EFFECT:
        if (r->rc == KBDD_DEV_OK) {
            const char *name = kbc_led_effect_name(op->payload[0]);
            char buf[192];
            int n = snprintf(buf, sizeof(buf),
                             "ok: effect %s (%u) set on profile %u, verified by read-back",
                             name != NULL ? name : "?",
                             (unsigned)op->payload[0], (unsigned)r->profile);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
            kctx->config_mode = KBDD_CONFIG_EFFECT;
            kctx->config_effect = op->payload[0];
            (void)kbdd_save_config(kctx);
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_SET_FIELD: {
        int field = op->payload != NULL && op->payload_len == 2 ? op->payload[0] : 0;
        if (r->rc == KBDD_DEV_OK) {
            static const char *const names[] = {
                "?", "effect", "brightness", "speed", "direction", "report rate"
            };
            char buf[192];
            int n = snprintf(buf, sizeof(buf),
                             "ok: %s set to %u on profile %u, verified by read-back",
                             (field >= 1 && field <= 5) ? names[field] : "field",
                             (unsigned)op->payload[1], (unsigned)r->profile);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
            if (field == KBC_FIELD_BRIGHTNESS) {
                kctx->led_brightness = op->payload[1];
                kctx->config_brightness = op->payload[1];
                if (op->stride != 0) {
                    memcpy(kctx->led_frame, kctx->led_base_frame,
                           sizeof(kctx->led_frame));
                    if (kbc_led_frame_apply_brightness(kctx->led_frame, op->stride,
                                                       op->payload[1]) != KBC_OK) {
                        kctx->led_active = 0;
                    } else {
                        kctx->led_stride = op->stride;
                        kctx->led_next_ms = now_ms() + 40;
                        kctx->led_hold = (op->flags & 0x01u) ? 1 : 0;
                    }
                }
            } else if (field == KBC_FIELD_SPEED) {
                kctx->config_speed[kctx->config_effect] = op->payload[1];
            } else if (field == KBC_FIELD_MODE) {
                kctx->config_mode = KBDD_CONFIG_EFFECT;
                kctx->config_effect = op->payload[1];
            }
            (void)kbdd_save_config(kctx);
        } else {
            if (field == KBC_FIELD_BRIGHTNESS && op->stride != 0) {
                kctx->led_active = 0;
                op_reply_text(kctx, op,
                    "error: brightness changed, but solid LED frame could not be refreshed");
            } else {
                op_send_error(kctx, op, r);
            }
        }
        break;
    }

    case OP_SET_COLORS:
        if (r->rc == KBDD_DEV_OK) {
            const char *name = kbc_led_effect_name(op->payload[0]);
            char buf[192];
            int n = snprintf(buf, sizeof(buf),
                             "ok: %s colors set on profile %u, verified by read-back (RAM only; use --save or 'led save')",
                             name != NULL ? name : "effect", (unsigned)r->profile);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_SAVE_PROFILE:
        if ((op->flags & 0x01u) != 0 && kctx->led_active) {
            kctx->led_next_ms = now_ms() + 40;
            kctx->led_hold = 1;
        }
        if (r->rc == KBDD_DEV_OK) {
            char buf[192];
            int n = snprintf(buf, sizeof(buf),
                             "ok: profile %u committed to flash (replug the keyboard to confirm it persists)",
                             (unsigned)r->profile);
            if (n >= 0) {
                op_reply(kctx, op, buf, (uint32_t)n + 1);
            }
        } else if (r->rc == KBDD_DEV_EVERIFY) {
            op_reply_text(kctx, op,
                "error: save not confirmed by the keyboard; nothing was committed or the block was restored");
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_RESTORE_PROFILE:
        if (r->rc == KBDD_DEV_OK) {
            op_reply_text(kctx, op,
                "ok: profile restored and verified by read-back (RAM only; use 'led save' for the active profile)");
        } else {
            op_send_error(kctx, op, r);
        }
        break;

    case OP_ENABLE_NOTIF:
        if (r->rc != KBDD_DEV_OK) {
            if (op->client_fd >= 0) {
                op_send_error(kctx, op, r);
            } else {
                fprintf(stderr,
                        "kbdd: could not enable keyboard notifications on %s\n",
                        op->dev.path);
            }
        } else if (op->client_fd >= 0) {
            op_reply_text(kctx, op, "notifications enabled");
        }
        break;

    case OP_SEND_FRAME: {
        long long now = now_ms();
        int fresh = op->epoch == g_led_epoch;

        if (r->rc == KBDD_DEV_OK) {
            if ((op->flags & KBDD_OP_FLAG_ANIM) != 0 &&
                op->payload != NULL && op->payload_len == KBDD_ANIM_PAYLOAD_SIZE) {
                static const char *const names[] = {"?", "breath", "cycle", "rainbow"};
                uint8_t kind = op->payload[0];
                uint32_t period = ((uint32_t)op->payload[2] << 8) | op->payload[3];
                size_t count = op->count != 0 ? op->count : op->stride;
                char buf[192];
                int n = snprintf(buf, sizeof(buf),
                                 "ok: %s animation started (25 fps, period %u ms); kbctl led release to stop",
                                 (kind >= 1 && kind <= 3) ? names[kind] : "animation",
                                 (unsigned)period);
                if (n >= 0) {
                    op_reply(kctx, op, buf, (uint32_t)n + 1);
                }
                if (fresh) {
                    kctx->led_anim = kind;
                    kctx->led_anim_period = period;
                    memcpy(kctx->led_anim_rgb, op->payload + 4, sizeof(kctx->led_anim_rgb));
                    kctx->led_anim_count = count;
                    kctx->led_anim_start_ms = now;
                    kctx->config_mode = KBDD_CONFIG_ANIMATION;
                    kctx->config_anim = kind;
                    kctx->config_anim_period = period;
                    memcpy(kctx->config_anim_rgb, op->payload + 4,
                           sizeof(kctx->config_anim_rgb));
                    kctx->config_stride = op->stride;
                    (void)kbdd_save_config(kctx);
                }
            } else if (op->client_fd >= 0) {
                if ((op->flags & KBDD_LED_FLAG_HOLD) != 0) {
                    op_reply_text(kctx, op,
                        "ok: LED frame sent, streaming every 40 ms (kbctl led release to stop)");
                } else {
                    op_reply_text(kctx, op, "ok: LED frame sent once");
                }
            }

            if (fresh) {
                kctx->led_stride = op->stride;
                kctx->led_active = 1;
                kctx->led_fail = 0;
                if (op->client_fd >= 0) {
                    /* Internal (streaming) frames already scheduled the next
                     * tick when they were queued; keep the 25 fps cadence. */
                    kctx->led_next_ms = now + 40;
                }
                kctx->led_hold = (op->flags & KBDD_LED_FLAG_HOLD) ? 1 : 0;
                /* Animation payloads share the 7-byte LED layout but are not
                 * LED commands; their config was recorded above. */
                if ((op->flags & KBDD_OP_FLAG_ANIM) == 0 &&
                    op->payload != NULL && op->payload_len == KBDD_LED_PAYLOAD_SIZE) {
                    uint8_t mode = op->payload[2];
                    uint8_t *pp = op->payload;
                    if (mode == KBDD_LED_MODE_SOLID) {
                        kctx->config_mode = KBDD_CONFIG_SOLID;
                        memcpy(kctx->config_solid, pp + 4, 3);
                        memset(kctx->config_positions, 0,
                               sizeof(kctx->config_positions));
                    } else if (mode == KBDD_LED_MODE_CLEAR) {
                        kctx->config_mode = KBDD_CONFIG_NONE;
                        memset(kctx->config_solid, 0, sizeof(kctx->config_solid));
                        memset(kctx->config_positions, 0,
                               sizeof(kctx->config_positions));
                    } else if (mode == KBDD_LED_MODE_POS) {
                        size_t pos = pp[3];
                        if (kctx->config_mode != KBDD_CONFIG_POSITIONS ||
                            kctx->config_stride != op->stride) {
                            memset(kctx->config_positions, 0,
                                   sizeof(kctx->config_positions));
                        }
                        kctx->config_mode = KBDD_CONFIG_POSITIONS;
                        if (pos < KBC_MAX_MATRIX_SIZE) {
                            memcpy(kctx->config_positions[pos], pp + 4, 3);
                        }
                    }
                    (void)kbdd_save_config(kctx);
                }
                kctx->config_stride = op->stride;
            }
        } else {
            if (fresh) {
                if (op->client_fd < 0) {
                    /* Streamed frames keep retrying, matching the previous
                     * "stop after five consecutive I/O errors" behaviour. */
                    if (++kctx->led_fail >= 5) {
                        kctx->led_hold = 0;
                        kctx->led_anim = 0;
                        fprintf(stderr, "kbdd: LED streaming stopped after repeated I/O errors\n");
                    } else {
                        kctx->led_next_ms = now + 40;
                    }
                } else {
                    kctx->led_hold = 0;
                    kctx->led_anim = 0;
                }
            }
            op_send_error(kctx, op, r);
        }
        break;
    }

    case OP_RESTORE_CONFIG: {
        long long now = now_ms();
        if (r->rc == KBDD_DEV_OK) {
            kctx->restore_pending = 0;
            kctx->restore_attempts = 0;
            fprintf(stderr, "kbdd: restored saved LED configuration\n");

            if (op->epoch == g_led_epoch) {
                uint8_t cfg_mode = op->mode;
                int allow_led = (op->flags & 0x02u) != 0;
                if (cfg_mode == KBDD_CONFIG_EFFECT) {
                    kctx->led_hold = 0;
                    kctx->led_active = 0;
                } else if (allow_led && op->data != NULL) {
                    kctx->led_brightness = kctx->config_brightness;
                    kctx->led_stride = op->stride;
                    kctx->led_active = 1;
                    kctx->led_hold = 1;
                    kctx->led_next_ms = now + 40;
                    if (cfg_mode == KBDD_CONFIG_ANIMATION) {
                        kctx->led_anim = kctx->config_anim;
                        kctx->led_anim_period = kctx->config_anim_period;
                        memcpy(kctx->led_anim_rgb, kctx->config_anim_rgb,
                               sizeof(kctx->led_anim_rgb));
                        kctx->led_anim_count = op->count;
                        kctx->led_anim_start_ms = now;
                    } else {
                        kctx->led_anim = 0;
                    }
                }
            }
        } else {
            kctx->restore_attempts++;
            if (kctx->restore_attempts >= 10) {
                kctx->restore_pending = 0;
                fprintf(stderr,
                        "kbdd: could not restore saved LED configuration after %u attempts\n",
                        kctx->restore_attempts);
            } else {
                kctx->restore_next_ms = now + 500;
            }
        }
        break;
    }
    }

    op_free(op);
}

static void op_start_next(void)
{
    while (g_child_pid < 0) {
        kbdd_op_node_t *op = op_dequeue();
        if (op == NULL) {
            return;
        }
        /* A newer LED command superseded this queued frame: do not touch the
         * keyboard with stale colours. Frames already in flight cannot be
         * recalled; op_complete ignores their state via the same epoch. */
        if ((op->type == OP_SEND_FRAME || op->type == OP_RESTORE_CONFIG) &&
            op->epoch != g_led_epoch) {
            if (op->type == OP_RESTORE_CONFIG && g_ctx != NULL) {
                g_ctx->restore_pending = 0;
            }
            op_free(op);
            continue;
        }

        kbdd_op_result_t failed;
        memset(&failed, 0, sizeof(failed));

        if (!op->has_dev || op_refresh_dev(op) != 0) {
            failed.rc = KBDD_DEV_EIO;
            op_complete(g_ctx, op, &failed);
            continue;
        }

        int pipefd[2];
        if (pipe(pipefd) != 0) {
            failed.rc = KBDD_DEV_EIO;
            op_complete(g_ctx, op, &failed);
            continue;
        }
        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            failed.rc = KBDD_DEV_EIO;
            op_complete(g_ctx, op, &failed);
            continue;
        }
        if (pid == 0) {
            close(pipefd[0]);
            op_child_close_fds(pipefd[1], op->dev.fd);
            op_child_exec(op, pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
        }
        g_pipe_fd = pipefd[0];
        g_child_pid = pid;
        g_current = op;
        g_op_start_ms = now_ms();
        g_result_size = 0;
        memset(&g_result, 0, sizeof(g_result));
        return;
    }
}

static int op_reap(pid_t pid, int *status, int options)
{
    for (;;) {
        pid_t r = waitpid(pid, status, options);
        if (r >= 0) {
            return (int)r;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == ECHILD) {
            return (int)pid; /* already reaped */
        }
        return -1;
    }
}

static void op_finish(kbdd_ctx_t *kctx)
{
    kbdd_op_node_t *op = g_current;
    g_current = NULL;
    op_complete(kctx, op, &g_result);
    g_result_size = 0;
    memset(&g_result, 0, sizeof(g_result));
}

static void op_poll_result(kbdd_ctx_t *kctx)
{
    if (g_child_pid < 0 || g_pipe_fd < 0) {
        return;
    }

    if (now_ms() - g_op_start_ms > KBDD_OP_TIMEOUT_MS) {
        int status = 0;
        (void)kill(g_child_pid, SIGKILL);
        (void)op_reap(g_child_pid, &status, 0);
        close(g_pipe_fd);
        g_pipe_fd = -1;
        g_child_pid = -1;
        memset(&g_result, 0, sizeof(g_result));
        g_result.rc = KBDD_DEV_EIO;
        g_result_size = sizeof(g_result);
        op_finish(kctx);
        return;
    }

    struct pollfd pfd;
    pfd.fd = g_pipe_fd;
    pfd.events = POLLIN | POLLHUP | POLLERR;
    pfd.revents = 0;
    int pr = poll(&pfd, 1, 0);

    uint8_t *dst = (uint8_t *)&g_result;
    size_t want = sizeof(g_result);
    int eof = 0;

    if (pr > 0) {
        for (;;) {
            ssize_t n = read(g_pipe_fd, dst + g_result_size,
                             want - g_result_size);
            if (n > 0) {
                g_result_size += (size_t)n;
                if (g_result_size >= want) {
                    break;
                }
                continue;
            }
            if (n == 0) {
                eof = 1;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break; /* EAGAIN */
        }
    }

    int status = 0;
    int reaped = op_reap(g_child_pid, &status, WNOHANG);
    if (reaped == 0) {
        return; /* still running */
    }

    /* Child is gone: everything it wrote is buffered in the pipe. Drain it
     * even when poll raced with the exit, so a completed operation is not
     * mistaken for an I/O failure. */
    while (!eof && g_result_size < want) {
        ssize_t n = read(g_pipe_fd, dst + g_result_size, want - g_result_size);
        if (n > 0) {
            g_result_size += (size_t)n;
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }

    close(g_pipe_fd);
    g_pipe_fd = -1;
    g_child_pid = -1;

    if (g_result_size != want) {
        memset(&g_result, 0, sizeof(g_result));
        g_result.rc = KBDD_DEV_EIO;
        g_result_size = want;
    }
    op_finish(kctx);
}

void kbdd_op_poll(kbdd_ctx_t *kctx)
{
    g_ctx = kctx;
    op_start_next();
    op_poll_result(kctx);
    op_start_next(); /* keep the queue moving without waiting for the loop */
}

void kbdd_op_cancel_led(void)
{
    g_led_epoch++;
}

int kbdd_op_pending(void)
{
    return g_child_pid >= 0 || g_queue_len > 0;
}

int kbdd_op_has_room(void)
{
    return g_queue_len < KBDD_MAX_PENDING;
}

static int op_submit(kbdd_op_type_t type, const kbdd_ipc_message_t *req,
                     const kbdd_hid_device_t *dev, size_t stride,
                     uint8_t flags, size_t count)
{
    if (g_queue_len >= KBDD_MAX_PENDING) {
        return -1;
    }
    kbdd_op_node_t *op = op_new(type, req);
    if (op == NULL) {
        return -1;
    }
    op_attach_dev(op, dev);
    op->stride = stride;
    op->flags = flags;
    op->count = count;
    op_set_payload(op, req != NULL ? req->payload : NULL,
                   req != NULL ? req->payload_len : 0);
    op_enqueue(op);
    if (g_child_pid < 0) {
        op_start_next();
    }
    return 0;
}

int kbdd_op_queue_read_active(const kbdd_ipc_message_t *req,
                              const kbdd_hid_device_t *dev)
{
    return op_submit(OP_READ_ACTIVE, req, dev, 0, 0, 0);
}

int kbdd_op_queue_read_block(const kbdd_ipc_message_t *req,
                             const kbdd_hid_device_t *dev)
{
    return op_submit(OP_READ_BLOCK, req, dev, 0, 0, 0);
}

int kbdd_op_queue_switch(const kbdd_ipc_message_t *req,
                         const kbdd_hid_device_t *dev)
{
    return op_submit(OP_SWITCH_PROFILE, req, dev, 0, 0, 0);
}

int kbdd_op_queue_effect(const kbdd_ipc_message_t *req,
                         const kbdd_hid_device_t *dev)
{
    return op_submit(OP_SET_EFFECT, req, dev, 0, 0, 0);
}

int kbdd_op_queue_field(const kbdd_ipc_message_t *req,
                        const kbdd_hid_device_t *dev, size_t stride,
                        uint8_t flags, const uint8_t *frame)
{
    if (!kbdd_op_has_room()) {
        return -1;
    }
    kbdd_op_node_t *op = op_new(OP_SET_FIELD, req);
    if (op == NULL) {
        return -1;
    }
    op_attach_dev(op, dev);
    op->stride = stride;
    op->flags = flags;
    op_set_payload(op, req != NULL ? req->payload : NULL,
                   req != NULL ? req->payload_len : 0);
    if (frame != NULL) {
        op_set_data(op, frame, KBC_LED_BUFFER_SIZE);
    }
    op_enqueue(op);
    if (g_child_pid < 0) {
        op_start_next();
    }
    return 0;
}

int kbdd_op_queue_colors(const kbdd_ipc_message_t *req,
                         const kbdd_hid_device_t *dev)
{
    return op_submit(OP_SET_COLORS, req, dev, 0, 0, 0);
}

int kbdd_op_queue_save(const kbdd_ipc_message_t *req,
                       const kbdd_hid_device_t *dev, uint8_t flags)
{
    return op_submit(OP_SAVE_PROFILE, req, dev, 0, flags, 0);
}

int kbdd_op_queue_restore(const kbdd_ipc_message_t *req,
                          const kbdd_hid_device_t *dev)
{
    return op_submit(OP_RESTORE_PROFILE, req, dev, 0, 0, 0);
}

int kbdd_op_queue_enable_notif(const kbdd_ipc_message_t *req,
                               const kbdd_hid_device_t *dev)
{
    return op_submit(OP_ENABLE_NOTIF, req, dev, 0, 0, 0);
}

int kbdd_op_queue_frame_from_base(const kbdd_ipc_message_t *req,
                                  const kbdd_hid_device_t *dev,
                                  const uint8_t *frame, size_t stride,
                                  size_t count, uint8_t flags)
{
    if (g_queue_len >= KBDD_MAX_PENDING) {
        return -1;
    }
    kbdd_op_node_t *op = op_new(OP_SEND_FRAME, req);
    if (op == NULL) {
        return -1;
    }
    op_attach_dev(op, dev);
    op->stride = stride;
    op->count = count;
    op->flags = flags;
    op_set_payload(op, req != NULL ? req->payload : NULL,
                   req != NULL ? req->payload_len : 0);
    op_set_data(op, frame, KBC_LED_BUFFER_SIZE);
    op_enqueue(op);
    if (g_child_pid < 0) {
        op_start_next();
    }
    return 0;
}

int kbdd_op_queue_restore_config(kbdd_ctx_t *k, const kbdd_hid_device_t *dev)
{
    if (k == NULL || dev == NULL || !kbdd_op_has_room()) {
        return -1;
    }
    if (k->config_mode == KBDD_CONFIG_NONE) {
        k->restore_pending = 0;
        return 0;
    }
    size_t matrix = dev->model != NULL ? dev->model->matrix_size : 70;
    size_t stride = k->config_stride != 0 ? k->config_stride : matrix;
    size_t count = matrix < stride ? matrix : stride;
    uint8_t flags = (k->allow_profile_write ? 0x01u : 0u) | (k->allow_led ? 0x02u : 0u);

    uint8_t payload[4];
    payload[0] = k->config_brightness;
    payload[1] = k->config_effect;
    payload[2] = k->config_effect < KBC_LED_EFFECT_COUNT ? k->config_speed[k->config_effect] : 0;
    payload[3] = k->config_anim;

    uint8_t frame[KBC_LED_BUFFER_SIZE];
    int has_frame = 0;

    if (k->config_mode != KBDD_CONFIG_EFFECT && k->allow_led) {
        if (kbc_led_frame_page_count(stride) == 0) {
            return -1;
        }
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
            for (size_t i = 0; i < count; i++) {
                kbc_led_frame_set(k->led_base_frame, stride, i,
                                  k->config_positions[i][0],
                                  k->config_positions[i][1],
                                  k->config_positions[i][2]);
            }
        }
        memcpy(frame, k->led_base_frame, sizeof(frame));
        if (kbc_led_frame_apply_brightness(frame, stride, k->config_brightness) != KBC_OK) {
            return -1;
        }
        memcpy(k->led_frame, frame, sizeof(k->led_frame));
        has_frame = 1;
    }

    kbdd_op_node_t *op = op_new(OP_RESTORE_CONFIG, NULL);
    if (op == NULL) {
        return -1;
    }
    op_attach_dev(op, dev);
    op->mode = k->config_mode;
    op->flags = flags;
    op->stride = stride;
    op->count = count;
    op_set_payload(op, payload, sizeof(payload));
    if (has_frame) {
        op_set_data(op, frame, sizeof(frame));
    }
    op_enqueue(op);
    if (g_child_pid < 0) {
        op_start_next();
    }
    return 0;
}

void kbdd_op_client_closed(kbdd_ctx_t *k, int fd)
{
    (void)k;
    for (kbdd_op_node_t *n = g_head; n != NULL; n = n->next) {
        if (n->client_fd == fd) {
            n->discard = 1;
        }
    }
    if (g_current != NULL && g_current->client_fd == fd) {
        g_current->discard = 1;
    }
}

void kbdd_op_shutdown(void)
{
    if (g_child_pid >= 0) {
        pid_t pid = g_child_pid;
        int status = 0;
        int reaped = 0;
        long grace_ms = 0;

        if (g_current != NULL &&
            (g_current->type == OP_SAVE_PROFILE ||
             g_current->type == OP_RESTORE_PROFILE)) {
            /* Let a flash commit reach its read-back verification instead of
             * killing the child mid-write and leaving the keyboard unverified. */
            grace_ms = 3000;
        }
        while (grace_ms > 0) {
            if (op_reap(pid, &status, WNOHANG) != 0) {
                reaped = 1;
                break;
            }
            struct timespec ts = {0, 10 * 1000 * 1000};
            nanosleep(&ts, NULL);
            grace_ms -= 10;
        }
        if (!reaped) {
            (void)kill(pid, SIGKILL);
            (void)op_reap(pid, &status, 0);
        }
        g_child_pid = -1;
    }
    if (g_pipe_fd >= 0) {
        close(g_pipe_fd);
        g_pipe_fd = -1;
    }
    op_free(g_current);
    g_current = NULL;
    while (g_head != NULL) {
        kbdd_op_node_t *next = g_head->next;
        op_free(g_head);
        g_head = next;
    }
    g_tail = NULL;
    g_queue_len = 0;
    g_ctx = NULL;
}
