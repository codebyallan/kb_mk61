#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    CHECK(k->config_led_direction_set == 0);
    CHECK(k->config_report_rate_set == 0);
    CHECK(k->config_opts_set == 0);
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

/* mode 5 = keyboard-owned custom LED: valid, and preserved by a save/load. */
static void test_valid_custom_config(void)
{
    char dir[] = "/tmp/kbdd-config-custom-XXXXXX";
    char path[512];
    kbdd_ctx_t k, r;

    CHECK(load_temp("mode 5\neffect 12\nbrightness 9\n", &k) == 0);
    CHECK(k.config_mode == KBDD_CONFIG_CUSTOM);
    CHECK(k.config_effect == 12);
    CHECK(k.config_brightness == 9);

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    CHECK(kbdd_save_config(&k) == 0);
    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_mode == KBDD_CONFIG_CUSTOM);
    CHECK(r.config_effect == 12);
    CHECK(r.config_brightness == 9);

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

static void test_invalid_cases(void)
{
    static const char * const cases[] = {
        "mode 6\n",                       /* invalid mode */
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

/* 0x0B opts: only 0 or 1 are accepted. */
static void test_valid_opts_config(void)
{
    kbdd_ctx_t k;

    CHECK(load_temp("direction 1\nwin_lock 0\n", &k) == 0);
    CHECK(k.config_opts_direction == 1);
    CHECK(k.config_opts_win_lock == 0);

    CHECK(load_temp("direction 0\nwin_lock 1\n", &k) == 0);
    CHECK(k.config_opts_direction == 0);
    CHECK(k.config_opts_win_lock == 1);

    /* defaults when absent */
    CHECK(load_temp("brightness 7\n", &k) == 0);
    CHECK(k.config_opts_direction == 0);
    CHECK(k.config_opts_win_lock == 0);
}

static void test_invalid_opts_config(void)
{
    static const char * const cases[] = {
        "direction 2\n",         /* > 1 */
        "win_lock 3\n",          /* > 1 */
        "direction\n",           /* missing value */
        "win_lock\n",            /* missing value */
        "direction abc\n",       /* not a number */
        "win_lock -1\n",         /* negative */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        kbdd_ctx_t k;
        memset(&k, 0, sizeof(k));
        int rc = load_temp(cases[i], &k);
        if (rc != -1) {
            fprintf(stderr, "invalid opts case %u unexpectedly succeeded\n",
                    (unsigned)i);
            failures++;
        }
        /* defaults must be preserved on failure */
        CHECK(k.config_opts_direction == 0);
        CHECK(k.config_opts_win_lock == 0);
    }
}

/* opts_set flag: explicit marker that 'led opts' was used. */
static void test_valid_opts_set_config(void)
{
    kbdd_ctx_t k;

    CHECK(load_temp("opts_set 1\n", &k) == 0);
    CHECK(k.config_opts_set == 1);

    CHECK(load_temp("opts_set 0\n", &k) == 0);
    CHECK(k.config_opts_set == 0);

    /* non-default opts imply set, even without the marker (back-compat) */
    CHECK(load_temp("direction 1\nwin_lock 0\n", &k) == 0);
    CHECK(k.config_opts_set == 1);

    /* absent marker and default opts stay unset */
    CHECK(load_temp("brightness 7\n", &k) == 0);
    CHECK(k.config_opts_set == 0);
}

static void test_invalid_opts_set_config(void)
{
    static const char * const cases[] = {
        "opts_set 2\n",
        "opts_set -1\n",
        "opts_set abc\n",
        "opts_set\n",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        kbdd_ctx_t k;
        memset(&k, 0, sizeof(k));
        int rc = load_temp(cases[i], &k);
        if (rc != -1) {
            fprintf(stderr, "invalid opts_set case %u unexpectedly succeeded\n",
                    (unsigned)i);
            failures++;
        }
        CHECK(k.config_opts_set == 0);
    }
}

/* Save/load round-trip for 0x0B opts. */
static void test_save_load_opts_roundtrip(void)
{
    char dir[] = "/tmp/kbdd-config-opts-XXXXXX";
    char path[512];
    kbdd_ctx_t k, r;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    k.config_mode = KBDD_CONFIG_SOLID;
    k.config_solid[0] = 1; k.config_solid[1] = 2; k.config_solid[2] = 3;
    k.config_opts_direction = 1;
    k.config_opts_win_lock = 1;
    k.config_opts_set = 1;
    CHECK(kbdd_save_config(&k) == 0);

    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_opts_direction == 1);
    CHECK(r.config_opts_win_lock == 1);
    CHECK(r.config_opts_set == 1);
    CHECK(r.config_mode == KBDD_CONFIG_SOLID);
    CHECK(r.config_solid[0] == 1 && r.config_solid[1] == 2 && r.config_solid[2] == 3);

    /* Verify the on-disk lines exist. */
    {
        FILE *f = fopen(path, "r");
        char line[64];
        int saw_direction = 0, saw_win_lock = 0, saw_opts_set = 0;
        CHECK(f != NULL);
        while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
            if (strcmp(line, "direction 1\n") == 0) saw_direction = 1;
            if (strcmp(line, "win_lock 1\n") == 0) saw_win_lock = 1;
            if (strcmp(line, "opts_set 1\n") == 0) saw_opts_set = 1;
        }
        if (f != NULL) fclose(f);
        CHECK(saw_direction == 1);
        CHECK(saw_win_lock == 1);
    CHECK(saw_opts_set == 1);
    }

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

/* Animation direction (0..3) and report-rate index (0..254). */
static void test_valid_led_field_config(void)
{
    kbdd_ctx_t k;

    CHECK(load_temp("led_direction 0\nreport_rate 0\n", &k) == 0);
    CHECK(k.config_led_direction == 0 && k.config_led_direction_set == 1);
    CHECK(k.config_report_rate == 0 && k.config_report_rate_set == 1);

    CHECK(load_temp("led_direction 3\nreport_rate 254\n", &k) == 0);
    CHECK(k.config_led_direction == 3);
    CHECK(k.config_report_rate == 254);

    /* absent keys stay unset, so a restore does not force the defaults */
    CHECK(load_temp("brightness 7\n", &k) == 0);
    CHECK(k.config_led_direction_set == 0);
    CHECK(k.config_report_rate_set == 0);
}

static void test_invalid_led_field_config(void)
{
    static const char * const cases[] = {
        "led_direction 4\n",   /* > 3 */
        "report_rate 255\n",   /* > 254 */
        "led_direction\n",     /* missing value */
        "report_rate\n",       /* missing value */
        "led_direction abc\n", /* not a number */
        "report_rate -1\n",    /* negative */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        kbdd_ctx_t k;
        memset(&k, 0, sizeof(k));
        int rc = load_temp(cases[i], &k);
        if (rc != -1) {
            fprintf(stderr, "invalid LED field case %u unexpectedly succeeded\n",
                    (unsigned)i);
            failures++;
        }
        CHECK(k.config_led_direction_set == 0);
        CHECK(k.config_report_rate_set == 0);
    }
}

/* Save/load round-trip for direction/report rate. */
static void test_save_load_led_fields_roundtrip(void)
{
    char dir[] = "/tmp/kbdd-config-ledfields-XXXXXX";
    char path[512], line[128];
    kbdd_ctx_t k, r;
    int saw_direction = 0, saw_rate = 0;
    FILE *f;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    k.config_led_direction = 2;
    k.config_led_direction_set = 1;
    k.config_report_rate = 7;
    k.config_report_rate_set = 1;
    CHECK(kbdd_save_config(&k) == 0);

    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    f = fopen(path, "r");
    CHECK(f != NULL);
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strcmp(line, "led_direction 2\n") == 0) saw_direction = 1;
        if (strcmp(line, "report_rate 7\n") == 0) saw_rate = 1;
    }
    if (f != NULL) fclose(f);
    CHECK(saw_direction == 1);
    CHECK(saw_rate == 1);

    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_led_direction == 2 && r.config_led_direction_set == 1);
    CHECK(r.config_report_rate == 7 && r.config_report_rate_set == 1);

    /* Unset fields are not written, so a fresh config does not override them. */
    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    CHECK(kbdd_save_config(&k) == 0);
    saw_direction = saw_rate = 0;
    f = fopen(path, "r");
    CHECK(f != NULL);
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "led_direction ", 14) == 0) saw_direction++;
        if (strncmp(line, "report_rate ", 12) == 0) saw_rate++;
    }
    if (f != NULL) fclose(f);
    CHECK(saw_direction == 0);
    CHECK(saw_rate == 0);

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

