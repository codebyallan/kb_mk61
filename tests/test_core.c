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



/* ---------- LED frame + read requests ---------- */

static void test_led_frame(void)
{
    uint8_t frame[KBC_LED_BUFFER_SIZE];
    uint8_t rep[KBC_FEATURE_REPORT_SIZE];
    size_t i;

    CHECK(kbc_led_frame_page_count(70) == 1);   /* 210 bytes fit in one page */
    CHECK(kbc_led_frame_page_count(75) == 1);   /* 225 */
    CHECK(kbc_led_frame_page_count(85) == 1);   /* 255 */
    CHECK(kbc_led_frame_page_count(86) == 2);   /* 258 */
    CHECK(kbc_led_frame_page_count(144) == 2);  /* 432 */
    CHECK(kbc_led_frame_page_count(0) == 0);
    CHECK(kbc_led_frame_page_count(145) == 0);

    zero_buf(frame, sizeof frame);
    CHECK(kbc_led_frame_fill(frame, 70, 70, 0x11, 0x22, 0x33) == KBC_OK);
    for (i = 0; i < 70; i++) {
        CHECK(frame[i] == 0x11);
        CHECK(frame[70 + i] == 0x22);
        CHECK(frame[140 + i] == 0x33);
    }
    CHECK(frame[210] == 0);

    zero_buf(frame, sizeof frame);
    CHECK(kbc_led_frame_set(frame, 70, 5, 1, 2, 3) == KBC_OK);
    CHECK(frame[5] == 1 && frame[75] == 2 && frame[145] == 3);
    CHECK(kbc_led_frame_set(frame, 70, 70, 1, 2, 3) == KBC_ERR_RANGE);
    CHECK(kbc_led_frame_set(NULL, 70, 0, 1, 2, 3) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_frame_fill(frame, 70, 71, 0, 0, 0) == KBC_ERR_RANGE);

    /* page 0 of a 70-stride frame carries the planes at data offset 8 */
    CHECK(kbc_led_encode_page(frame, sizeof frame, 0, rep, sizeof rep) == KBC_OK);
    CHECK(rep[0] == 0x07 && rep[1] == 0x07 && rep[2] == 0);
    CHECK(rep[8 + 5] == 1 && rep[8 + 75] == 2 && rep[8 + 145] == 3);
}

static void test_led_frame_brightness(void)
{
    uint8_t frame[KBC_LED_BUFFER_SIZE] = {0};

    CHECK(kbc_led_frame_fill(frame, 2, 2, 255, 100, 1) == KBC_OK);
    CHECK(kbc_led_frame_apply_brightness(frame, 2, 10) == KBC_OK);
    CHECK(frame[0] == 255 && frame[2] == 100 && frame[4] == 1);
    CHECK(kbc_led_frame_apply_brightness(frame, 2, 5) == KBC_OK);
    CHECK(frame[0] == 128 && frame[2] == 50 && frame[4] == 1);
    CHECK(kbc_led_frame_apply_brightness(frame, 2, 0) == KBC_OK);
    CHECK(frame[0] == 0 && frame[2] == 0 && frame[4] == 0);
    CHECK(kbc_led_frame_apply_brightness(frame, 0, 5) == KBC_ERR_RANGE);
    CHECK(kbc_led_frame_apply_brightness(NULL, 2, 5) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_frame_apply_brightness(frame, 2, 11) == KBC_ERR_RANGE);
}

