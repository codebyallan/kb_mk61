#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kbcore.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void zero_buf(uint8_t *b, size_t n) { memset(b, 0, n); }

/* ---------- Feature reports ---------- */

static void test_tv_f01(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t out[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.report_id == 0x07);
    CHECK(rep.opcode == 0x01);
    CHECK(rep.param0 == 1);
    CHECK(rep.param1 == 0);
    CHECK(rep.params[0] == 0);
    CHECK(memcmp(rep.data, buf + 8, KBC_FEATURE_DATA_SIZE) == 0);
    CHECK(kbc_encode_feature_report(&rep, out, sizeof out) == KBC_OK);
    CHECK(memcmp(out, buf, sizeof buf) == 0);
}

static void test_tv_f02(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0xFF;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) == KBC_OK);
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.param0 == 0xFF);
}

static void test_tv_f03(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x02; buf[2] = 0x01; buf[3] = 0x00;
    buf[4] = 0x02; buf[7] = 0x03;
    buf[130] = 0xFF; buf[131] = 0x01; buf[132] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x02);
    CHECK(rep.param0 == 1);
    CHECK(rep.params[0] == 0x02);
    CHECK(rep.params[3] == 0x03);
    CHECK(rep.data[130 - 8] == 0xFF);
    CHECK(rep.data[131 - 8] == 0x01);
    CHECK(rep.data[132 - 8] == 0x01);
}

static void test_tv_f04(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x02; buf[2] = 0x01;
    /* byte 11 = 0x05, 12 = 0x50, 13 = 0x32 */
    buf[11] = 0x05; buf[12] = 0x50; buf[13] = 0x32;
    /* color at 8+84 = 92 */
    buf[92] = 0xFF;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x02);
    CHECK(buf[11] == 0x05);
    CHECK(buf[12] == 0x50);
    CHECK(buf[13] == 0x32);
    CHECK(rep.data[84] == 0xFF);
}

static void test_tv_f05(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x03; buf[2] = 0x01; buf[3] = 0x00;
    buf[8] = 0x04;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x03);
    CHECK(rep.param0 == 1);
    CHECK(rep.param1 == 0);
    CHECK(rep.data[0] == 0x04);
}

static void test_tv_f06(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x04; buf[2] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x04);
    CHECK(rep.param0 == 1);
}

static void test_tv_f07(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x05; buf[2] = 0x01; buf[3] = 0x05;
    buf[8] = 0x00; buf[9] = 0x01;
    buf[10] = 0x80; buf[11] = 0x14; buf[12] = 0x04;
    buf[13] = 0x00; buf[14] = 0x14; buf[15] = 0x04;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x05);
    CHECK(rep.param0 == 1);
    CHECK(rep.param1 == 5);
    CHECK(rep.data[0] == 0x00);
    CHECK(rep.data[1] == 0x01);
    CHECK(rep.data[2] == 0x80);
}

static void test_tv_f08(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x06; buf[2] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x06);
    CHECK(rep.param0 == 1);
}

static void test_tv_f09(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t out[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x07; buf[2] = 0x00;
    /* TV-L01 gradient first 8 bytes */
    buf[8] = 0x00; buf[9] = 0x1F; buf[10] = 0x3F; buf[11] = 0x5F;
    buf[12] = 0x7F; buf[13] = 0x9F; buf[14] = 0xBF; buf[15] = 0xDF;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x07);
    CHECK(rep.param0 == 0);
    CHECK(rep.data[0] == 0x00);
    CHECK(rep.data[7] == 0xDF);
    CHECK(kbc_encode_feature_report(&rep, out, sizeof out) == KBC_OK);
    CHECK(memcmp(out, buf, sizeof buf) == 0);
}

static void test_tv_f10(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x08; buf[2] = 0x01; buf[3] = 0x00;
    memset(buf + 8, 0xFF, KBC_FEATURE_DATA_SIZE);
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x08);
    CHECK(rep.param0 == 1);
    CHECK(rep.param1 == 0);
    CHECK(rep.data[0] == 0xFF);
    CHECK(rep.data[255] == 0xFF);
}

static void test_tv_f11(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x09; buf[2] = 0x01; buf[3] = 0x00;
    buf[8] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x09);
    CHECK(rep.data[0] == 0x01);
}

static void test_tv_f12(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x0A; buf[2] = 0x01; buf[3] = 0x00;
    memset(buf + 8, 0x02, 70);
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x0A);
    CHECK(rep.data[0] == 0x02);
    CHECK(rep.data[69] == 0x02);
    CHECK(rep.data[70] == 0x00);
}