/* 'led color' palettes: save/load round-trip and rejection of bad lines. */
static void test_colors_roundtrip(void)
{
    char dir[] = "/tmp/kbdd-config-colors-XXXXXX";
    char path[512];
    kbdd_ctx_t k, r;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    CHECK(kbdd_config_has_colors(&k) == 0);
    k.config_color_count[1] = 2;                 /* Breath: 2 colours */
    k.config_colors[1][0][0] = 255;
    k.config_colors[1][1][2] = 128;
    k.config_color_count[0] = 14;                /* Wave: 14 colours */
    for (int i = 0; i < 14; i++) k.config_colors[0][i][1] = (uint8_t)(i * 10);
    CHECK(kbdd_config_has_colors(&k) == 1);
    CHECK(kbdd_save_config(&k) == 0);

    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_color_count[1] == 2 && r.config_colors[1][0][0] == 255 &&
          r.config_colors[1][1][2] == 128);
    CHECK(r.config_color_count[0] == 14 && r.config_colors[0][13][1] == 130);
    CHECK(r.config_color_count[3] == 0);

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");

    CHECK(load_temp("color 1 3 1 2 3 4 5 6 7 8 9\n", &k) != 0);  /* Breath holds 2 */
    CHECK(load_temp("color 99 1 1 2 3\n", &k) != 0);             /* bad effect */
    CHECK(load_temp("color 1 1 1 2 300\n", &k) != 0);            /* bad channel */
    CHECK(load_temp("color 1 2 1 2 3\n", &k) != 0);              /* truncated */
}

