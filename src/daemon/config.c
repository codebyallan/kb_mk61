#include "kbdd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int kbdd_ensure_dir(const char *path)
{
    struct stat st;
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (lstat(path, &st) != 0) {
        if (errno != ENOENT) {
            return -1;
        }
        if (mkdir(path, 0700) != 0) {
            return -1;
        }
        if (lstat(path, &st) != 0) {
            return -1;
        }
    }
    if (S_ISLNK(st.st_mode)) {
        /* A symlinked config dir is normal with dotfile managers (stow).
         * Accept it when the target is a directory we own that others cannot
         * write to. Never chmod through the link: that would change the
         * permissions of the user's dotfiles repository. */
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode) ||
            st.st_uid != getuid() || (st.st_mode & 0022) != 0) {
            return -1;
        }
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    if (st.st_uid != getuid()) {
        return -1;
    }
    if ((st.st_mode & 0777) != 0700) {
        /* tighten permissions if owned by us */
        if (chmod(path, 0700) != 0) {
            return -1;
        }
    }
    return 0;
}

static int kbdd_config_dir(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        if (snprintf(buf, sz, "%s/kbdd", xdg) >= (int)sz) {
            return -1;
        }
    } else {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return -1;
        }
        if (snprintf(buf, sz, "%s/.config/kbdd", home) >= (int)sz) {
            return -1;
        }
    }
    /* create parent directory if needed (best effort) */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", buf);
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (parent[0] != '\0') {
            mkdir(parent, 0755);
        }
    }
    if (kbdd_ensure_dir(buf) != 0) {
        return -1;
    }
    return 0;
}

static int kbdd_config_path(char *path, size_t size)
{
    char dir[512];
    if (kbdd_config_dir(dir, sizeof(dir)) != 0 ||
        snprintf(path, size, "%s/config", dir) >= (int)size) {
        return -1;
    }
    return 0;
}

void kbdd_config_defaults(kbdd_ctx_t *k)
{
    size_t i;
    k->config_extra[0] = '\0';
    k->config_extra_len = 0;
    memset(k->config_solid, 0, sizeof(k->config_solid));
    memset(k->config_positions, 0, sizeof(k->config_positions));
    k->config_mode = KBDD_CONFIG_NONE;
    k->config_effect = 0;
    k->config_brightness = KBC_BRIGHTNESS_RAW_MAX;
    k->config_stride = 0;
    k->config_anim = 0;
    k->config_anim_period = 0;
    memset(k->config_anim_rgb, 0, sizeof(k->config_anim_rgb));
    for (i = 0; i < KBC_LED_EFFECT_COUNT; i++) {
        k->config_speed[i] = 50;
    }
}

int kbdd_load_config_file(kbdd_ctx_t *k, const char *path)
{
    FILE *f;
    char key[32];
    unsigned value;
    unsigned speed_effect;
    size_t pos;
    unsigned r, g, b;

    if (k == NULL || path == NULL) return -1;
    kbdd_config_defaults(k);
    f = fopen(path, "r");
    if (f == NULL) return errno == ENOENT ? 0 : -1;
    while (fscanf(f, "%31s", key) == 1) {
        if (strcmp(key, "mode") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBDD_CONFIG_CUSTOM) {
                fclose(f);
                kbdd_config_defaults(k);
                return -1;
            }
            k->config_mode = (uint8_t)value;
        } else if (strcmp(key, "effect") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value >= KBC_LED_EFFECT_COUNT) {
                fclose(f);
                return -1;
            }
            k->config_effect = (uint8_t)value;
        } else if (strcmp(key, "animation") == 0) {
            if (fscanf(f, "%u %u %u %u %u", &value, &speed_effect,
                       &r, &g, &b) != 5 ||
                value < KBC_ANIM_BREATH || value > KBC_ANIM_RAINBOW ||
                speed_effect < KBC_ANIM_PERIOD_MIN ||
                speed_effect > KBC_ANIM_PERIOD_MAX ||
                r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_anim = (uint8_t)value;
            k->config_anim_period = speed_effect;
            k->config_anim_rgb[0] = (uint8_t)r;
            k->config_anim_rgb[1] = (uint8_t)g;
            k->config_anim_rgb[2] = (uint8_t)b;
        } else if (strcmp(key, "brightness") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBC_BRIGHTNESS_RAW_MAX) {
                fclose(f);
                return -1;
            }
            k->config_brightness = (uint8_t)value;
        } else if (strcmp(key, "speed") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value >= KBC_LED_EFFECT_COUNT) {
                fclose(f);
                return -1;
            }
            speed_effect = value;
            if (fscanf(f, "%u", &value) != 1 ||
                value < KBC_SPEED_RAW_MIN || value > KBC_SPEED_RAW_MAX) {
                fclose(f);
                return -1;
            }
            k->config_speed[speed_effect] = (uint8_t)value;
        } else if (strcmp(key, "solid") == 0) {
            if (fscanf(f, "%u %u %u", &r, &g, &b) != 3 ||
                r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_solid[0] = (uint8_t)r;
            k->config_solid[1] = (uint8_t)g;
            k->config_solid[2] = (uint8_t)b;
        } else if (strcmp(key, "stride") == 0) {
            if (fscanf(f, "%u", &value) != 1 || value > KBC_MAX_MATRIX_SIZE) {
                fclose(f);
                return -1;
            }
            k->config_stride = value;
        } else if (strcmp(key, "pos") == 0) {
            if (fscanf(f, "%zu %u %u %u", &pos, &r, &g, &b) != 4 ||
                pos >= KBC_MAX_MATRIX_SIZE || r > 255 || g > 255 || b > 255) {
                fclose(f);
                return -1;
            }
            k->config_positions[pos][0] = (uint8_t)r;
            k->config_positions[pos][1] = (uint8_t)g;
            k->config_positions[pos][2] = (uint8_t)b;
        } else {
            /* Unknown key (probably from a newer version): skip the line and
             * keep it so the next save does not destroy it. */
            char rest[256];
            size_t klen = strlen(key), rlen;

            rest[0] = '\0';
            if (fgets(rest, sizeof(rest), f) != NULL && strchr(rest, '\n') == NULL) {
                int ch;
                while ((ch = fgetc(f)) != EOF && ch != '\n') {
                }
            }
            rest[strcspn(rest, "\r\n")] = '\0';
            rlen = strlen(rest);
            fprintf(stderr, "kbdd: config: ignoring unknown key '%s'\n", key);
            if (k->config_extra_len + klen + rlen + 1 < sizeof(k->config_extra)) {
                memcpy(k->config_extra + k->config_extra_len, key, klen);
                memcpy(k->config_extra + k->config_extra_len + klen, rest, rlen);
                k->config_extra_len += klen + rlen;
                k->config_extra[k->config_extra_len++] = '\n';
                k->config_extra[k->config_extra_len] = '\0';
            } else {
                fprintf(stderr, "kbdd: config: too many unknown keys to preserve '%s'\n", key);
            }
        }
    }
    fclose(f);
    return 0;
}


