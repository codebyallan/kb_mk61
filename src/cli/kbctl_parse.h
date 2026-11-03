#ifndef KBCTL_PARSE_H
#define KBCTL_PARSE_H

#include <stdint.h>
#include <stddef.h>

/* Parse a hardware profile index in 1..4. Returns 0 on success, -1 on error. */
int kbctl_parse_index(const char *s, long *out);

/* Parse an RGB triplet as "RRGGBB" or "#RRGGBB" (case-insensitive). Returns 0 on success, -1 on error. */
int kbctl_parse_rgb(const char *s, uint8_t *r, uint8_t *g, uint8_t *b);

/* Parse an unsigned integer <= max. Returns 0 on success, -1 on error. */
int kbctl_parse_uint(const char *s, unsigned long max, unsigned long *out);

#endif