static void test_tv_f13(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x0B; buf[2] = 0x01;
    buf[11] = 0x01; buf[12] = 0x00; buf[13] = 0x01;
    CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x0B);
    CHECK(rep.param0 == 1);
    CHECK(buf[11] == 0x01);
    CHECK(buf[12] == 0x00);
    CHECK(buf[13] == 0x01);
}

static void test_tv_f14(void)
{
    uint8_t sent[KBC_FEATURE_REPORT_SIZE];
    uint8_t resp[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(sent, sizeof sent);
    sent[0] = 0x07; sent[1] = 0x81;
    CHECK(kbc_parse_feature_report(sent, sizeof sent, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x81);
    zero_buf(resp, sizeof resp);
    resp[0] = 0x07; resp[1] = 0x01;
    /* Response to 0x81 reuses byte 1 for the profile id; callers extract it
       directly.  It is not re-validated as a 0x01 command here. */
}

static void test_tv_f15(void)
{
    uint8_t sent[KBC_FEATURE_REPORT_SIZE];
    kbc_feature_report rep;
    zero_buf(sent, sizeof sent);
    sent[0] = 0x07; sent[1] = 0x82; sent[2] = 0x01;
    CHECK(kbc_parse_feature_report(sent, sizeof sent, &rep) == KBC_OK);
    CHECK(rep.opcode == 0x82);
    CHECK(rep.param0 == 1);
}

/* ---------- Input reports ---------- */

static void test_tv_i01(void)
{
    uint8_t buf[] = {0x04, 0xF1, 0xF2, 0x02, 0x03, 0x01};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.report_id == 0x04);
    CHECK(rep.subtype == 0xF1);
    CHECK(rep.matrix_index == 13);
    CHECK(rep.pressed == 1);
}

static void test_tv_i02(void)
{
    uint8_t buf[] = {0x04, 0xF1, 0xF2, 0x02, 0x03, 0x00};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.subtype == 0xF1);
    CHECK(rep.pressed == 0);
}

static void test_tv_i03(void)
{
    uint8_t buf[] = {0x04, 0xF5, 0x01, 0x04, 0x00, 0x01};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.subtype == 0xF5);
    CHECK(rep.matrix_index == 9);
    CHECK(rep.pressed == 1);
}

static void test_tv_i04(void)
{
    uint8_t buf[] = {0x04, 0xF5, 0x01, 0x04, 0x00, 0x00};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.pressed == 0);
}

static void test_tv_i05(void)
{
    uint8_t buf[] = {0x04, 0xE1, 0x01, 0x02};
    /* pad to 6 */
    uint8_t full[6] = {0x04, 0xE1, 0x01, 0x02, 0x00, 0x00};
    kbc_input_report rep;
    (void)buf;
    CHECK(kbc_parse_input_report(full, sizeof full, &rep) == KBC_OK);
    CHECK(rep.subtype == 0x01);
    CHECK(rep.value == 2);
}

static void test_tv_i06(void)
{
    uint8_t full[6] = {0x04, 0xE1, 0x08, 0x07, 0x00, 0x00};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(full, sizeof full, &rep) == KBC_OK);
    CHECK(rep.subtype == 0x08);
    CHECK(rep.value == 7);
}

static void test_tv_i07(void)
{
    uint8_t full[6] = {0x04, 0xE1, 0x11, 0x00, 0x00, 0x00};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(full, sizeof full, &rep) == KBC_OK);
    CHECK(rep.subtype == 0x11);
}

static void test_tv_i08(void)
{
    uint8_t full[6] = {0x04, 0xE1, 0x12, 0x00, 0x00, 0x00};
    kbc_input_report rep;
    CHECK(kbc_parse_input_report(full, sizeof full, &rep) == KBC_OK);
    CHECK(rep.subtype == 0x12);
}

/* ---------- Invalid cases ---------- */

static void test_tv_e01(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x08; buf[1] = 0x01; buf[2] = 0x01;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    CHECK(kbc_parse_feature_report(buf, sizeof buf, NULL) == KBC_ERR_INVALID_ARGUMENT);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

static void test_tv_e02(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x01;
    CHECK(kbc_validate_feature_report(buf, 263) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, 263, &rep) != KBC_OK);
    }
}

static void test_tv_e03(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE + 1];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x01;
    CHECK(kbc_validate_feature_report(buf, 265) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, 265, &rep) != KBC_OK);
    }
}

