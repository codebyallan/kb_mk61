#include "kbcore.h"

#include <stdlib.h>
#include <string.h>

#define KBC_MACRO_MAX_STEPS_INTERNAL 80

struct kbc_macro_step {
    uint8_t delay;
    int key_down;
    uint8_t keycode;
};

struct kbc_macro {
    uint16_t repetitions;
    size_t step_count;
    size_t capacity;
    struct kbc_macro_step *steps;
};

kbc_macro *kbc_macro_create(void)
{
    kbc_macro *m = (kbc_macro *)calloc(1, sizeof(*m));
    if (m == NULL) return NULL;
    m->repetitions = 1;
    m->capacity = 16;
    m->steps = (struct kbc_macro_step *)calloc(m->capacity, sizeof(*m->steps));
    if (m->steps == NULL) {
        free(m);
        return NULL;
    }
    return m;
}

void kbc_macro_destroy(kbc_macro *m)
{
    if (m == NULL) return;
    free(m->steps);
    free(m);
}

static int kbc_macro_keycode_valid(uint8_t keycode)
{
    /* Conservative allowlist of standard HID keyboard usage IDs:
       0x00 (reserved/no-op), printable/control usages up to 0x7F and the
       eight modifier keys 0xE0..0xE7. Rejects vendor/unknown usages. */
    if (keycode <= 0x7F) {
        return 1;
    }
    if (keycode >= 0xE0 && keycode <= 0xE7) {
        return 1;
    }
    return 0;
}

