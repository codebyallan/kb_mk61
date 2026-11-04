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
    CHECK(kbdd_save_config(&k) == 0);

    snprintf(path, sizeof(path), "%s/kbdd/config", dir);
    memset(&r, 0, sizeof(r));
    CHECK(kbdd_load_config_file(&r, path) == 0);
    CHECK(r.config_opts_direction == 1);
    CHECK(r.config_opts_win_lock == 1);
    CHECK(r.config_mode == KBDD_CONFIG_SOLID);
    CHECK(r.config_solid[0] == 1 && r.config_solid[1] == 2 && r.config_solid[2] == 3);

    /* Verify the on-disk lines exist. */
    {
        FILE *f = fopen(path, "r");
        char line[64];
        int saw_direction = 0, saw_win_lock = 0;
        CHECK(f != NULL);
        while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
            if (strcmp(line, "direction 1\n") == 0) saw_direction = 1;
            if (strcmp(line, "win_lock 1\n") == 0) saw_win_lock = 1;
        }
        if (f != NULL) fclose(f);
        CHECK(saw_direction == 1);
        CHECK(saw_win_lock == 1);
    }

    unlink(path);
    snprintf(path, sizeof(path), "%s/kbdd", dir);
    rmdir(path);
    rmdir(dir);
    unsetenv("XDG_CONFIG_HOME");
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
    test_save_load_opts_roundtrip();
    test_save_writes_animation_once();
    test_unknown_keys_tolerated_and_preserved();
    test_invalid_config_is_kept();
    test_symlinked_config_dir();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all config tests");
    return 0;
}