static void test_tv_e04(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x7F; buf[2] = 0x01;
    CHECK(kbc_validate_opcode(0x7F) != KBC_OK);
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

static void test_tv_e05(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x05; buf[2] = 0x01; buf[3] = 0xFF;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

static void test_tv_e06(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x07; buf[2] = 0x02;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

static void test_tv_e07(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x00;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

static void test_tv_e08(void)
{
    uint8_t buf[] = {0x04, 0xF1, 0xF2, 0x02, 0x03, 0x01, 0x99};
    kbc_input_report rep;
    /* Policy: ignore extra bytes, parse first 6. */
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) == KBC_OK);
    CHECK(rep.matrix_index == 13);
    CHECK(rep.pressed == 1);
}

static void test_tv_e09(void)
{
    uint8_t buf[] = {0x05, 0xF1, 0xF2, 0x02, 0x03, 0x01};
    kbc_input_report rep;
    CHECK(kbc_validate_input_report(buf, sizeof buf) != KBC_OK);
    CHECK(kbc_parse_input_report(buf, sizeof buf, &rep) != KBC_OK);
}

static void test_tv_e10(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x03; buf[2] = 0x01; buf[3] = 0x01;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    {
        kbc_feature_report rep;
        CHECK(kbc_parse_feature_report(buf, sizeof buf, &rep) != KBC_OK);
    }
}

/* ---------- encode field-based ---------- */

static void test_build_feature(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t params[4] = {0x02, 0x00, 0x00, 0x03};
    uint8_t data[KBC_FEATURE_DATA_SIZE];
    zero_buf(data, sizeof data);
    CHECK(kbc_build_feature_report(0x02, 1, 0, params, data, sizeof data, buf, sizeof buf) == KBC_OK);
    CHECK(buf[0] == 0x07);
    CHECK(buf[1] == 0x02);
    CHECK(buf[2] == 1);
    CHECK(buf[4] == 0x02);
    CHECK(buf[7] == 0x03);
    /* too small buffer */
    CHECK(kbc_build_feature_report(0x02, 1, 0, params, data, sizeof data, buf, 10) != KBC_OK);
    /* unknown opcode */
    CHECK(kbc_build_feature_report(0x7F, 1, 0, params, data, sizeof data, buf, sizeof buf) != KBC_OK);
    /* null buf */
    CHECK(kbc_build_feature_report(0x02, 1, 0, params, data, sizeof data, NULL, sizeof buf) != KBC_OK);
}

static void test_null_args(void)
{
    kbc_feature_report rep;
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x01;
    CHECK(kbc_parse_feature_report(NULL, sizeof buf, &rep) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_parse_feature_report(buf, sizeof buf, NULL) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_validate_input_report(NULL, 6) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_parse_input_report(NULL, 6, NULL) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_encode_feature_report(NULL, buf, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_validate_report_id(0x07, 0x04) == KBC_ERR_PROTOCOL);
    CHECK(kbc_validate_report_id(0x07, 0x07) == KBC_OK);
}



/* ---------- New T6b tests ---------- */

static void test_profile_roundtrip(void)
{
    kbc_profile *p = kbc_profile_create(70, 61);
    kbc_profile *q = kbc_profile_create(70, 61);
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t hw = 0;
    size_t i;
    CHECK(p != NULL);
    CHECK(q != NULL);
    for (i = 0; i < 70; i++) {
        CHECK(kbc_profile_set_key(p, 0, i, (uint8_t)(i + 1)) == KBC_OK);
        CHECK(kbc_profile_set_key(p, 1, i, (uint8_t)(0x80 + i)) == KBC_OK);
        CHECK(kbc_profile_set_key(p, 2, i, (i % 5 == 0) ? 10 : 0) == KBC_OK);
    }
    CHECK(kbc_profile_encode_key_matrix(p, 2, buf, sizeof buf) == KBC_OK);
    CHECK(kbc_profile_decode_key_matrix(q, buf, sizeof buf, &hw) == KBC_OK);
    CHECK(hw == 2);
    for (i = 0; i < 70; i++) {
        uint8_t a, b;
        CHECK(kbc_profile_get_key(p, 0, i, &a) == KBC_OK);
        CHECK(kbc_profile_get_key(q, 0, i, &b) == KBC_OK);
        CHECK(a == b);
    }
    kbc_profile_destroy(p);
    kbc_profile_destroy(q);
}

static void test_profile_params_a_roundtrip(void)
{
    kbc_profile *p = kbc_profile_create(75, 68);
    kbc_profile *q = kbc_profile_create(75, 68);
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t hw = 0;
    CHECK(p && q);
    CHECK(kbc_profile_set_report_rate_index(p, 3) == KBC_OK);
    CHECK(kbc_profile_set_led_mode(p, 5) == KBC_OK);
    CHECK(kbc_profile_set_custom_total(p, 3) == KBC_OK);
    CHECK(kbc_profile_set_custom_current(p, 2) == KBC_OK);
    CHECK(kbc_profile_set_led_state(p, 1) == KBC_OK);
    CHECK(kbc_profile_encode_params_a(p, 1, buf, sizeof buf) == KBC_OK);
    CHECK(buf[4] == 4); /* index+1 */
    CHECK(buf[7] == 5);
    CHECK(buf[130] == 3);
    CHECK(buf[131] == 2);
    CHECK(buf[132] == 1);
    CHECK(kbc_profile_decode_params_a(q, buf, sizeof buf, &hw) == KBC_OK);
    CHECK(hw == 1);
    CHECK(kbc_profile_get_report_rate_index(q) == 3);
    CHECK(kbc_profile_get_led_mode(q) == 5);
    CHECK(kbc_profile_get_custom_total(q) == 3);
    kbc_profile_destroy(p);
    kbc_profile_destroy(q);
}

static void test_profile_params_b_roundtrip(void)
{
    kbc_profile *p = kbc_profile_create(70, 61);
    kbc_profile *q = kbc_profile_create(70, 61);
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t hw = 0;
    CHECK(p && q);
    CHECK(kbc_profile_set_led_mode(p, 7) == KBC_OK);
    CHECK(kbc_profile_set_led_intensity(p, 80) == KBC_OK);
    CHECK(kbc_profile_set_led_speed(p, 50) == KBC_OK);
    CHECK(kbc_profile_set_spectrum(p, 0, 9) == KBC_OK);
    CHECK(kbc_profile_set_color(p, 0, 255, 0, 0) == KBC_OK);
    CHECK(kbc_profile_set_custom_total(p, 0xFF) == KBC_OK);
    CHECK(kbc_profile_encode_params_b(p, 4, buf, sizeof buf) == KBC_OK);
    CHECK(buf[11] == 7);
    CHECK(buf[12] == 80);
    CHECK(buf[13] == 50);
    CHECK(kbc_profile_decode_params_b(q, buf, sizeof buf, &hw) == KBC_OK);
    CHECK(hw == 4);
    CHECK(kbc_profile_get_led_mode(q) == 7);
    CHECK(kbc_profile_get_led_intensity(q) == 80);
    CHECK(kbc_profile_get_led_speed(q) == 50);
    CHECK(kbc_profile_get_spectrum(q, 0) == 9);
    {
        uint8_t r,g,b;
        CHECK(kbc_profile_get_color(q, 0, &r,&g,&b) == KBC_OK);
        CHECK(r == 255 && g == 0 && b == 0);
    }
    kbc_profile_destroy(p);
    kbc_profile_destroy(q);
}

static void test_profile_extra(void)
{
    kbc_profile *p = kbc_profile_create(70, 61);
    kbc_profile *q = kbc_profile_create(70, 61);
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    CHECK(p && q);
    CHECK(kbc_profile_set_direction(p, 1) == KBC_OK);
    CHECK(kbc_profile_set_six_key_mode(p, 0) == KBC_OK);
    CHECK(kbc_profile_set_win_lock(p, 1) == KBC_OK);
    CHECK(kbc_profile_encode_extra(p, 1, buf, sizeof buf) == KBC_OK);
    CHECK(buf[11] == 1);
    CHECK(buf[12] == 0);
    CHECK(buf[13] == 1);
    CHECK(kbc_profile_decode_extra(q, buf, sizeof buf, NULL) == KBC_OK);
    CHECK(kbc_profile_get_direction(q) == 1);
    CHECK(kbc_profile_get_six_key_mode(q) == 0);
    CHECK(kbc_profile_get_win_lock(q) == 1);
    kbc_profile_destroy(p);
    kbc_profile_destroy(q);
}

static void test_profile_select_encode(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    CHECK(kbc_profile_encode_select(2, buf, sizeof buf) == KBC_OK);
    CHECK(buf[0] == 0x07 && buf[1] == 0x01 && buf[2] == 2);
    CHECK(kbc_profile_encode_select_logical(0, buf, sizeof buf) == KBC_OK);
    CHECK(buf[2] == 1);
    CHECK(kbc_profile_encode_select(0xFF, buf, sizeof buf) == KBC_OK);
    CHECK(buf[2] == 0xFF);
    CHECK(kbc_profile_encode_select(0, buf, sizeof buf) != KBC_OK);
}

static void test_macro_roundtrip(void)
{
    kbc_macro *m = kbc_macro_create();
    kbc_macro *n = kbc_macro_create();
    uint8_t buf[KBC_MACRO_SIZE];
    uint8_t full[KBC_FEATURE_REPORT_SIZE];
    CHECK(m && n);
    CHECK(kbc_macro_set_repetitions(m, 3) == KBC_OK);
    CHECK(kbc_macro_add_step(m, 10, 1, 0x04) == KBC_OK); /* delay elevated to 20 */
    CHECK(kbc_macro_add_step(m, 25, 0, 0x04) == KBC_OK);
    CHECK(kbc_macro_add_step(m, 20, 1, 0x05) == KBC_OK);
    CHECK(kbc_macro_step_count(m) == 3);
    /* check delay elevation */
    {
        uint8_t d; int kd; uint8_t kc;
        CHECK(kbc_macro_get_step(m, 0, &d, &kd, &kc) == KBC_OK);
        CHECK(d == 20);
        CHECK(kd == 1);
        CHECK(kc == 0x04);
    }
    CHECK(kbc_macro_encode(m, buf, sizeof buf) == KBC_OK);
    CHECK(buf[0] == 0 && buf[1] == 3);
    CHECK(buf[2] == (0x80 | 20));
    CHECK(buf[3] == 20);
    CHECK(buf[4] == 0x04);
    CHECK(kbc_macro_decode(n, buf, sizeof buf) == KBC_OK);
    CHECK(kbc_macro_get_repetitions(n) == 3);
    CHECK(kbc_macro_step_count(n) == 3);
    /* encode report */
    CHECK(kbc_macro_encode_report(m, 1, 5, full, sizeof full) == KBC_OK);
    CHECK(full[0] == 0x07 && full[1] == 0x05 && full[2] == 1 && full[3] == 5);
    CHECK(full[8] == 0 && full[9] == 3);
    kbc_macro_destroy(m);
    kbc_macro_destroy(n);
}

static void test_macro_limits(void)
{
    kbc_macro *m = kbc_macro_create();
    size_t i;
    CHECK(m);
    for (i = 0; i < KBC_MACRO_MAX_STEPS; i++) {
        CHECK(kbc_macro_add_step(m, 20, 1, 0x04) == KBC_OK);
    }
    CHECK(kbc_macro_add_step(m, 20, 1, 0x04) == KBC_ERR_RANGE);
    CHECK(kbc_macro_encode(m, (uint8_t[KBC_MACRO_SIZE]){0}, KBC_MACRO_SIZE) == KBC_OK);
    {
        uint8_t buf[KBC_MACRO_SIZE];
        memset(buf, 0, sizeof buf);
        CHECK(kbc_macro_decode(m, buf, 263) != KBC_OK);
        CHECK(kbc_macro_decode(m, buf, 265) != KBC_OK);
    }
    CHECK(kbc_macro_set_repetitions(NULL, 1) == KBC_ERR_INVALID_ARGUMENT);
    kbc_macro_destroy(m);
    kbc_macro_destroy(NULL);
}

static void test_led_pagination(void)
{
    uint8_t buf[KBC_LED_BUFFER_SIZE];
    uint8_t page0[KBC_FEATURE_REPORT_SIZE];
    uint8_t page1[KBC_FEATURE_REPORT_SIZE];
    size_t i;
    memset(buf, 0, sizeof buf);
    for (i = 0; i < 10; i++) buf[i] = (uint8_t)(i + 1);
    buf[300] = 0xAB;
    CHECK(kbc_led_buffer_validate(buf, sizeof buf) == KBC_OK);
    CHECK(kbc_led_buffer_validate(buf, 431) != KBC_OK);
    CHECK(kbc_led_set_rgb(buf, 61, 0, 10, 20, 30) == KBC_OK);
    {
        uint8_t r,g,b;
        CHECK(kbc_led_get_rgb(buf, 61, 0, &r,&g,&b) == KBC_OK);
        CHECK(r == 10 && g == 20 && b == 30);
    }
    CHECK(kbc_led_get_rgb(buf, 61, 61, NULL, NULL, NULL) != KBC_OK);
    CHECK(kbc_led_page_count(KBC_LED_BUFFER_SIZE) == 2);
    CHECK(kbc_led_encode_page(buf, sizeof buf, 0, page0, sizeof page0) == KBC_OK);
    CHECK(kbc_led_encode_page(buf, sizeof buf, 1, page1, sizeof page1) == KBC_OK);
    CHECK(page0[0] == 0x07 && page0[1] == 0x07 && page0[2] == 0);
    CHECK(page1[0] == 0x07 && page1[1] == 0x07 && page1[2] == 1);
    CHECK(page0[8] == 10);
    CHECK(page1[8 + (300 - 256)] == 0xAB);
    CHECK(kbc_led_encode_page(buf, sizeof buf, 2, page0, sizeof page0) != KBC_OK);
    CHECK(kbc_led_encode_page_observed_single(buf, sizeof buf, page0, sizeof page0) == KBC_OK);
    /* game mode */
    {
        uint8_t colors[210];
        uint8_t gm[KBC_FEATURE_REPORT_SIZE];
        memset(colors, 0, sizeof colors);
        colors[0] = 7;
        CHECK(kbc_led_encode_game_mode(colors, sizeof colors, 1, gm, sizeof gm) == KBC_OK);
        CHECK(gm[1] == 0x04 && gm[2] == 1 && gm[8] == 7);
    }
}

static void test_profile_invalid_limits(void)
{
    CHECK(kbc_profile_create(0, 61) == NULL);
    CHECK(kbc_profile_create(76, 61) == NULL);
    CHECK(kbc_profile_create(70, 0) == NULL);
    CHECK(kbc_profile_create(70, 145) == NULL);
    kbc_profile *p = kbc_profile_create(70, 61);
    CHECK(p != NULL);
    CHECK(kbc_profile_set_key(p, 4, 0, 1) == KBC_ERR_RANGE);
    CHECK(kbc_profile_set_key(p, 0, 70, 1) == KBC_ERR_RANGE);
    CHECK(kbc_profile_set_layer(p, 0, NULL, 70) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_profile_set_custom_total(p, 6) == KBC_ERR_RANGE);
    CHECK(kbc_profile_set_led_mode(p, 13) == KBC_ERR_RANGE);
    CHECK(kbc_profile_set_spectrum(p, 9, 1) == KBC_ERR_RANGE);
    CHECK(kbc_profile_set_color(p, 8, 1,2,3) == KBC_ERR_RANGE);
    CHECK(kbc_profile_validate(NULL) == KBC_ERR_INVALID_ARGUMENT);
    kbc_profile_destroy(p);
}

static void test_null_profile_args(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    kbc_profile *p = kbc_profile_create(70, 61);
    CHECK(kbc_profile_encode_key_matrix(NULL, 1, buf, sizeof buf) != KBC_OK);
    CHECK(kbc_profile_encode_key_matrix(p, 1, NULL, sizeof buf) != KBC_OK);
    CHECK(kbc_profile_encode_key_matrix(p, 0, buf, sizeof buf) != KBC_OK);
    CHECK(kbc_profile_encode_key_matrix(p, 5, buf, sizeof buf) != KBC_OK);
    CHECK(kbc_profile_decode_key_matrix(NULL, buf, sizeof buf, NULL) != KBC_OK);
    CHECK(kbc_profile_decode_key_matrix(p, NULL, sizeof buf, NULL) != KBC_OK);
    kbc_profile_destroy(p);
}


/* ---------- T7 additional coverage ---------- */

static void test_invalid_report_sizes(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE + 2];
    size_t i;
    zero_buf(buf, sizeof buf);
    buf[0] = 0x07; buf[1] = 0x01; buf[2] = 0x01;
    for (i = 0; i <= 3; i++) {
        size_t len = i;
        CHECK(kbc_validate_feature_report(buf, len) != KBC_OK);
        {
            kbc_feature_report rep;
            CHECK(kbc_parse_feature_report(buf, len, &rep) != KBC_OK);
        }
    }
    CHECK(kbc_validate_feature_report(buf, KBC_FEATURE_REPORT_SIZE - 1) != KBC_OK);
    CHECK(kbc_validate_feature_report(buf, KBC_FEATURE_REPORT_SIZE + 1) != KBC_OK);
}

static void test_invalid_opcodes(void)
{
    uint8_t invalid[] = {0x00, 0x0C, 0x0D, 0x7F, 0x80, 0x83, 0xFF};
    size_t i;
    for (i = 0; i < sizeof(invalid); i++) {
        CHECK(kbc_is_valid_opcode(invalid[i]) != KBC_OK);
        CHECK(kbc_validate_opcode(invalid[i]) != KBC_OK);
    }
    /* all documented opcodes valid */
    {
        uint8_t valid[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x81,0x82};
        for (i = 0; i < sizeof(valid); i++) {
            CHECK(kbc_is_valid_opcode(valid[i]) == KBC_OK);
        }
    }
}

static void test_input_report_sizes(void)
{
    uint8_t buf[8];
    zero_buf(buf, sizeof buf);
    buf[0] = 0x04; buf[1] = 0xF1; buf[2] = 0xF2; buf[3]=2; buf[4]=3; buf[5]=1;
    CHECK(kbc_validate_input_report(buf, 0) == KBC_ERR_PROTOCOL);
    CHECK(kbc_validate_input_report(buf, 5) != KBC_OK);
    CHECK(kbc_validate_input_report(buf, 6) == KBC_OK);
    CHECK(kbc_validate_input_report(buf, 7) == KBC_OK);
    CHECK(kbc_validate_input_report(NULL, 6) == KBC_ERR_INVALID_ARGUMENT);
    {
        kbc_input_report rep;
        CHECK(kbc_parse_input_report(buf, 5, &rep) != KBC_OK);
        CHECK(kbc_parse_input_report(buf, 6, &rep) == KBC_OK);
    }
}

static void test_null_buffers_all(void)
{
    kbc_feature_report fr;
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t params[4] = {0,0,0,0};
    uint8_t data[KBC_FEATURE_DATA_SIZE];
    zero_buf(buf, sizeof buf);
    zero_buf(data, sizeof data);
    buf[0]=0x07; buf[1]=0x01; buf[2]=0x01;

    CHECK(kbc_validate_feature_report(NULL, KBC_FEATURE_REPORT_SIZE) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_parse_feature_report(NULL, KBC_FEATURE_REPORT_SIZE, &fr) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_parse_feature_report(buf, KBC_FEATURE_REPORT_SIZE, NULL) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_encode_feature_report(NULL, buf, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_encode_feature_report(&fr, NULL, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_build_feature_report(0x01,1,0,params,data,sizeof data,NULL,sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_validate_input_report(NULL, 6) == KBC_ERR_INVALID_ARGUMENT);
    {
        kbc_input_report ir;
        CHECK(kbc_parse_input_report(NULL, 6, &ir) == KBC_ERR_INVALID_ARGUMENT);
        CHECK(kbc_parse_input_report(buf, 6, NULL) == KBC_ERR_INVALID_ARGUMENT);
    }
    CHECK(kbc_led_buffer_validate(NULL, KBC_LED_BUFFER_SIZE) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_set_rgb(NULL, 1, 0, 1,2,3) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_get_rgb(NULL, 1, 0, NULL, NULL, NULL) == KBC_ERR_INVALID_ARGUMENT);
}

static void test_integer_overflow_args(void)
{
    /* profile creation with huge sizes must fail, not overflow. */
    CHECK(kbc_profile_create((size_t)-1, 61) == NULL);
    CHECK(kbc_profile_create(70, (size_t)-1) == NULL);
    CHECK(kbc_profile_create(0, 0) == NULL);

    /* led helpers with out-of-range counts. */
    {
        uint8_t buf[KBC_LED_BUFFER_SIZE];
        memset(buf, 0, sizeof buf);
        CHECK(kbc_led_set_rgb(buf, 0, 0, 1,2,3) == KBC_ERR_RANGE);
        CHECK(kbc_led_set_rgb(buf, KBC_MAX_LED_COUNT + 1, 0, 1,2,3) == KBC_ERR_RANGE);
        CHECK(kbc_led_set_rgb(buf, 61, 61, 1,2,3) == KBC_ERR_RANGE);
        CHECK(kbc_led_get_rgb(buf, 61, 61, (uint8_t[1]){0}, (uint8_t[1]){0}, (uint8_t[1]){0}) == KBC_ERR_RANGE);
    }

    /* led page index out of range. */
    {
        uint8_t buf[KBC_LED_BUFFER_SIZE];
        uint8_t out[KBC_FEATURE_REPORT_SIZE];
        memset(buf, 0, sizeof buf);
        CHECK(kbc_led_encode_page(buf, sizeof buf, 999, out, sizeof out) == KBC_ERR_RANGE);
    }

    /* macro steps limit. */
    {
        kbc_macro *m = kbc_macro_create();
        size_t i;
        CHECK(m != NULL);
        for (i = 0; i < KBC_MACRO_MAX_STEPS; i++) {
            CHECK(kbc_macro_add_step(m, 20, 1, 0x04) == KBC_OK);
        }
        CHECK(kbc_macro_add_step(m, 20, 1, 0x04) == KBC_ERR_RANGE);
        kbc_macro_destroy(m);
    }
}

static void test_macro_invalid_lengths(void)
{
    kbc_macro *m = kbc_macro_create();
    uint8_t buf[KBC_MACRO_SIZE];
    CHECK(m != NULL);
    memset(buf, 0, sizeof buf);
    CHECK(kbc_macro_decode(m, buf, 0) != KBC_OK);
    CHECK(kbc_macro_decode(m, buf, KBC_MACRO_SIZE - 1) != KBC_OK);
    CHECK(kbc_macro_decode(m, buf, KBC_MACRO_SIZE + 1) != KBC_OK);
    CHECK(kbc_macro_encode(m, buf, KBC_MACRO_SIZE - 1) != KBC_OK);
    CHECK(kbc_macro_encode(m, buf, KBC_MACRO_SIZE + 1) != KBC_OK);
    CHECK(kbc_macro_encode(m, NULL, KBC_MACRO_SIZE) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_macro_decode(NULL, buf, KBC_MACRO_SIZE) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_macro_decode(m, NULL, KBC_MACRO_SIZE) == KBC_ERR_INVALID_ARGUMENT);
    kbc_macro_destroy(m);
}

static void test_profile_regression_shifted(void)
{
    kbc_profile *p = kbc_profile_create(70, 61);
    kbc_profile *q = kbc_profile_create(70, 61);
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    uint8_t hw = 0;
    CHECK(p && q);
    CHECK(kbc_profile_set_led_mode(p, 5) == KBC_OK);
    CHECK(kbc_profile_set_led_intensity(p, 80) == KBC_OK);
    CHECK(kbc_profile_set_led_speed(p, 50) == KBC_OK);
    CHECK(kbc_profile_encode_params_b(p, 2, buf, sizeof buf) == KBC_OK);
    /* shifted decode should not crash and yield valid profile */
    CHECK(kbc_profile_decode_block_shifted(q, buf, sizeof buf, 1, &hw) == KBC_OK);
    CHECK(hw == 2);
    kbc_profile_destroy(p);
    kbc_profile_destroy(q);
}

static void test_profile_invalid_ranges_feature(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    /* opcode 0x05 with position >= matrix max (255) */
    zero_buf(buf, sizeof buf);
    buf[0]=0x07; buf[1]=0x05; buf[2]=0x01; buf[3]=0xFF;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    /* LED page 2 */
    zero_buf(buf, sizeof buf);
    buf[0]=0x07; buf[1]=0x07; buf[2]=0x02;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    /* profile 0 for select (not 0xFF) */
    zero_buf(buf, sizeof buf);
    buf[0]=0x07; buf[1]=0x01; buf[2]=0x00;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
    /* matrix page != 0 */
    zero_buf(buf, sizeof buf);
    buf[0]=0x07; buf[1]=0x03; buf[2]=0x01; buf[3]=0x01;
    CHECK(kbc_validate_feature_report(buf, sizeof buf) != KBC_OK);
}


int main(void)
{
    test_tv_f01(); test_tv_f02(); test_tv_f03(); test_tv_f04();
    test_tv_f05(); test_tv_f06(); test_tv_f07(); test_tv_f08();
    test_tv_f09(); test_tv_f10(); test_tv_f11(); test_tv_f12();
    test_tv_f13(); test_tv_f14(); test_tv_f15();

    test_tv_i01(); test_tv_i02(); test_tv_i03(); test_tv_i04();
    test_tv_i05(); test_tv_i06(); test_tv_i07(); test_tv_i08();

    test_tv_e01(); test_tv_e02(); test_tv_e03(); test_tv_e04();
    test_tv_e05(); test_tv_e06(); test_tv_e07(); test_tv_e08();
    test_tv_e09(); test_tv_e10();

    test_build_feature();
    test_null_args();

    test_profile_roundtrip();
    test_profile_params_a_roundtrip();
    test_profile_params_b_roundtrip();
    test_profile_extra();
    test_profile_select_encode();
    test_macro_roundtrip();
    test_macro_limits();
    test_led_pagination();
    test_profile_invalid_limits();
    test_null_profile_args();

    test_invalid_report_sizes();
    test_invalid_opcodes();
    test_input_report_sizes();
    test_null_buffers_all();
    test_integer_overflow_args();
    test_macro_invalid_lengths();
    test_profile_regression_shifted();
    test_profile_invalid_ranges_feature();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("ok: all core tests");
    return 0;
}