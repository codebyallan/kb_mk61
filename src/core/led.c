#include "kbcore.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

kbc_status kbc_led_buffer_validate(const uint8_t *buf, size_t len)
{
    if (buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (len != KBC_LED_BUFFER_SIZE) return KBC_ERR_PROTOCOL;
    return KBC_OK;
}

kbc_status kbc_led_set_rgb(uint8_t *buf, size_t led_count, size_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (led_count == 0 || led_count > KBC_MAX_LED_COUNT) return KBC_ERR_RANGE;
    if (index >= led_count) return KBC_ERR_RANGE;
    /* planar: R in 0..143, G in 144..287, B in 288..431 */
    buf[index] = r;
    buf[KBC_LED_PLANAR_STRIDE + index] = g;
    buf[2 * KBC_LED_PLANAR_STRIDE + index] = b;
    return KBC_OK;
}

kbc_status kbc_led_get_rgb(const uint8_t *buf, size_t led_count, size_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (buf == NULL || r == NULL || g == NULL || b == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (led_count == 0 || led_count > KBC_MAX_LED_COUNT) return KBC_ERR_RANGE;
    if (index >= led_count) return KBC_ERR_RANGE;
    *r = buf[index];
    *g = buf[KBC_LED_PLANAR_STRIDE + index];
    *b = buf[2 * KBC_LED_PLANAR_STRIDE + index];
    return KBC_OK;
}

size_t kbc_led_page_count(size_t buffer_size)
{
    if (buffer_size == 0) return 0;
    if (buffer_size > ((size_t)-1 - KBC_PAGE_SIZE + 1)) return 0;
    /* also cap to a sane maximum number of pages */
    {
        size_t pages = (buffer_size + KBC_PAGE_SIZE - 1) / KBC_PAGE_SIZE;
        if (pages > 64) return 0; /* overflow/garbage guard */
        return pages;
    }
}

kbc_status kbc_led_encode_page(const uint8_t *buf, size_t buf_len, size_t page_index,
                              uint8_t *out, size_t out_len)
{
    size_t offset;
    uint8_t params[4] = {0,0,0,0};

    if (buf == NULL || out == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (buf_len != KBC_LED_BUFFER_SIZE) return KBC_ERR_PROTOCOL;
    if (page_index >= kbc_led_page_count(KBC_LED_BUFFER_SIZE)) return KBC_ERR_RANGE;
    if (out_len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;

    offset = page_index * KBC_PAGE_SIZE;
    memset(out, 0, KBC_FEATURE_REPORT_SIZE);
    out[0] = KBC_FEATURE_REPORT_ID;
    out[1] = KBC_OP_LED_PAGE;
    out[2] = (uint8_t)page_index;
    out[3] = 0;
    /* data region 8..263 = 256 bytes */
    {
        size_t i;
        for (i = 0; i < KBC_PAGE_SIZE; i++) {
            size_t src = offset + i;
            if (src < KBC_LED_BUFFER_SIZE) {
                out[KBC_FEATURE_DATA_OFFSET + i] = buf[src];
            } else {
                out[KBC_FEATURE_DATA_OFFSET + i] = 0;
            }
        }
    }
    (void)params;
    return KBC_OK;
}

/* Observed (possibly defective) behavior: send only page 0. */
kbc_status kbc_led_encode_page_observed_single(const uint8_t *buf, size_t buf_len,
                                              uint8_t *out, size_t out_len)
{
    return kbc_led_encode_page(buf, buf_len, 0, out, out_len);
}

kbc_status kbc_led_encode_game_mode(const uint8_t *colors, size_t colors_len,
                                   uint8_t hw_profile, uint8_t *buf, size_t buf_len)
{
    uint8_t params[4] = {0,0,0,0};
    uint8_t data[KBC_FEATURE_DATA_SIZE];
    size_t i;

    if (colors == NULL && colors_len != 0) return KBC_ERR_INVALID_ARGUMENT;
    if (hw_profile < 1 || hw_profile > 4) return KBC_ERR_RANGE;
    if (buf == NULL) return KBC_ERR_INVALID_ARGUMENT;
    if (buf_len != KBC_FEATURE_REPORT_SIZE) return KBC_ERR_PROTOCOL;
    if (colors_len > KBC_FEATURE_DATA_SIZE) return KBC_ERR_INVALID_ARGUMENT;

    memset(data, 0, sizeof(data));
    if (colors != NULL) {
        memcpy(data, colors, colors_len);
    }
    /* ensure at least 210 bytes zero-filled (already zero) */
    for (i = colors_len; i < 210 && i < KBC_FEATURE_DATA_SIZE; i++) {
        data[i] = 0;
    }
    return kbc_build_feature_report(KBC_OP_LED_DATA, hw_profile, 0, params, data, KBC_FEATURE_DATA_SIZE, buf, buf_len);
}