kbc_status kbc_macro_set_repetitions(kbc_macro *m, uint16_t reps)
{
    if (m == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (reps > KBC_MACRO_MAX_REPETITIONS) return KBC_ERR_RANGE;
    m->repetitions = reps;
    return KBC_OK;
}

uint16_t kbc_macro_get_repetitions(const kbc_macro *m)
{
    return (m == NULL) ? 0 : m->repetitions;
}

kbc_status kbc_macro_add_step(kbc_macro *m, uint8_t delay, int key_down, uint8_t keycode)
{
    struct kbc_macro_step *tmp;

    if (m == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (!kbc_macro_keycode_valid(keycode)) return KBC_ERR_PROTOCOL;
    if (m->step_count >= KBC_MACRO_MAX_STEPS) return KBC_ERR_RANGE;
    if (m->step_count >= m->capacity) {
        size_t newcap = m->capacity * 2;
        if (newcap > KBC_MACRO_MAX_STEPS) newcap = KBC_MACRO_MAX_STEPS;
        if (newcap <= m->capacity) return KBC_ERR_NO_MEMORY;
        tmp = (struct kbc_macro_step *)realloc(m->steps, newcap * sizeof(*tmp));
        if (tmp == NULL) return KBC_ERR_NO_MEMORY;
        memset(tmp + m->capacity, 0, (newcap - m->capacity) * sizeof(*tmp));
        m->steps = tmp;
        m->capacity = newcap;
    }
    if (delay < KBC_MACRO_MIN_DELAY) delay = KBC_MACRO_MIN_DELAY;
    m->steps[m->step_count].delay = delay;
    m->steps[m->step_count].key_down = key_down ? 1 : 0;
    m->steps[m->step_count].keycode = keycode;
    m->step_count++;
    return KBC_OK;
}

size_t kbc_macro_step_count(const kbc_macro *m)
{
    return (m == NULL) ? 0 : m->step_count;
}

kbc_status kbc_macro_get_step(const kbc_macro *m, size_t index, uint8_t *delay, int *key_down, uint8_t *keycode)
{
    if (m == NULL || delay == NULL || key_down == NULL || keycode == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (index >= m->step_count) return KBC_ERR_RANGE;
    *delay = m->steps[index].delay;
    *key_down = m->steps[index].key_down;
    *keycode = m->steps[index].keycode;
    return KBC_OK;
}

kbc_status kbc_macro_encode(const kbc_macro *m, uint8_t *buf, size_t buf_len)
{
    size_t i;

    if (m == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (buf_len != KBC_MACRO_SIZE) return KBC_ERR_PROTOCOL;
    if (m->step_count > KBC_MACRO_MAX_STEPS) return KBC_ERR_RANGE;

    memset(buf, 0, KBC_MACRO_SIZE);
    buf[0] = (uint8_t)((m->repetitions >> 8) & 0xFF);
    buf[1] = (uint8_t)(m->repetitions & 0xFF);
    for (i = 0; i < m->step_count; i++) {
        size_t off = 2 + i * 3;
        uint8_t delay_byte = m->steps[i].delay;
        if (m->steps[i].key_down) delay_byte |= 0x80;
        buf[off] = delay_byte;
        buf[off + 1] = m->steps[i].delay; /* low byte (same value, min 20) */
        buf[off + 2] = m->steps[i].keycode;
    }
    return KBC_OK;
}

kbc_status kbc_macro_decode(kbc_macro *m, const uint8_t *buf, size_t len)
{
    size_t steps = 0;
    size_t k;
    size_t i;
    uint16_t reps;

    if (m == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (len < 2) return KBC_ERR_PROTOCOL;

    reps = (uint16_t)((buf[0] << 8) | buf[1]);
    if (reps > KBC_MACRO_MAX_REPETITIONS) return KBC_ERR_RANGE;

    if (len == KBC_MACRO_SIZE) {
        /* Padded 256-byte buffer: trim trailing zero steps. */
        k = (KBC_MACRO_SIZE - 2) / 3;
        if (k > KBC_MACRO_MAX_STEPS) k = KBC_MACRO_MAX_STEPS;
        for (i = 0; i < k; i++) {
            size_t off = 2 + i * 3;
            if (buf[off] == 0 && buf[off + 1] == 0 && buf[off + 2] == 0) {
                break;
            }
            steps++;
        }
        /* trailing bytes after the last step must be zero */
        for (i = 2 + steps * 3; i < len; i++) {
            if (buf[i] != 0) return KBC_ERR_PROTOCOL;
        }
    } else {
        if ((len - 2) % 3 != 0) return KBC_ERR_PROTOCOL;
        k = (len - 2) / 3;
        if (k > KBC_MACRO_MAX_STEPS) return KBC_ERR_RANGE;
        steps = k;
    }

    if (steps > KBC_MACRO_MAX_STEPS) return KBC_ERR_RANGE;

    /* reset steps */
    m->step_count = 0;
    m->repetitions = reps;
    for (i = 0; i < steps; i++) {
        size_t off = 2 + i * 3;
        uint8_t delay = (uint8_t)(buf[off] & 0x7F);
        int kd = (buf[off] & 0x80) ? 1 : 0;
        uint8_t keycode = buf[off + 2];

        if (buf[off + 1] != 0) delay = buf[off + 1];
        if (delay < KBC_MACRO_MIN_DELAY) delay = KBC_MACRO_MIN_DELAY;
        if (!kbc_macro_keycode_valid(keycode)) return KBC_ERR_PROTOCOL;

        if (kbc_macro_add_step(m, delay, kd, keycode) != KBC_OK) {
            m->step_count = 0;
            return KBC_ERR_PROTOCOL;
        }
    }
    return KBC_OK;
}

kbc_status kbc_macro_encode_report(const kbc_macro *m, uint8_t hw_profile, uint8_t position,
                                  uint8_t *buf, size_t buf_len)
{
    uint8_t macro_buf[KBC_MACRO_SIZE];
    uint8_t params[4] = {0,0,0,0};
    kbc_status st;

    if (m == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (position >= KBC_MAX_MATRIX_SIZE) return KBC_ERR_RANGE;
    st = kbc_macro_encode(m, macro_buf, sizeof(macro_buf));
    if (st != KBC_OK) return st;
    return kbc_build_feature_report(KBC_OP_MACRO, hw_profile, position, params, macro_buf, KBC_MACRO_SIZE, buf, buf_len);
}