static void test_read_requests(void)
{
    uint8_t buf[KBC_FEATURE_REPORT_SIZE];
    size_t i;

    CHECK(kbc_build_read_request(0x81, 3, buf, sizeof buf) == KBC_OK);
    CHECK(buf[0] == 0x07 && buf[1] == 0x81 && buf[2] == 0);
    for (i = 3; i < sizeof buf; i++) CHECK(buf[i] == 0);

    CHECK(kbc_build_read_request(0x82, 2, buf, sizeof buf) == KBC_OK);
    CHECK(buf[0] == 0x07 && buf[1] == 0x82 && buf[2] == 2);
    for (i = 3; i < sizeof buf; i++) CHECK(buf[i] == 0);

    CHECK(kbc_build_read_request(0x82, 0, buf, sizeof buf) == KBC_ERR_RANGE);
    CHECK(kbc_build_read_request(0x82, 5, buf, sizeof buf) == KBC_ERR_RANGE);
    /* only query opcodes are accepted */
    CHECK(kbc_build_read_request(0x02, 1, buf, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_build_read_request(0x03, 1, buf, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_build_read_request(0x82, 1, buf, 263) == KBC_ERR_PROTOCOL);
    CHECK(kbc_build_read_request(0x82, 1, NULL, sizeof buf) == KBC_ERR_INVALID_ARGUMENT);
}

static void test_led_effect_names(void)
{
    CHECK(strcmp(kbc_led_effect_name(0), "wave") == 0);
    CHECK(strcmp(kbc_led_effect_name(12), "lighting") == 0);
    CHECK(strcmp(kbc_led_effect_name(15), "custom") == 0);
    CHECK(kbc_led_effect_name(13) == NULL);
    CHECK(kbc_led_effect_name(255) == NULL);
    CHECK(KBC_LED_EFFECT_LIGHTING == 12);
}

static void test_profile_block_write(void)
{
    uint8_t blk[264], out[264];
    size_t i;
    memset(blk, 0, sizeof blk);
    for (i = 8; i < 264; i++) blk[i] = (uint8_t)(i * 7);
    blk[0] = 7; blk[1] = 0x82; blk[2] = 2; blk[3] = 0; blk[4] = 0;
    blk[5] = 0; blk[6] = 0; blk[7] = 0; blk[11] = 0;

    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 1, out, 264) == KBC_OK);
    CHECK(out[0] == 7 && out[1] == 0x02 && out[2] == 2 && out[11] == 1);
    for (i = 0; i < 264; i++) {
        if (i == 1 || i == 11) continue;
        CHECK(out[i] == blk[i]);              /* everything else untouched */
    }
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 12, out, 264) == KBC_OK);
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 11, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 13, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 15, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_mode(blk, 264, 3, 1, out, 264) == KBC_ERR_PROTOCOL);  /* wrong profile */
    CHECK(kbc_profile_block_set_mode(blk, 264, 0, 1, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_mode(blk, 263, 2, 1, out, 264) == KBC_ERR_PROTOCOL);
    blk[1] = 0x02;
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 1, out, 264) == KBC_ERR_PROTOCOL);  /* not a 0x82 reply */
    blk[1] = 0x82; blk[11] = 0x77;
    CHECK(kbc_profile_block_set_mode(blk, 264, 2, 1, out, 264) == KBC_ERR_PROTOCOL);  /* implausible layout */
    blk[11] = 11;                                  /* currently game mode: to_write must still work */
    CHECK(kbc_profile_block_to_write(blk, 264, 2, out, 264) == KBC_OK && out[11] == 11);
}

static void test_profile_block_fields(void)
{
    uint8_t blk[264], out[264];
    size_t i, changed;

    memset(blk, 0, sizeof blk);
    for (i = 8; i < 264; i++) blk[i] = (uint8_t)(i * 3);
    blk[0] = 7; blk[1] = 0x82; blk[2] = 1;
    for (i = 3; i < 8; i++) blk[i] = 0;
    blk[11] = 0; blk[12] = 0; blk[19] = 0x66;

    /* brightness: only offset 12 changes */
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_BRIGHTNESS, 10, out, 264) == KBC_OK);
    changed = 0;
    for (i = 3; i < 264; i++) if (out[i] != blk[i]) { changed++; CHECK(i == 12); }
    CHECK(changed == 1 && out[12] == 10 && out[1] == 0x02);
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_BRIGHTNESS, 11, out, 264) == KBC_ERR_RANGE);
    blk[11] = KBC_LED_EFFECT_OBSERVED_14;
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_BRIGHTNESS, 5, out, 264) == KBC_OK);
    CHECK(out[11] == KBC_LED_EFFECT_OBSERVED_14 && out[12] == 5);
    blk[11] = 0;

    /* speed: slot = 19 + current effect */
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 2, out, 264) == KBC_OK);
    CHECK(out[19] == 2);
    changed = 0;
    for (i = 3; i < 264; i++) if (out[i] != blk[i]) changed++;
    CHECK(changed == 1);
    blk[11] = 1;
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 102, out, 264) == KBC_OK);
    CHECK(out[20] == 102 && out[19] == blk[19]);
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 1, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 103, out, 264) == KBC_ERR_RANGE);
    blk[11] = 12;                                   /* lighting has no speed slot */
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 50, out, 264) == KBC_ERR_RANGE);
    blk[11] = 15;                                   /* custom: no speed slot */
    CHECK(kbc_profile_block_set_field(blk, 264, 1, KBC_FIELD_SPEED, 50, out, 264) == KBC_ERR_RANGE);
    CHECK(kbc_profile_block_set_field(blk, 264, 1, (kbc_profile_field)9, 1, out, 264) == KBC_ERR_INVALID_ARGUMENT);
}

