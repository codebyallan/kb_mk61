#include "kbdd.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct kbdd_uinput {
    int fd;
};

kbdd_uinput_t *kbdd_uinput_open(const char *path)
{
    const char *p = (path != NULL && path[0] != '\0') ? path : "/dev/uinput";
    int fd = open(p, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    kbdd_uinput_t *u = calloc(1, sizeof(*u));
    if (u == NULL) {
        close(fd);
        return NULL;
    }
    u->fd = fd;
    return u;
}

void kbdd_uinput_close(kbdd_uinput_t *u)
{
    if (u == NULL) {
        return;
    }
    if (u->fd >= 0) {
        ioctl(u->fd, UI_DEV_DESTROY);
        close(u->fd);
    }
    free(u);
}

int kbdd_uinput_create_keyboard(kbdd_uinput_t *u)
{
    if (u == NULL || u->fd < 0) {
        return -1;
    }
    if (ioctl(u->fd, UI_SET_EVBIT, EV_KEY) < 0) {
        return -1;
    }
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(u->fd, UI_SET_KEYBIT, i);
    }
    if (ioctl(u->fd, UI_SET_EVBIT, EV_SYN) < 0) {
        return -1;
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", "kbdd virtual keyboard");

    if (ioctl(u->fd, UI_DEV_SETUP, &setup) < 0) {
        /* fall back: older kernels */
        struct uinput_user_dev udev;
        memset(&udev, 0, sizeof(udev));
        snprintf(udev.name, UINPUT_MAX_NAME_SIZE, "%s", "kbdd virtual keyboard");
        udev.id.bustype = BUS_USB;
        udev.id.vendor = 0x1234;
        udev.id.product = 0x5678;
        if (write(u->fd, &udev, sizeof(udev)) != (ssize_t)sizeof(udev)) {
            return -1;
        }
    }
    if (ioctl(u->fd, UI_DEV_CREATE) < 0) {
        return -1;
    }
    return 0;
}

int kbdd_uinput_send_key(kbdd_uinput_t *u, uint16_t code, int value)
{
    if (u == NULL || u->fd < 0) {
        return -1;
    }
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if (write(u->fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        return -1;
    }
    return 0;
}

int kbdd_uinput_sync(kbdd_uinput_t *u)
{
    if (u == NULL || u->fd < 0) {
        return -1;
    }
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(u->fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        return -1;
    }
    return 0;
}