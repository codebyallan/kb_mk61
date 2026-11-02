#include "kbcore.h"

#include <stdlib.h>
#include <string.h>

#define KBC_PROFILE_LAYERS_INTERNAL 4

struct kbc_profile {
    size_t matrix_size;
    size_t led_count;
    size_t layer_count;
    uint8_t *layers;

    uint8_t hw_profile;
    uint8_t report_rate_index;
    uint8_t led_mode;
    uint8_t led_intensity;
    uint8_t led_speed;
    uint8_t spectrum[9];
    uint8_t colors[8][3];
    size_t  color_count;
    uint8_t direction;
    uint8_t six_key_mode;
    uint8_t win_lock;
    uint8_t custom_total;
    uint8_t custom_current;
    uint8_t led_state;
};

static int valid_matrix_size(size_t n)
{
    return n > 0 && n <= KBC_MAX_MATRIX_SIZE;
}

static int valid_led_count(size_t l)
{
    return l > 0 && l <= KBC_MAX_LED_COUNT;
}

kbc_profile *kbc_profile_create(size_t matrix_size, size_t led_count)
{
    kbc_profile *p;

    if (!valid_matrix_size(matrix_size) || !valid_led_count(led_count)) {
        return NULL;
    }
    p = (kbc_profile *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return NULL;
    }
    p->matrix_size = matrix_size;
    p->led_count = led_count;
    p->layer_count = KBC_PROFILE_LAYERS_INTERNAL;
    p->layers = (uint8_t *)calloc(p->layer_count * p->matrix_size, 1);
    if (p->layers == NULL) {
        free(p);
        return NULL;
    }
    p->hw_profile = 1;
    p->custom_total = 0xFF;
    p->custom_current = 1;
    p->led_state = 1;
    p->color_count = 1;
    p->colors[0][0] = 255;
    return p;
}

void kbc_profile_destroy(kbc_profile *p)
{
    if (p == NULL) {
        return;
    }
    free(p->layers);
    free(p);
}

