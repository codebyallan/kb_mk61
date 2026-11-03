#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/daemon/kbdd.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static void defaults_check(const kbdd_ctx_t *k)
{
    CHECK(k->config_mode == KBDD_CONFIG_NONE);
    CHECK(k->config_effect == 0);
    CHECK(k->config_brightness == 10);
    CHECK(k->config_stride == 0);
    CHECK(k->config_anim == 0);
    CHECK(k->config_anim_period == 0);
    CHECK(k->config_anim_rgb[0] == 0 && k->config_anim_rgb[1] == 0 && k->config_anim_rgb[2] == 0);
    for (size_t i = 0; i < KBC_LED_EFFECT_COUNT; i++) {
        CHECK(k->config_speed[i] == 50);
    }
}

static int load_temp(const char *contents, kbdd_ctx_t *k)
{
    char path[] = "/tmp/kbdd-config-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) {
        return -1;
    }

    const char *p = contents;
    size_t left = strlen(contents);
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) {
            close(fd);
            unlink(path);
            CHECK(0);
            return -2;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    close(fd);

    int rc = kbdd_load_config_file(k, path);
    unlink(path);
    return rc;
}

static void test_missing_file_defaults(void)
{
    kbdd_ctx_t k;
    kbdd_config_defaults(&k);
    CHECK(kbdd_load_config_file(&k, "/tmp/kbdd-config-test-file-does-not-exist-9f0b5c1e") == 0);
    defaults_check(&k);
}

static void test_empty_file_defaults(void)
{
    kbdd_ctx_t k;
    CHECK(load_temp("", &k) == 0);
    defaults_check(&k);
}

static void test_valid_full_config(void)
{
    kbdd_ctx_t k;
    const char *cfg =
        "mode 2\n"
        "effect 12\n"
        "brightness 7\n"
        "speed 12 42\n"
        "solid 10 20 30\n"
        "stride 70\n"
        "pos 0 40 50 60\n"
        "pos 10 70 80 90\n"
        "pos 60 100 110 120\n";

    CHECK(load_temp(cfg, &k) == 0);
    CHECK(k.config_mode == KBDD_CONFIG_SOLID);
    CHECK(k.config_effect == 12);
    CHECK(k.config_brightness == 7);
    CHECK(k.config_speed[12] == 42);
    CHECK(k.config_solid[0] == 10 && k.config_solid[1] == 20 && k.config_solid[2] == 30);
    CHECK(k.config_stride == 70);
    CHECK(k.config_positions[0][0] == 40 && k.config_positions[0][1] == 50 && k.config_positions[0][2] == 60);
    CHECK(k.config_positions[10][0] == 70 && k.config_positions[10][1] == 80 && k.config_positions[10][2] == 90);
    CHECK(k.config_positions[60][0] == 100 && k.config_positions[60][1] == 110 && k.config_positions[60][2] == 120);
    /* defaults for non-written slots */
    CHECK(k.config_speed[0] == 50);
}

static void test_valid_animation_config(void)
{
    kbdd_ctx_t k;
    const char *cfg =
        "mode 4\n"
        "animation 2 4000 10 20 30\n"
        "brightness 10\n";

    CHECK(load_temp(cfg, &k) == 0);
    CHECK(k.config_mode == KBDD_CONFIG_ANIMATION);
    CHECK(k.config_anim == KBC_ANIM_CYCLE);
    CHECK(k.config_anim_period == 4000);
    CHECK(k.config_anim_rgb[0] == 10 && k.config_anim_rgb[1] == 20 && k.config_anim_rgb[2] == 30);
}

static void test_invalid_cases(void)
{
    static const char * const cases[] = {
        "mode 5\n",                       /* invalid mode */
        "effect 13\n",                    /* effect out of range */
        "effect foo\n",                   /* effect not a number */
        "brightness 11\n",                /* brightness > max */
        "animation 1 199 10 20 30\n",     /* period below min */
        "animation 1 60001 10 20 30\n",   /* period above max */
        "animation 1 4000 300 20 30\n",   /* red > 255 */
        "speed 13 2\n",                   /* speed effect >= effect count */
        "speed 1 1\n",                    /* speed below min */
        "speed 1 103\n",                  /* speed above max */
        "solid 10 20 256\n",              /* blue > 255 */
        "stride 76\n",                    /* stride > max matrix size */
        "pos 75 10 20 30\n",              /* pos out of range */
        "unknown 1\n",                    /* unknown key */
        "mode\n",                         /* missing value */
        "solid 1 2\n",                    /* missing value */
        "pos 0 1 2\n",                    /* missing value */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        kbdd_ctx_t k;
        memset(&k, 0, sizeof(k));
        int rc = load_temp(cases[i], &k);
        if (rc != -1) {
            fprintf(stderr, "invalid case %u unexpectedly succeeded\n", (unsigned)i);
            failures++;
        }
        defaults_check(&k);
    }
}

static void test_boundary_cases(void)
{
    kbdd_ctx_t k;

    CHECK(load_temp("effect 0\n", &k) == 0);
    CHECK(k.config_effect == 0);

    CHECK(load_temp("effect 12\n", &k) == 0);
    CHECK(k.config_effect == 12);

    CHECK(load_temp("stride 0\n", &k) == 0);
    CHECK(k.config_stride == 0);

    CHECK(load_temp("stride 75\n", &k) == 0);
    CHECK(k.config_stride == 75);

    CHECK(load_temp("speed 0 2\n", &k) == 0);
    CHECK(k.config_speed[0] == 2);

    CHECK(load_temp("speed 12 102\n", &k) == 0);
    CHECK(k.config_speed[12] == 102);

    CHECK(load_temp("pos 74 255 255 255\n", &k) == 0);
    CHECK(k.config_positions[74][0] == 255 &&
          k.config_positions[74][1] == 255 &&
          k.config_positions[74][2] == 255);
}

int main(void)
{
    test_missing_file_defaults();
    test_empty_file_defaults();
    test_valid_full_config();
    test_valid_animation_config();
    test_invalid_cases();
    test_boundary_cases();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all config tests");
    return 0;
}
