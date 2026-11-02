#ifndef KBDD_H
#define KBDD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "kbipc.h"

#define KBDD_MAX_DEVICES 16
#define KBDD_MAX_CLIENTS 16

typedef struct kbdd_model {
    const char *name;
    uint16_t vid;
    uint16_t pid;
    size_t matrix_size;
    size_t led_count;
} kbdd_model_t;

/* Single source of truth for the supported device models (kept in the header
 * so daemon.c and hidraw.c cannot diverge). */
static const kbdd_model_t KBDD_MODELS[] = {
    {"MK61L", 0x1EA7, 0x9018, 70, 61},
    {"MK68M", 0x1EA7, 0x9050, 75, 68},
    {"GK8150", 0x2442, 0x0011, 75, 68},
    {"GK8150", 0x2442, 0xB011, 75, 68},
    {"GK8150", 0x2442, 0xB014, 75, 68},
};

typedef struct kbdd_hid_device {
    char path[256];
    int fd;
    uint16_t vid;
    uint16_t pid;
    uint16_t usage_page;
    uint16_t usage;
    int role; /* 0 = control (feature reports), 1 = input (notifications) */
    const kbdd_model_t *model;
} kbdd_hid_device_t;

/* hidraw module */
int kbdd_hidraw_enumerate(const kbdd_model_t *models, size_t model_count,
                          kbdd_hid_device_t **out_devs, size_t *out_count);
void kbdd_hidraw_free(kbdd_hid_device_t *devs, size_t count);
int kbdd_hidraw_open(kbdd_hid_device_t *dev);
void kbdd_hidraw_close(kbdd_hid_device_t *dev);
ssize_t kbdd_hidraw_get_feature(const kbdd_hid_device_t *dev, uint8_t *buf, size_t len);
ssize_t kbdd_hidraw_set_feature(const kbdd_hid_device_t *dev, const uint8_t *buf, size_t len);
ssize_t kbdd_hidraw_read(const kbdd_hid_device_t *dev, uint8_t *buf, size_t len);

/* device layer: the only code allowed to talk to the control interface.
 * Allowlist: query opcodes 0x81/0x82 and LED frame pages (opcode 0x07).
 * No other opcode can be sent through this layer. */
typedef struct kbdd_dev_io {
    void *ctx;
    ssize_t (*set_feature)(void *ctx, const uint8_t *buf, size_t len);
    ssize_t (*get_feature)(void *ctx, uint8_t *buf, size_t len);
    void (*sleep_ms)(void *ctx, unsigned ms);
} kbdd_dev_io_t;

#define KBDD_DEV_OK 0
#define KBDD_DEV_EINVAL (-1)
#define KBDD_DEV_EIO (-2)
#define KBDD_DEV_EPROTO (-3)
#define KBDD_DEV_ENOSAVE (-5)  /* profile cannot be saved safely (custom LED data, effect 14/15) */
#define KBDD_DEV_EVERIFY (-4)  /* write not confirmed by read-back (rolled back) */

void kbdd_dev_io_init_hidraw(kbdd_dev_io_t *io, const kbdd_hid_device_t *dev);
int kbdd_dev_query(const kbdd_dev_io_t *io, uint8_t opcode, uint8_t profile,
                   uint8_t out[264]);
int kbdd_dev_send_frame(const kbdd_dev_io_t *io, const uint8_t frame[432],
                        size_t stride);
int kbdd_dev_enable_notifications(const kbdd_dev_io_t *io);

/* Changes the LED effect of the ACTIVE profile: read 0x81, read 0x82, write
 * 0x02 with only the effect byte changed, read back and verify. On a failed
 * verification the original block is written back (EVERIFY). */
int kbdd_dev_set_effect(const kbdd_dev_io_t *io, uint8_t mode, uint8_t *profile_out);
/* Same flow for one profile field: effect, brightness, speed, direction or report rate. */
typedef struct {
    uint8_t effect;
    uint8_t rgb[14][3];
    size_t count;
} kbdd_color_edit_t;
/* Sets the colours of one effect in the ACTIVE profile (RAM only; see kbdd_dev_save_profile). */
int kbdd_dev_set_colors(const kbdd_dev_io_t *io, const kbdd_color_edit_t *ce, uint8_t *profile_out);
/* Human-readable detail of the last failed check in kbdd_dev_set_field (empty if none). */
const char *kbdd_dev_last_diag(void);
int kbdd_dev_set_field(const kbdd_dev_io_t *io, int field, uint8_t value, uint8_t *profile_out);
int kbdd_dev_restore_profile(const kbdd_dev_io_t *io, uint8_t profile,
                             const uint8_t block[264]);

/* Switches the active profile (1..4): reads 0x81, sends 0x01, reads 0x81 again to
 * confirm. Already-active profile = no write. On failure tries to go back to the
 * previous profile (EVERIFY). *changed = 1 only if the profile actually changed. */
int kbdd_dev_switch_profile(const kbdd_dev_io_t *io, uint8_t profile, int *changed);

/* Commits the current RAM block of the ACTIVE profile to flash: 0x02 with byte 134 = 1,
 * then = 255 (the keyboard saves on each change). Requires a clean block. */
int kbdd_dev_save_profile(const kbdd_dev_io_t *io, uint8_t *profile_out);

/* uinput module */
typedef struct kbdd_uinput kbdd_uinput_t;
kbdd_uinput_t *kbdd_uinput_open(const char *path);
void kbdd_uinput_close(kbdd_uinput_t *u);
int kbdd_uinput_create_keyboard(kbdd_uinput_t *u);
int kbdd_uinput_send_key(kbdd_uinput_t *u, uint16_t code, int value);
int kbdd_uinput_sync(kbdd_uinput_t *u);

/* ipc module */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t payload_len;
    uint16_t opcode;
    uint16_t request_id;
    uint8_t *payload;
} kbdd_ipc_message_t;

typedef int (*kbdd_ipc_handler_t)(const kbdd_ipc_message_t *req,
                                  kbdd_ipc_message_t *resp, void *ctx);

typedef struct kbdd_ipc_server kbdd_ipc_server_t;
kbdd_ipc_server_t *kbdd_ipc_server_create(void);
void kbdd_ipc_server_destroy(kbdd_ipc_server_t *s);
int kbdd_ipc_server_fd(const kbdd_ipc_server_t *s);
int kbdd_ipc_server_accept(kbdd_ipc_server_t *s);
int kbdd_ipc_server_poll(kbdd_ipc_server_t *s, int timeout_ms,
                         kbdd_ipc_handler_t handler, void *ctx);
int kbdd_ipc_server_publish(kbdd_ipc_server_t *s, const char *event);

/* daemon */
int kbdd_run(int argc, char **argv);

#endif /* KBDD_H */