/* save_config must write the animation line once, not once per speed entry. */
static void test_save_writes_animation_once(void)
{
    char dir[] = "/tmp/kbdd-config-save-XXXXXX";
    char path[512], line[256];
    kbdd_ctx_t k, r;
    int anim = 0, speed = 0;
    FILE *f;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    k.config_mode = KBDD_CONFIG_ANIMATION;
    k.config_anim = KBC_ANIM_CYCLE;
    k.config_anim_period = 4000;
    k.config_anim_rgb[0] = 10; k.config_anim_rgb[1] = 20; k.config_anim_rgb[2] = 30;
    CHECK(kbdd_save_config(&k) == 0);

    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    f = fopen(path, "r");
    CHECK(f != NULL);
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "animation ", 10) == 0) anim++;
        if (strncmp(line, "speed ", 6) == 0) speed++;
    }
    if (f != NULL) fclose(f);
    CHECK(anim == 1);
    CHECK(speed == KBC_LED_EFFECT_COUNT);

    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_mode == KBDD_CONFIG_ANIMATION && r.config_anim == KBC_ANIM_CYCLE);
    CHECK(r.config_anim_period == 4000 && r.config_anim_rgb[2] == 30);

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

/* A newer version's keys are ignored, not fatal, and survive a save. */
static void test_unknown_keys_tolerated_and_preserved(void)
{
    char dir[] = "/tmp/kbdd-config-unk-XXXXXX";
    char path[512], line[256];
    kbdd_ctx_t k, r;
    int kept = 0;
    FILE *f;

    memset(&k, 0, sizeof(k));
    CHECK(load_temp("mode 2\nfuture_key 1 two 3\nbrightness 7\n", &k) == 0);
    CHECK(k.config_mode == KBDD_CONFIG_SOLID && k.config_brightness == 7);

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    CHECK(kbdd_save_config(&k) == 0);
    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    f = fopen(path, "r");
    CHECK(f != NULL);
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strcmp(line, "future_key 1 two 3\n") == 0) kept++;
    }
    if (f != NULL) fclose(f);
    CHECK(kept == 1);
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_extra_len == k.config_extra_len);   /* no duplication on reload */
    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

