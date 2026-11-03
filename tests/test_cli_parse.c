#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cli/kbctl_parse.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void test_parse_index(void)
{
    long v = 0;
    CHECK(kbctl_parse_index("1", &v) == 0 && v == 1);
    CHECK(kbctl_parse_index("2", &v) == 0 && v == 2);
    CHECK(kbctl_parse_index("3", &v) == 0 && v == 3);
    CHECK(kbctl_parse_index("4", &v) == 0 && v == 4);
    CHECK(kbctl_parse_index("0", &v) != 0);
    CHECK(kbctl_parse_index("5", &v) != 0);
    CHECK(kbctl_parse_index("1x", &v) != 0);
    CHECK(kbctl_parse_index("x1", &v) != 0);
    CHECK(kbctl_parse_index("", &v) != 0);
    CHECK(kbctl_parse_index(NULL, &v) != 0);
    CHECK(kbctl_parse_index(" 1", &v) != 0);
    CHECK(kbctl_parse_index("01\n", &v) != 0);
    CHECK(kbctl_parse_index("0003", &v) == 0 && v == 3);
}

static void test_parse_rgb(void)
{
    uint8_t r = 0, g = 0, b = 0;
    CHECK(kbctl_parse_rgb("000000", &r, &g, &b) == 0 && r == 0 && g == 0 && b == 0);
    CHECK(kbctl_parse_rgb("ffffff", &r, &g, &b) == 0 && r == 255 && g == 255 && b == 255);
    CHECK(kbctl_parse_rgb("FFFFFF", &r, &g, &b) == 0 && r == 255 && g == 255 && b == 255);
    CHECK(kbctl_parse_rgb("#ff0000", &r, &g, &b) == 0 && r == 255 && g == 0 && b == 0);
    CHECK(kbctl_parse_rgb("00ff00", &r, &g, &b) == 0 && r == 0 && g == 255 && b == 0);
    CHECK(kbctl_parse_rgb("0000FF", &r, &g, &b) == 0 && r == 0 && g == 0 && b == 255);
    CHECK(kbctl_parse_rgb("1F7C3A", &r, &g, &b) == 0 && r == 0x1f && g == 0x7c && b == 0x3a);
    CHECK(kbctl_parse_rgb("gggggg", &r, &g, &b) != 0);
    CHECK(kbctl_parse_rgb("12345", &r, &g, &b) != 0);
    CHECK(kbctl_parse_rgb("1234567", &r, &g, &b) != 0);
    CHECK(kbctl_parse_rgb("", &r, &g, &b) != 0);
    CHECK(kbctl_parse_rgb(NULL, &r, &g, &b) != 0);
}

static void test_parse_uint(void)
{
    unsigned long v = 0;
    CHECK(kbctl_parse_uint("0", 255, &v) == 0 && v == 0);
    CHECK(kbctl_parse_uint("10", 255, &v) == 0 && v == 10);
    CHECK(kbctl_parse_uint("255", 255, &v) == 0 && v == 255);
    CHECK(kbctl_parse_uint("256", 255, &v) != 0);
    CHECK(kbctl_parse_uint("0007", 10, &v) == 0 && v == 7);
    CHECK(kbctl_parse_uint("1.0", 10, &v) != 0);
    CHECK(kbctl_parse_uint("10x", 10, &v) != 0);
    CHECK(kbctl_parse_uint("x1", 10, &v) != 0);
    CHECK(kbctl_parse_uint("", 10, &v) != 0);
    CHECK(kbctl_parse_uint(NULL, 10, &v) != 0);
    CHECK(kbctl_parse_uint("18446744073709551615", 1024, &v) != 0); /* overflow */
    CHECK(kbctl_parse_uint("18446744073709551615", 18446744073709551615UL, &v) == 0);
}

int main(void)
{
    test_parse_index();
    test_parse_rgb();
    test_parse_uint();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all cli parser tests");
    return 0;
}