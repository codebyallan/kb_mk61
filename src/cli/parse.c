#include "kbctl_parse.h"

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int parse_digits(const char *s, unsigned long *out)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned long v = 0;

    if (s == NULL || s[0] == '\0' || !isdigit(p[0])) {
        return -1;
    }
    for (; *p; p++) {
        unsigned digit;
        if (!isdigit(*p)) {
            return -1;
        }
        digit = (unsigned)(*p - '0');
        if (v > (ULONG_MAX - digit) / 10UL) {
            return -1;
        }
        v = v * 10UL + digit;
    }
    *out = v;
    return 0;
}

int kbctl_parse_index(const char *s, long *out)
{
    unsigned long v;

    if (s == NULL) {
        return -1;
    }
    if (parse_digits(s, &v) != 0 || v < 1 || v > 4) {
        return -1;
    }
    *out = (long)v;
    return 0;
}

int kbctl_parse_rgb(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
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

int kbctl_parse_uint(const char *s, unsigned long max, unsigned long *out)
{
    unsigned long v;

    if (s == NULL || parse_digits(s, &v) != 0 || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}