/* Genuinely invalid files are set aside, not overwritten by the next save. */
static void test_invalid_config_is_kept(void)
{
    char dir[] = "/tmp/kbdd-config-bad-XXXXXX";
    char cfg[600], bad[700], line[64];
    kbdd_ctx_t k;
    FILE *f;

    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    snprintf(cfg, sizeof(cfg), "%s/kbdd", dir);
    CHECK(mkdir(cfg, 0700) == 0);
    snprintf(cfg, sizeof(cfg), "%s/kbdd/config", dir);
    f = fopen(cfg, "w");
    CHECK(f != NULL);
    if (f != NULL) { fputs("brightness 99\n", f); fclose(f); }

    memset(&k, 0, sizeof(k));
    CHECK(kbdd_load_config(&k) != 0);
    CHECK(access(cfg, F_OK) != 0);
    snprintf(bad, sizeof(bad), "%s.invalid", cfg);
    f = fopen(bad, "r");
    CHECK(f != NULL && fgets(line, sizeof(line), f) != NULL && strcmp(line, "brightness 99\n") == 0);
    if (f != NULL) fclose(f);

    unlink(bad);
    snprintf(cfg, sizeof(cfg), "%s/kbdd", dir);
    rmdir(cfg);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

/* ~/.config/kbdd as a symlink (stow, dotfiles) must work, read and write. */
static void test_symlinked_config_dir(void)
{
    char base[] = "/tmp/kbdd-config-lnk-XXXXXX";
    char target[512], xdg[512], link[600], file[700], bad[800];
    kbdd_ctx_t k, r;
    struct stat st;

    CHECK(mkdtemp(base) != NULL);
    snprintf(target, sizeof(target), "%s/dotfiles", base);
    snprintf(xdg, sizeof(xdg), "%s/config", base);
    snprintf(link, sizeof(link), "%s/kbdd", xdg);
    CHECK(mkdir(target, 0755) == 0);
    CHECK(mkdir(xdg, 0700) == 0);
    CHECK(symlink(target, link) == 0);
    CHECK(setenv("XDG_CONFIG_HOME", xdg, 1) == 0);

    memset(&k, 0, sizeof(k));
    kbdd_config_defaults(&k);
    k.config_mode = KBDD_CONFIG_SOLID;
    k.config_solid[0] = 9;
    CHECK(kbdd_save_config(&k) == 0);
    snprintf(file, sizeof(file), "%s/config", target);
    CHECK(access(file, F_OK) == 0);               /* written through the link */
    CHECK(stat(target, &st) == 0 && (st.st_mode & 0777) == 0755);   /* not chmod'ed */
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config(&r) == 0 && r.config_solid[0] == 9);

    /* a target other users can write to is still refused */
    CHECK(chmod(target, 0777) == 0);
    CHECK(kbdd_save_config(&k) != 0);

    unlink(file);
    snprintf(bad, sizeof(bad), "%s.tmp", file);
    unlink(bad);
    unlink(link);
    rmdir(target);
    rmdir(xdg);
    rmdir(base);
    unsetenv("XDG_CONFIG_HOME");
}

/* ---- Macro slot tests --------------------------------------------------- */