kbc_status kbc_profile_validate(const kbc_profile *p)
{
    if (p == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (!valid_matrix_size(p->matrix_size) || !valid_led_count(p->led_count)) {
        return KBC_ERR_RANGE;
    }
    if (p->layers == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (p->hw_profile < 1 || p->hw_profile > 4) {
        return KBC_ERR_RANGE;
    }
    if (p->led_mode > 12 && p->led_mode != 15) {
        return KBC_ERR_RANGE;
    }
    if (p->custom_total != 0xFF && (p->custom_total < 1 || p->custom_total > 5)) {
        return KBC_ERR_RANGE;
    }
    if (p->color_count > 8) {
        return KBC_ERR_RANGE;
    }
    return KBC_OK;
}

size_t kbc_profile_matrix_size(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->matrix_size;
}

size_t kbc_profile_led_count(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->led_count;
}

size_t kbc_profile_layer_count(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->layer_count;
}

kbc_status kbc_profile_set_key(kbc_profile *p, size_t layer, size_t pos, uint8_t value)
{
    if (p == NULL || p->layers == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (layer >= p->layer_count || pos >= p->matrix_size) {
        return KBC_ERR_RANGE;
    }
    p->layers[layer * p->matrix_size + pos] = value;
    return KBC_OK;
}

kbc_status kbc_profile_get_key(const kbc_profile *p, size_t layer, size_t pos, uint8_t *out)
{
    if (p == NULL || out == NULL || p->layers == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (layer >= p->layer_count || pos >= p->matrix_size) {
        return KBC_ERR_RANGE;
    }
    *out = p->layers[layer * p->matrix_size + pos];
    return KBC_OK;
}

kbc_status kbc_profile_set_layer(kbc_profile *p, size_t layer, const uint8_t *values, size_t count)
{
    if (p == NULL || values == NULL || p->layers == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (layer >= p->layer_count || count != p->matrix_size) {
        return KBC_ERR_RANGE;
    }
    memcpy(p->layers + layer * p->matrix_size, values, count);
    return KBC_OK;
}

kbc_status kbc_profile_get_layer(const kbc_profile *p, size_t layer, uint8_t *out, size_t count)
{
    if (p == NULL || out == NULL || p->layers == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (layer >= p->layer_count || count != p->matrix_size) {
        return KBC_ERR_RANGE;
    }
    memcpy(out, p->layers + layer * p->matrix_size, count);
    return KBC_OK;
}

kbc_status kbc_profile_set_hw_profile(kbc_profile *p, uint8_t hw)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (hw < 1 || hw > 4) return KBC_ERR_RANGE;
    p->hw_profile = hw;
    return KBC_OK;
}

uint8_t kbc_profile_get_hw_profile(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->hw_profile;
}

kbc_status kbc_profile_set_report_rate_index(kbc_profile *p, uint8_t idx)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (idx == 255) return KBC_ERR_RANGE;
    p->report_rate_index = idx;
    return KBC_OK;
}

uint8_t kbc_profile_get_report_rate_index(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->report_rate_index;
}

kbc_status kbc_profile_set_led_mode(kbc_profile *p, uint8_t mode)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (mode > 12 && mode != 15) return KBC_ERR_RANGE;
    p->led_mode = mode;
    return KBC_OK;
}

uint8_t kbc_profile_get_led_mode(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->led_mode;
}

kbc_status kbc_profile_set_led_intensity(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->led_intensity = v;
    return KBC_OK;
}

uint8_t kbc_profile_get_led_intensity(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->led_intensity;
}

kbc_status kbc_profile_set_led_speed(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->led_speed = v;
    return KBC_OK;
}

uint8_t kbc_profile_get_led_speed(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->led_speed;
}

kbc_status kbc_profile_set_spectrum(kbc_profile *p, size_t index, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (index >= 9) return KBC_ERR_RANGE;
    p->spectrum[index] = v;
    return KBC_OK;
}

uint8_t kbc_profile_get_spectrum(const kbc_profile *p, size_t index)
{
    if (p == NULL || index >= 9) return 0;
    return p->spectrum[index];
}

kbc_status kbc_profile_set_color(kbc_profile *p, size_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (index >= 8) return KBC_ERR_RANGE;
    p->colors[index][0] = r;
    p->colors[index][1] = g;
    p->colors[index][2] = b;
    if (index >= p->color_count) p->color_count = index + 1;
    return KBC_OK;
}

kbc_status kbc_profile_get_color(const kbc_profile *p, size_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (p == NULL || r == NULL || g == NULL || b == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (index >= 8) return KBC_ERR_RANGE;
    *r = p->colors[index][0];
    *g = p->colors[index][1];
    *b = p->colors[index][2];
    return KBC_OK;
}

kbc_status kbc_profile_set_direction(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (v > 3) return KBC_ERR_RANGE;
    p->direction = v;
    return KBC_OK;
}

kbc_status kbc_profile_set_win_lock(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->win_lock = v ? 1 : 0;
    return KBC_OK;
}

kbc_status kbc_profile_set_six_key_mode(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->six_key_mode = v ? 1 : 0;
    return KBC_OK;
}

kbc_status kbc_profile_set_custom_total(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (v != 0xFF && (v < 1 || v > 5)) return KBC_ERR_RANGE;
    p->custom_total = v;
    return KBC_OK;
}

kbc_status kbc_profile_set_custom_current(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->custom_current = v;
    return KBC_OK;
}

kbc_status kbc_profile_set_led_state(kbc_profile *p, uint8_t v)
{
    if (p == NULL) return KBC_ERR_INVALID_ARGUMENT;
    p->led_state = v ? 1 : 0;
    return KBC_OK;
}

static kbc_status write_report(uint8_t opcode, uint8_t param0, uint8_t param1,
                              const uint8_t params[4], const uint8_t *data,
                              size_t data_len, uint8_t *buf, size_t buf_len)
{
    return kbc_build_feature_report(opcode, param0, param1, params, data, data_len, buf, buf_len);
}

kbc_status kbc_profile_encode_select(uint8_t hw_profile, uint8_t *buf, size_t buf_len)
{
    uint8_t params[4] = {0,0,0,0};
    if (hw_profile != 0xFF && (hw_profile < 1 || hw_profile > 4)) {
        return KBC_ERR_RANGE;
    }
    return write_report(KBC_OP_SELECT_PROFILE, hw_profile, 0, params, NULL, 0, buf, buf_len);
}

kbc_status kbc_profile_encode_select_logical(size_t logical, uint8_t *buf, size_t buf_len)
{
    if (logical > 3) return KBC_ERR_RANGE;
    return kbc_profile_encode_select((uint8_t)(logical + 1), buf, buf_len);
}

kbc_status kbc_profile_encode_key_matrix(const kbc_profile *p, uint8_t hw_profile,
                                        uint8_t *buf, size_t buf_len)
{
    uint8_t params[4] = {0,0,0,0};
    uint8_t data[KBC_FEATURE_DATA_SIZE];

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (kbc_profile_validate(p) != KBC_OK) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (3 * p->matrix_size > KBC_FEATURE_DATA_SIZE) return KBC_ERR_RANGE;

    memset(data, 0, sizeof(data));
    {
        size_t i;
        for (i = 0; i < 3 && i < p->layer_count; i++) {
            memcpy(data + i * p->matrix_size, p->layers + i * p->matrix_size, p->matrix_size);
        }
    }
    return write_report(KBC_OP_KEY_MATRIX, hw_profile, 0, params, data, KBC_FEATURE_DATA_SIZE, buf, buf_len);
}

kbc_status kbc_profile_decode_key_matrix(kbc_profile *p, const uint8_t *buf, size_t len,
                                        uint8_t *hw_profile)
{
    kbc_feature_report rep;
    kbc_status st;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    st = kbc_parse_feature_report(buf, len, &rep);
    if (st != KBC_OK) return st;
    if (rep.opcode != KBC_OP_KEY_MATRIX) return KBC_ERR_PROTOCOL;
    if (rep.param0 < 1 || rep.param0 > 4) return KBC_ERR_PROTOCOL;
    if (rep.param1 != 0) return KBC_ERR_PROTOCOL;
    if (3 * p->matrix_size > KBC_FEATURE_DATA_SIZE) return KBC_ERR_RANGE;

    memcpy(p->layers, rep.data, p->matrix_size);
    memcpy(p->layers + p->matrix_size, rep.data + p->matrix_size, p->matrix_size);
    if (2 * p->matrix_size + p->matrix_size <= KBC_FEATURE_DATA_SIZE) {
        memcpy(p->layers + 2 * p->matrix_size, rep.data + 2 * p->matrix_size, p->matrix_size);
    }
    if (hw_profile != NULL) *hw_profile = rep.param0;
    p->hw_profile = rep.param0;
    return KBC_OK;
}

kbc_status kbc_profile_encode_params_a(const kbc_profile *p, uint8_t hw_profile,
                                      uint8_t *buf, size_t buf_len)
{
    uint8_t full[KBC_FEATURE_REPORT_SIZE];

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (kbc_profile_validate(p) != KBC_OK) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (buf_len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;

    memset(full, 0, sizeof(full));
    full[0] = KBC_FEATURE_REPORT_ID;
    full[1] = KBC_OP_PROFILE_PARAMS;
    full[2] = hw_profile;
    full[3] = 0;
    full[4] = (uint8_t)(p->report_rate_index + 1);
    full[7] = p->led_mode;
    if (4 + 126 < KBC_FEATURE_REPORT_SIZE) full[4 + 126] = p->custom_total;
    if (4 + 127 < KBC_FEATURE_REPORT_SIZE) full[4 + 127] = p->custom_current;
    if (4 + 128 < KBC_FEATURE_REPORT_SIZE) full[4 + 128] = p->led_state;
    memcpy(buf, full, KBC_FEATURE_REPORT_SIZE);
    return KBC_OK;
}

kbc_status kbc_profile_encode_params_b(const kbc_profile *p, uint8_t hw_profile,
                                      uint8_t *buf, size_t buf_len)
{
    uint8_t full[KBC_FEATURE_REPORT_SIZE];
    size_t base = 8;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (kbc_profile_validate(p) != KBC_OK) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (buf_len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;

    memset(full, 0, sizeof(full));
    full[0] = KBC_FEATURE_REPORT_ID;
    full[1] = KBC_OP_PROFILE_PARAMS;
    full[2] = hw_profile;
    full[3] = 0;

    full[base] = (uint8_t)(p->report_rate_index + 1);
    full[base + 3] = p->led_mode;
    full[base + 4] = p->led_intensity;
    full[base + 5] = p->direction;
    full[KBC_PBLK_OFF_SPEED] = p->led_speed;
    {
        size_t i;
        for (i = 0; i < 9; i++) {
            if (base + 24 + i < KBC_FEATURE_REPORT_SIZE) {
                full[base + 24 + i] = p->spectrum[i];
            }
        }
    }
    {
        size_t c;
        for (c = 0; c < 8 && c < p->color_count; c++) {
            size_t off = base + 84 + c * 24;
            if (off + 16 < KBC_FEATURE_REPORT_SIZE) {
                full[off] = p->colors[c][0];
                full[off + 8] = p->colors[c][1];
                full[off + 16] = p->colors[c][2];
            }
        }
    }
    if (base + 126 < KBC_FEATURE_REPORT_SIZE) full[base + 126] = p->custom_total;
    if (base + 127 < KBC_FEATURE_REPORT_SIZE) full[base + 127] = p->custom_current;
    if (base + 128 < KBC_FEATURE_REPORT_SIZE) full[base + 128] = p->led_state;
    memcpy(buf, full, KBC_FEATURE_REPORT_SIZE);
    return KBC_OK;
}

kbc_status kbc_profile_decode_params_a(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile)
{
    kbc_feature_report rep;
    kbc_status st;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    st = kbc_parse_feature_report(buf, len, &rep);
    if (st != KBC_OK) return st;
    if (rep.opcode != KBC_OP_PROFILE_PARAMS) return KBC_ERR_PROTOCOL;
    if (rep.param0 < 1 || rep.param0 > 4) return KBC_ERR_PROTOCOL;
    p->hw_profile = rep.param0;
    if (hw_profile != NULL) *hw_profile = rep.param0;
    p->report_rate_index = (rep.params[0] > 0) ? (uint8_t)(rep.params[0] - 1) : 0;
    p->led_mode = rep.params[3];
    if (p->led_mode > 12 && p->led_mode != 15) return KBC_ERR_PROTOCOL;
    if (4 + 126 < KBC_FEATURE_REPORT_SIZE) p->custom_total = buf[4 + 126];
    if (4 + 127 < KBC_FEATURE_REPORT_SIZE) p->custom_current = buf[4 + 127];
    if (4 + 128 < KBC_FEATURE_REPORT_SIZE) p->led_state = buf[4 + 128];
    return KBC_OK;
}

static kbc_status kbc_profile_decode_params_b_impl(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile, int validate_led)
{
    kbc_feature_report rep;
    kbc_status st;
    size_t base = 8;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    st = kbc_parse_feature_report(buf, len, &rep);
    if (st != KBC_OK) return st;
    if (rep.opcode != KBC_OP_PROFILE_PARAMS) return KBC_ERR_PROTOCOL;
    if (rep.param0 < 1 || rep.param0 > 4) return KBC_ERR_PROTOCOL;
    p->hw_profile = rep.param0;
    if (hw_profile != NULL) *hw_profile = rep.param0;
    p->report_rate_index = (buf[base] > 0) ? (uint8_t)(buf[base] - 1) : 0;
    p->led_mode = buf[base + 3];
    if (validate_led && p->led_mode > 12 && p->led_mode != 15) return KBC_ERR_PROTOCOL;
    p->led_intensity = buf[base + 4];
    p->direction = (buf[base + 5] > 3) ? 0 : buf[base + 5];
    p->led_speed = buf[KBC_PBLK_OFF_SPEED];
    {
        size_t i;
        for (i = 0; i < 9; i++) {
            if (base + 24 + i < KBC_FEATURE_REPORT_SIZE) {
                p->spectrum[i] = buf[base + 24 + i];
            }
        }
    }
    {
        size_t c;
        for (c = 0; c < 8; c++) {
            size_t off = base + 84 + c * 24;
            if (off + 16 < KBC_FEATURE_REPORT_SIZE) {
                p->colors[c][0] = buf[off];
                p->colors[c][1] = buf[off + 8];
                p->colors[c][2] = buf[off + 16];
            }
        }
    }
    if (base + 126 < KBC_FEATURE_REPORT_SIZE) p->custom_total = buf[base + 126];
    if (base + 127 < KBC_FEATURE_REPORT_SIZE) p->custom_current = buf[base + 127];
    if (base + 128 < KBC_FEATURE_REPORT_SIZE) p->led_state = buf[base + 128];
    return KBC_OK;
}

kbc_status kbc_profile_decode_params_b(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile)
{
    return kbc_profile_decode_params_b_impl(p, buf, len, hw_profile, 1);
}

kbc_status kbc_profile_decode_block_shifted(kbc_profile *p, const uint8_t *buf, size_t len,
                                           int apply_shift, uint8_t *hw_profile)
{
    uint8_t shifted[KBC_FEATURE_REPORT_SIZE];
    size_t i;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;
    if (!apply_shift) {
        return kbc_profile_decode_params_b_impl(p, buf, len, hw_profile, 1);
    }
    memcpy(shifted, buf, KBC_FEATURE_REPORT_SIZE);
    for (i = KBC_FEATURE_DATA_OFFSET; i + 1 < KBC_FEATURE_REPORT_SIZE; i++) {
        shifted[i] = buf[i + 1];
    }
    shifted[KBC_FEATURE_REPORT_SIZE - 1] = 0;
    return kbc_profile_decode_params_b_impl(p, shifted, len, hw_profile, 0);
}

kbc_status kbc_profile_encode_extra(const kbc_profile *p, uint8_t hw_profile,
                                   uint8_t *buf, size_t buf_len)
{
    uint8_t full[KBC_FEATURE_REPORT_SIZE];
    size_t base = 8;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (kbc_profile_validate(p) != KBC_OK) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (buf_len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;

    memset(full, 0, sizeof(full));
    full[0] = KBC_FEATURE_REPORT_ID;
    full[1] = KBC_OP_PROFILE_EXTRA;
    full[2] = hw_profile;
    full[3] = 0;
    if (base + 3 < KBC_FEATURE_REPORT_SIZE) full[base + 3] = p->direction;
    if (base + 4 < KBC_FEATURE_REPORT_SIZE) full[base + 4] = p->six_key_mode;
    if (base + 5 < KBC_FEATURE_REPORT_SIZE) full[base + 5] = p->win_lock;
    memcpy(buf, full, KBC_FEATURE_REPORT_SIZE);
    return KBC_OK;
}

kbc_status kbc_profile_decode_extra(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile)
{
    kbc_feature_report rep;
    kbc_status st;
    size_t base = 8;

    if (p == NULL || buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    st = kbc_parse_feature_report(buf, len, &rep);
    if (st != KBC_OK) return st;
    if (rep.opcode != KBC_OP_PROFILE_EXTRA) return KBC_ERR_PROTOCOL;
    if (rep.param0 < 1 || rep.param0 > 4) return KBC_ERR_PROTOCOL;
    p->hw_profile = rep.param0;
    if (hw_profile != NULL) *hw_profile = rep.param0;
    if (base + 3 < KBC_FEATURE_REPORT_SIZE) p->direction = buf[base + 3] ? 1 : 0;
    if (base + 4 < KBC_FEATURE_REPORT_SIZE) p->six_key_mode = buf[base + 4] ? 1 : 0;
    if (base + 5 < KBC_FEATURE_REPORT_SIZE) p->win_lock = buf[base + 5] ? 1 : 0;
    return KBC_OK;
}

kbc_status kbc_profile_encode_custom(kbc_profile *p, uint8_t hw_profile, uint8_t slot_index,
                                    uint8_t opcode, const uint8_t *data, size_t data_len,
                                    uint8_t *buf, size_t buf_len)
{
    uint8_t params[4] = {0,0,0,0};
    (void)p;
    if (slot_index > 4) return KBC_ERR_RANGE;
    if (opcode != KBC_OP_CUSTOM_COLOR && opcode != KBC_OP_CUSTOM_FRAME && opcode != KBC_OP_CUSTOM_EFFECT) {
        return KBC_ERR_PROTOCOL;
    }
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    return kbc_build_feature_report(opcode, hw_profile, slot_index, params, data, data_len, buf, buf_len);
}

kbc_status kbc_read_active_profile_decode(const uint8_t *buf, size_t len, uint8_t *hw_profile)
{
    kbc_feature_report rep;
    kbc_status st;

    if (buf == NULL || hw_profile == NULL) return KBC_ERR_INVALID_ARGUMENT;
    st = kbc_parse_feature_report(buf, len, &rep);
    if (st != KBC_OK) return st;
    *hw_profile = rep.opcode;
    if (*hw_profile < 1 || *hw_profile > 4) return KBC_ERR_PROTOCOL;
    return KBC_OK;
}

uint8_t kbc_profile_get_direction(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->direction;
}

uint8_t kbc_profile_get_win_lock(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->win_lock;
}

uint8_t kbc_profile_get_six_key_mode(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->six_key_mode;
}

uint8_t kbc_profile_get_custom_total(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->custom_total;
}

uint8_t kbc_profile_get_custom_current(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->custom_current;
}

uint8_t kbc_profile_get_led_state(const kbc_profile *p)
{
    return (p == NULL) ? 0 : p->led_state;
}