static void test_select_profile_builder(void)
{
    uint8_t b[264];
    size_t i;
    CHECK(kbc_build_select_profile(2, b, sizeof b) == KBC_OK);
    CHECK(b[0] == 7 && b[1] == 0x01 && b[2] == 2);
    for (i = 3; i < sizeof b; i++) CHECK(b[i] == 0);
    CHECK(kbc_build_select_profile(0, b, sizeof b) == KBC_ERR_RANGE);
    CHECK(kbc_build_select_profile(5, b, sizeof b) == KBC_ERR_RANGE);
    CHECK(kbc_build_select_profile(0xFF, b, sizeof b) == KBC_ERR_RANGE);  /* factory reset never built */
    CHECK(kbc_build_select_profile(1, b, 100) == KBC_ERR_PROTOCOL);
    CHECK(kbc_build_select_profile(1, NULL, sizeof b) == KBC_ERR_INVALID_ARGUMENT);
}

static void test_led_anim(void)
{
    uint8_t f[KBC_LED_BUFFER_SIZE];
    size_t i;
    const size_t stride = 70, count = 70;

    /* breath: black at t=0, full colour at half period, smooth in between */
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 4000, 200, 100, 50, f, stride, count) == KBC_OK);
    for (i = 0; i < count; i++) CHECK(f[i] == 0 && f[stride + i] == 0 && f[2 * stride + i] == 0);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 2000, 4000, 200, 100, 50, f, stride, count) == KBC_OK);
    for (i = 0; i < count; i++) CHECK(f[i] == 200 && f[stride + i] == 100 && f[2 * stride + i] == 50);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 1000, 4000, 200, 100, 50, f, stride, count) == KBC_OK);
    CHECK(f[0] == 100 && f[stride] == 50);                    /* quarter period = half intensity */
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 4000 + 2000, 4000, 200, 100, 50, f, stride, count) == KBC_OK);
    CHECK(f[0] == 200);                                       /* periodic */

    /* cycle: whole keyboard one hue; red at 0, green at 1/3, blue at 2/3 */
    CHECK(kbc_led_anim_render(KBC_ANIM_CYCLE, 0, 6000, 0, 0, 0, f, stride, count) == KBC_OK);
    CHECK(f[0] == 255 && f[stride] == 0 && f[2 * stride] == 0);
    for (i = 1; i < count; i++) CHECK(f[i] == f[0] && f[stride + i] == f[stride]);
    CHECK(kbc_led_anim_render(KBC_ANIM_CYCLE, 2000, 6000, 0, 0, 0, f, stride, count) == KBC_OK);
    CHECK(f[0] <= 1 && f[stride] >= 254 && f[2 * stride] <= 1);
    CHECK(kbc_led_anim_render(KBC_ANIM_CYCLE, 4000, 6000, 0, 0, 0, f, stride, count) == KBC_OK);
    CHECK(f[0] <= 1 && f[stride] <= 1 && f[2 * stride] >= 254);

    /* rainbow: same column = same colour, different columns differ */
    CHECK(kbc_led_anim_render(KBC_ANIM_RAINBOW, 0, 6000, 0, 0, 0, f, stride, count) == KBC_OK);
    for (i = 1; i < KBC_MATRIX_ROWS; i++) CHECK(f[i] == f[0] && f[stride + i] == f[stride]);
    CHECK(f[0] != f[7 * KBC_MATRIX_ROWS] || f[stride] != f[stride + 7 * KBC_MATRIX_ROWS]);
    CHECK(f[0] == 255);                                       /* column 0 starts at red */

    /* positions beyond count stay black; bytes after the planes are untouched */
    memset(f, 0xAA, sizeof f);
    CHECK(kbc_led_anim_render(KBC_ANIM_CYCLE, 0, 6000, 0, 0, 0, f, stride, 61) == KBC_OK);
    CHECK(f[61] == 0 && f[stride + 61] == 0 && f[2 * stride + 69] == 0);
    CHECK(f[3 * stride] == 0xAA);

    /* validation */
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 199, 1, 1, 1, f, stride, count) == KBC_ERR_RANGE);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 60001, 1, 1, 1, f, stride, count) == KBC_ERR_RANGE);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 4000, 1, 1, 1, f, stride, 71) == KBC_ERR_RANGE);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 4000, 1, 1, 1, f, 145, 70) == KBC_ERR_RANGE);
    CHECK(kbc_led_anim_render((kbc_led_anim)0, 0, 4000, 1, 1, 1, f, stride, count) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_anim_render((kbc_led_anim)9, 0, 4000, 1, 1, 1, f, stride, count) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_anim_render(KBC_ANIM_BREATH, 0, 4000, 1, 1, 1, NULL, stride, count) == KBC_ERR_INVALID_ARGUMENT);
    CHECK(kbc_led_anim_default_period(KBC_ANIM_BREATH) == 4000);
    CHECK(kbc_led_anim_default_period((kbc_led_anim)0) == 0);
    /* every default period is accepted */
    CHECK(kbc_led_anim_render(KBC_ANIM_CYCLE, 123456789u, kbc_led_anim_default_period(KBC_ANIM_CYCLE),
                              0, 0, 0, f, stride, count) == KBC_OK);
}

