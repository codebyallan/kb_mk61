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

/* daemon */
int kbdd_run(int argc, char **argv);

#endif /* KBDD_H */