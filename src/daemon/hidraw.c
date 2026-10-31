#include "kbdd.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const kbdd_model_t KBDD_MODELS[] = {
    {"MK61L", 0x1EA7, 0x9018, 70, 61},
    {"MK68M", 0x1EA7, 0x9050, 75, 68},
    {"GK8150", 0x2442, 0x0011, 75, 68},
    {"GK8150", 0x2442, 0xB011, 75, 68},
    {"GK8150", 0x2442, 0xB014, 75, 68},
};

static const kbdd_model_t *kbdd_model_lookup(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < sizeof(KBDD_MODELS) / sizeof(KBDD_MODELS[0]); i++) {
        if (KBDD_MODELS[i].vid == vid && KBDD_MODELS[i].pid == pid) {
            return &KBDD_MODELS[i];
        }
    }
    return NULL;
}

static int kbdd_hidraw_get_usage(int fd, uint16_t *page, uint16_t *usage)
{
    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0) {
        return -1;
    }
    if (desc_size <= 0 || desc_size > 4096) {
        return -1;
    }

    struct hidraw_report_descriptor *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return -1;
    }
    r->size = (unsigned int)desc_size;
    if (ioctl(fd, HIDIOCGRDESC, r) < 0) {
        free(r);
        return -1;
    }

    uint16_t cur_page = 0;
    uint16_t cur_usage = 0;
    int found = 0;
    size_t i = 0;
    while (i < r->size) {
        uint8_t prefix = r->value[i];
        if (prefix == 0xFE) { /* long item */
            if (i + 2 >= r->size) {
                break;
            }
            int size = r->value[i + 1];
            i += (size_t)(3 + size);
            if (i > r->size) {
                break;
            }
            continue;
        }
        int size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;
        if (i + 1 + (size_t)size > r->size) {
            break;
        }
        uint32_t data = 0;
        for (int j = 0; j < size; j++) {
            data |= (uint32_t)r->value[i + 1 + (size_t)j] << (8 * j);
        }

        if (type == 1) { /* global */
            if (tag == 0) { /* Usage Page */
                cur_page = (uint16_t)data;
            }
        } else if (type == 2) { /* local */
            if (tag == 0) { /* Usage */
                cur_usage = (uint16_t)data;
            }
        } else if (type == 0) { /* main */
            if (tag == 0x0A && data == 0x01) { /* Collection Application */
                if (!found) {
                    *page = cur_page;
                    *usage = cur_usage;
                    found = 1;
                }
            }
        }

        i += (size_t)(1 + size);
    }

    free(r);
    return found ? 0 : -1;
}

int kbdd_hidraw_enumerate(const kbdd_model_t *models, size_t model_count,
                          kbdd_hid_device_t **out_devs, size_t *out_count)
{
    (void)models;
    (void)model_count;

    if (out_devs == NULL || out_count == NULL) {
        return -1;
    }
    *out_devs = NULL;
    *out_count = 0;

    DIR *d = opendir("/dev");
    if (d == NULL) {
        return -1;
    }

    size_t cap = 0;
    size_t count = 0;
    kbdd_hid_device_t *devs = NULL;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0) {
            continue;
        }
        char path[256];
        const char prefix[] = "/dev/";
        size_t plen = sizeof(prefix) - 1;
        size_t nlen = strlen(ent->d_name);
        if (plen + nlen + 1 > sizeof(path)) {
            continue;
        }
        memcpy(path, prefix, plen);
        memcpy(path + plen, ent->d_name, nlen + 1);

        int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        struct hidraw_devinfo info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            continue;
        }

        const kbdd_model_t *model = kbdd_model_lookup((uint16_t)info.vendor,
                                                      (uint16_t)info.product);
        if (model == NULL) {
            close(fd);
            continue;
        }

        uint16_t page = 0;
        uint16_t usage = 0;
        if (kbdd_hidraw_get_usage(fd, &page, &usage) != 0) {
            close(fd);
            continue;
        }

        int role = -1;
        if (page == 0xFF01 && usage == 0x01) {
            role = 0;
        } else if (page == 0xFF00 && usage == 0xFF00) {
            role = 1;
        }
        if (role < 0) {
            close(fd);
            continue;
        }

        if (count == cap) {
            size_t ncap = cap == 0 ? 8 : cap * 2;
            kbdd_hid_device_t *nd = realloc(devs, ncap * sizeof(*nd));
            if (nd == NULL) {
                close(fd);
                continue;
            }
            devs = nd;
            cap = ncap;
        }

        memset(&devs[count], 0, sizeof(devs[count]));
        memcpy(devs[count].path, path, sizeof(devs[count].path));
        devs[count].fd = -1;
        devs[count].vid = (uint16_t)info.vendor;
        devs[count].pid = (uint16_t)info.product;
        devs[count].usage_page = page;
        devs[count].usage = usage;
        devs[count].role = role;
        devs[count].model = model;
        count++;

        close(fd);
    }

    closedir(d);
    *out_devs = devs;
    *out_count = count;
    return 0;
}

void kbdd_hidraw_free(kbdd_hid_device_t *devs, size_t count)
{
    if (devs == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        kbdd_hidraw_close(&devs[i]);
    }
    free(devs);
}

int kbdd_hidraw_open(kbdd_hid_device_t *dev)
{
    if (dev == NULL) {
        return -1;
    }
    if (dev->fd >= 0) {
        return 0;
    }
    dev->fd = open(dev->path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (dev->fd < 0) {
        return -1;
    }
    /* validate VID/PID after open */
    struct hidraw_devinfo info;
    memset(&info, 0, sizeof(info));
    if (ioctl(dev->fd, HIDIOCGRAWINFO, &info) < 0 ||
        (uint16_t)info.vendor != dev->vid ||
        (uint16_t)info.product != dev->pid) {
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }
    return 0;
}

void kbdd_hidraw_close(kbdd_hid_device_t *dev)
{
    if (dev == NULL) {
        return;
    }
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

ssize_t kbdd_hidraw_get_feature(const kbdd_hid_device_t *dev, uint8_t *buf, size_t len)
{
    if (dev == NULL || dev->fd < 0 || buf == NULL || len == 0) {
        return -1;
    }
    if (len > (size_t)INT_MAX) {
        return -1;
    }
    /* buf[0] must contain report id */
    int r = ioctl(dev->fd, HIDIOCGFEATURE(len), buf);
    if (r < 0) {
        return -1;
    }
    return (ssize_t)r;
}

ssize_t kbdd_hidraw_set_feature(const kbdd_hid_device_t *dev, const uint8_t *buf, size_t len)
{
    if (dev == NULL || dev->fd < 0 || buf == NULL || len == 0) {
        return -1;
    }
    if (len > (size_t)INT_MAX) {
        return -1;
    }
    int r = ioctl(dev->fd, HIDIOCSFEATURE(len), (void *)buf);
    if (r < 0) {
        return -1;
    }
    return (ssize_t)r;
}

ssize_t kbdd_hidraw_read(const kbdd_hid_device_t *dev, uint8_t *buf, size_t len)
{
    if (dev == NULL || dev->fd < 0 || buf == NULL || len == 0) {
        return -1;
    }
    ssize_t r = read(dev->fd, buf, len);
    return r;
}