int kbdd_load_config(kbdd_ctx_t *k)
{
    char path[768];
    int rc;

    if (k == NULL || kbdd_config_path(path, sizeof(path)) != 0) return -1;
    rc = kbdd_load_config_file(k, path);
    if (rc != 0 && access(path, F_OK) == 0) {
        /* The caller falls back to defaults and the next save would overwrite
         * the file: keep the user's original next to it. */
        char bad[800];

        if (snprintf(bad, sizeof(bad), "%s.invalid", path) < (int)sizeof(bad) &&
            rename(path, bad) == 0) {
            fprintf(stderr, "kbdd: invalid config moved to %s\n", bad);
        }
    }
    return rc;
}
int kbdd_save_config(const kbdd_ctx_t *k)
{
    char path[768], tmp[800];
    FILE *f;
    size_t i;

    if (k == NULL || kbdd_config_path(path, sizeof(path)) != 0 ||
        snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    f = fopen(tmp, "w");
    if (f == NULL) return -1;
    if (fchmod(fileno(f), 0600) != 0) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    if (fprintf(f, "mode %u\neffect %u\nbrightness %u\n",
                (unsigned)k->config_mode, (unsigned)k->config_effect,
                (unsigned)k->config_brightness) < 0) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    for (i = 0; i < KBC_LED_EFFECT_COUNT; i++) {
        if (fprintf(f, "speed %zu %u\n", i, (unsigned)k->config_speed[i]) < 0) {
            fclose(f);
            unlink(tmp);
            return -1;
        }
    }
    if (k->config_mode == KBDD_CONFIG_ANIMATION) {
        if (fprintf(f, "animation %u %u %u %u %u\n",
                    (unsigned)k->config_anim,
                    (unsigned)k->config_anim_period,
                    (unsigned)k->config_anim_rgb[0],
                    (unsigned)k->config_anim_rgb[1],
                    (unsigned)k->config_anim_rgb[2]) < 0) {
            fclose(f);
            unlink(tmp);
            return -1;
        }
    }
    if (k->config_mode == KBDD_CONFIG_SOLID) {
        fprintf(f, "solid %u %u %u\nstride %zu\n",
                (unsigned)k->config_solid[0], (unsigned)k->config_solid[1],
                (unsigned)k->config_solid[2], k->config_stride);
    } else if (k->config_mode == KBDD_CONFIG_POSITIONS) {
        fprintf(f, "stride %zu\n", k->config_stride);
        for (i = 0; i < KBC_MAX_MATRIX_SIZE; i++) {
            if (k->config_positions[i][0] != 0 ||
                k->config_positions[i][1] != 0 ||
                k->config_positions[i][2] != 0) {
                fprintf(f, "pos %zu %u %u %u\n", i,
                        (unsigned)k->config_positions[i][0],
                        (unsigned)k->config_positions[i][1],
                        (unsigned)k->config_positions[i][2]);
            }
        }
    }
    if (k->config_extra_len > 0 &&
        fwrite(k->config_extra, 1, k->config_extra_len, f) != k->config_extra_len) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