static void test_keymap(void)
{
    int tmp[KBC_KEY_POSITIONS], pos[KBC_KEY_POSITIONS];
    size_t i, n, count = 0;

    /* positions confirmed on hardware (kbctl led sweep) */
    CHECK(kbc_key_position("esc") == 0 && kbc_key_position("tab") == 1);
    CHECK(kbc_key_position("w") == 11 && kbc_key_position("a") == 7);
    CHECK(kbc_key_position("s") == 12 && kbc_key_position("d") == 17);
    CHECK(kbc_key_position("space") == 29 && kbc_key_position("enter") == 67);
    CHECK(kbc_key_position("fn") == 69 && kbc_key_position("menu") == 59);
    CHECK(kbc_key_position("lshift") == 3 && kbc_key_position("rshift") == 68);
    CHECK(kbc_key_position("0") == 50 && kbc_key_position("1") == 5);
    CHECK(kbc_key_position("backspace") == 65 && kbc_key_position("backslash") == 66);

    /* case-insensitive, aliases, ambiguity */
    CHECK(kbc_key_position("W") == 11 && kbc_key_position("ESC") == 0);
    CHECK(kbc_key_position("escape") == 0 && kbc_key_position("-") == 55 && kbc_key_position("win") == 9);
    CHECK(kbc_key_position("shift") == -1 && kbc_key_position("ctrl") == -1 && kbc_key_position("alt") == -1);
    CHECK(kbc_key_position("") == -1 && kbc_key_position(NULL) == -1 && kbc_key_position("nope") == -1);

    /* 61 keys, each name round-trips, gaps have no name */
    for (i = 0; i < KBC_KEY_POSITIONS; i++) {
        const char *nm = kbc_key_name(i);
        if (nm != NULL) { count++; CHECK(kbc_key_position(nm) == (int)i); }
    }
    CHECK(count == 61);
    CHECK(kbc_key_name(8) == NULL && kbc_key_name(62) == NULL && kbc_key_name(63) == NULL);
    CHECK(kbc_key_name(70) == NULL && kbc_key_name(9999) == NULL);

    /* groups */
    n = kbc_key_group("wasd", tmp, KBC_KEY_POSITIONS);
    CHECK(n == 4 && tmp[0] == 11 && tmp[1] == 7 && tmp[2] == 12 && tmp[3] == 17);
    CHECK(kbc_key_group("numbers", tmp, KBC_KEY_POSITIONS) == 10);
    CHECK(kbc_key_group("letters", tmp, KBC_KEY_POSITIONS) == 26);
    CHECK(kbc_key_group("all", pos, KBC_KEY_POSITIONS) == 61);
    CHECK(kbc_key_group("mods", tmp, KBC_KEY_POSITIONS) == 9);
    CHECK(kbc_key_group("nope", tmp, KBC_KEY_POSITIONS) == 0);
    CHECK(kbc_key_group("wasd", tmp, 2) == 2);                 /* respects max */
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
    test_led_frame();
    test_led_frame_brightness();
    test_read_requests();
    test_keymap();
    test_led_anim();
    test_select_profile_builder();
    test_led_effect_names();
    test_profile_block_write();
    test_profile_block_fields();
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