static void write_macro_file(const char *dir, const char *fname, uint8_t key_index)
{
    char path[512];
    FILE *f;
    uint32_t hdr[4] = {KBDD_MACRO_MAGIC, KBDD_MACRO_VERSION, key_index, KBC_MACRO_SIZE};
    uint8_t data[KBC_MACRO_SIZE];

    memset(data, 0, sizeof(data));
    snprintf(path, sizeof(path), "%s/kbdd/%s", dir, fname);
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(data, 1, KBC_MACRO_SIZE, f);
    fclose(f);
}

static void test_kbdd_macro_slot_next(void)
{
    char dir[] = "/tmp/kbdd-macro-slot-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    kbdd_ctx_t k;
    memset(&k, 0, sizeof(k));
    k.config_keymap_profile = 1;

    /* Nenhum arquivo: slot 1 livre */
    uint8_t slot = 0;
    CHECK(kbdd_macro_slot_next(&k, &slot) == 0);
    CHECK(slot == 1);

    /* Cria slot 1 (legado): próximo livre deve ser 2 */
    write_macro_file(dir, "macro-profile1.bin", 7);
    CHECK(kbdd_macro_slot_next(&k, &slot) == 0);
    CHECK(slot == 2);

    /* Cria slot 2 (indexado): próximo livre deve ser 3 */
    write_macro_file(dir, "macro-profile1-002.bin", 8);
    CHECK(kbdd_macro_slot_next(&k, &slot) == 0);
    CHECK(slot == 3);

    /* Preenche todos os slots 1..255: deve falhar */
    for (int s = 3; s <= 255; s++) {
        char fname[64];
        if (s == 1) {
            snprintf(fname, sizeof(fname), "macro-profile1.bin");
        } else {
            snprintf(fname, sizeof(fname), "macro-profile1-%03d.bin", s);
        }
        write_macro_file(dir, fname, (uint8_t)s);
    }
    CHECK(kbdd_macro_slot_next(&k, &slot) == -1);

    /* Limpa */
    for (int s = 1; s <= 255; s++) {
        char fname[64];
        if (s == 1) {
            snprintf(fname, sizeof(fname), "macro-profile1.bin");
        } else {
            snprintf(fname, sizeof(fname), "macro-profile1-%03d.bin", s);
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/kbdd/%s", dir, fname);
        unlink(path);
    }
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

static void test_kbdd_load_macro_with_slot(void)
{
    char dir[] = "/tmp/kbdd-macro-load-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    kbdd_ctx_t k;
    memset(&k, 0, sizeof(k));

    /* Slot inexistente → 0 (não encontrado, não erro) */
    uint8_t key_index = 0;
    uint8_t data[KBC_MACRO_SIZE];
    memset(data, 0xAA, sizeof(data));
    CHECK(kbdd_load_macro(&k, 1, 99, &key_index, data) == 0);
    CHECK(key_index == 0);

    /* Slot inválido (0 ou 256) → -1 */
    CHECK(kbdd_load_macro(&k, 1, 0, &key_index, data) == -1);
    CHECK(kbdd_load_macro(&k, 1, 256, &key_index, data) == -1);

    /* Cria slot 1 válido */
    write_macro_file(dir, "macro-profile1.bin", 42);
    CHECK(kbdd_load_macro(&k, 1, 1, &key_index, data) == 0);
    CHECK(key_index == 42);

    /* Cria slot 2 válido */
    write_macro_file(dir, "macro-profile1-002.bin", 75);
    CHECK(kbdd_load_macro(&k, 1, 2, &key_index, data) == 0);
    CHECK(key_index == 75);

    /* Slot corrompido (magic errado) → -1 */
    char path[512];
    snprintf(path, sizeof(path), "%s/kbdd/macro-profile1-003.bin", dir);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        uint32_t bad_hdr[4] = {0xDEADBEEF, 1, 10, KBC_MACRO_SIZE};
        fwrite(bad_hdr, 1, sizeof(bad_hdr), f);
        fwrite(data, 1, KBC_MACRO_SIZE, f);
        fclose(f);
    }
    CHECK(kbdd_load_macro(&k, 1, 3, &key_index, data) == -1);

    /* Limpa */
    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd/macro-profile1.bin", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd/macro-profile1-002.bin", dir);
    unlink(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

static void test_kbdd_macro_slot_find_by_key(void)
{
    char dir[] = "/tmp/kbdd-macro-find-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    kbdd_ctx_t k;
    memset(&k, 0, sizeof(k));
    k.config_keymap_profile = 1;

    /* Nenhum arquivo: não encontrado */
    uint8_t slot = 0;
    CHECK(kbdd_macro_slot_find_by_key(&k, 1, 42, &slot) == -1);

    /* Cria slot 1 com key_index 42 */
    write_macro_file(dir, "macro-profile1.bin", 42);
    CHECK(kbdd_macro_slot_find_by_key(&k, 1, 42, &slot) == 0);
    CHECK(slot == 1);

    /* Cria slot 2 com key_index 75 */
    write_macro_file(dir, "macro-profile1-002.bin", 75);
    CHECK(kbdd_macro_slot_find_by_key(&k, 1, 75, &slot) == 0);
    CHECK(slot == 2);

    /* Key_index não existente */
    CHECK(kbdd_macro_slot_find_by_key(&k, 1, 99, &slot) == -1);

    /* Perfil inválido */
    CHECK(kbdd_macro_slot_find_by_key(&k, 99, 42, &slot) == -1);

    /* NULL out_slot */
    CHECK(kbdd_macro_slot_find_by_key(&k, 1, 42, NULL) == -1);

    /* Limpa */
    char path[512];
    snprintf(path, sizeof(path), "%s/kbdd/macro-profile1.bin", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd/macro-profile1-002.bin", dir);
    unlink(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
}

static void test_macro_slot_persistence(void)
{
    kbdd_ctx_t k;
    kbdd_config_defaults(&k);

    /* config_macro_slot inicia como 0 (nenhum) */
    CHECK(k.config_macro_slot == 0);
    CHECK(k.config_macro_set == 0);

    /* Recarrega */
    kbdd_ctx_t k2;
    kbdd_config_defaults(&k2);
    CHECK(load_temp("macro_slot 3\n", &k2) == 0);
    CHECK(k2.config_macro_slot == 3);
    /* macro_slot is only written when a macro has been persisted, so the flag
     * config_macro_set must be restored as well. */
    CHECK(k2.config_macro_set == 1);
}

static void test_keymap_profile_parsing(void)
{
    kbdd_ctx_t k;
    kbdd_config_defaults(&k);

    /* Parsing only: no keymap binary exists yet, so the flag stays clear. */
    CHECK(load_temp("keymap_profile 2\n", &k) == 0);
    CHECK(k.config_keymap_profile == 2);
    CHECK(k.config_keymap_set == 0);

    /* Invalid values are rejected. */
    kbdd_ctx_t k2;
    kbdd_config_defaults(&k2);
    CHECK(load_temp("keymap_profile 0\n", &k2) != 0);
    CHECK(load_temp("keymap_profile 5\n", &k2) != 0);
}

static void write_test_keymap_file(const char *dir, uint8_t profile)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/kbdd/keymap-profile%u.bin", dir, profile);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    kbdd_keymap_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = KBDD_KEYMAP_MAGIC;
    hdr.version = KBDD_KEYMAP_VERSION;
    hdr.layer_count = 3;
    hdr.positions_per_layer = KBC_KEY_POSITIONS;
    fwrite(&hdr, 1, sizeof(hdr), f);
    uint8_t matrix[3 * KBC_KEY_POSITIONS];
    memset(matrix, 0, sizeof(matrix));
    /* Put a 0xB2 macro marker at position 0 of layer 0. */
    matrix[0] = 0xB2;
    fwrite(matrix, 1, sizeof(matrix), f);
    fclose(f);
}

static void test_keymap_profile_load_from_disk(void)
{
    /* Use a temporary XDG_CONFIG_HOME so we don't clobber the user's config. */
    const char *tmpdir = "/tmp/kbdd-config-test-xdg";
    char kbdd_dir[512];
    snprintf(kbdd_dir, sizeof(kbdd_dir), "%s/kbdd", tmpdir);
    mkdir(tmpdir, 0700);
    mkdir(kbdd_dir, 0700);

    setenv("XDG_CONFIG_HOME", tmpdir, 1);

    write_test_keymap_file(tmpdir, 1);
    kbdd_ctx_t k;
    kbdd_config_defaults(&k);
    /* kbdd_load_config reads $XDG_CONFIG_HOME/kbdd/config. Write a minimal
     * config pointing to keymap profile 1. */
    char cfgpath[512];
    snprintf(cfgpath, sizeof(cfgpath), "%s/kbdd/config", tmpdir);
    FILE *cfg = fopen(cfgpath, "w");
    if (cfg != NULL) {
        fprintf(cfg, "keymap_profile 1\n");
        fclose(cfg);
    }

    CHECK(kbdd_load_config(&k) == 0);
    CHECK(k.config_keymap_profile == 1);
    CHECK(k.config_keymap_set == 1);
    CHECK(k.config_keymap[0][0] == 0xB2);

    unsetenv("XDG_CONFIG_HOME");
    remove(cfgpath);
    char keypath[512];
    snprintf(keypath, sizeof(keypath), "%s/kbdd/keymap-profile1.bin", tmpdir);
    remove(keypath);
    rmdir(kbdd_dir);
    rmdir(tmpdir);
}

static void test_keymap_profile_missing_binary_clears_flag(void)
{
    const char *tmpdir = "/tmp/kbdd-config-test-xdg2";
    char kbdd_dir[512];
    snprintf(kbdd_dir, sizeof(kbdd_dir), "%s/kbdd", tmpdir);
    mkdir(tmpdir, 0700);
    mkdir(kbdd_dir, 0700);

    setenv("XDG_CONFIG_HOME", tmpdir, 1);

    /* No keymap-profile1.bin exists. */
    kbdd_ctx_t k;
    kbdd_config_defaults(&k);
    char cfgpath[512];
    snprintf(cfgpath, sizeof(cfgpath), "%s/kbdd/config", tmpdir);
    FILE *cfg = fopen(cfgpath, "w");
    if (cfg != NULL) {
        fprintf(cfg, "keymap_profile 1\n");
        fclose(cfg);
    }

    CHECK(kbdd_load_config(&k) == 0);
    /* Orphan reference must be cleared. */
    CHECK(k.config_keymap_profile == 0);
    CHECK(k.config_keymap_set == 0);

    unsetenv("XDG_CONFIG_HOME");
    remove(cfgpath);
    rmdir(kbdd_dir);
    rmdir(tmpdir);
}


int main(void)
{
    test_missing_file_defaults();
    test_empty_file_defaults();
    test_valid_full_config();
    test_valid_animation_config();
    test_valid_custom_config();
    test_invalid_cases();
    test_boundary_cases();
    test_valid_opts_config();
    test_invalid_opts_config();
    test_valid_opts_set_config();
    test_invalid_opts_set_config();
    test_valid_led_field_config();
    test_invalid_led_field_config();
    test_save_load_led_fields_roundtrip();
    test_colors_roundtrip();
    test_save_load_opts_roundtrip();
    test_save_writes_animation_once();
    test_unknown_keys_tolerated_and_preserved();
    test_invalid_config_is_kept();
    test_symlinked_config_dir();

    test_kbdd_macro_slot_next();
    test_kbdd_load_macro_with_slot();
    test_kbdd_macro_slot_find_by_key();
    test_macro_slot_persistence();
    test_keymap_profile_parsing();
    test_keymap_profile_load_from_disk();
    test_keymap_profile_missing_binary_clears_flag();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all config tests");
    return 0;
}
