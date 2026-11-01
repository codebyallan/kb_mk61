#include "kbcore.h"

#include <string.h>

/* MK61L / SK61L matrix: index = column * 5 + row. NULL = no key (gaps left by
 * wide keys). Confirmed on hardware with `kbctl led sweep`. */
static const char *const KEYS[KBC_KEY_POSITIONS] = {
    "esc", "tab", "caps", "lshift", "lctrl",            /*  0.. 4 */
    "1", "q", "a", NULL, "lwin",                        /*  5.. 9 */
    "2", "w", "s", "z", "lalt",                         /* 10..14 */
    "3", "e", "d", "x", NULL,                           /* 15..19 */
    "4", "r", "f", "c", NULL,                           /* 20..24 */
    "5", "t", "g", "v", "space",                        /* 25..29 */
    "6", "y", "h", "b", NULL,                           /* 30..34 */
    "7", "u", "j", "n", NULL,                           /* 35..39 */
    "8", "i", "k", "m", NULL,                           /* 40..44 */
    "9", "o", "l", "comma", NULL,                       /* 45..49 */
    "0", "p", "semicolon", "period", "ralt",            /* 50..54 */
    "minus", "lbracket", "quote", "slash", "menu",      /* 55..59 */
    "equals", "rbracket", NULL, NULL, "rctrl",          /* 60..64 */
    "backspace", "backslash", "enter", "rshift", "fn"   /* 65..69 */
};

static const struct { const char *alias; int pos; } ALIASES[] = {
    {"escape", 0}, {"grave", 0}, {"tilde", 0}, {"`", 0}, {"~", 0},
    {"capslock", 2}, {"shift", -1}, {"ctrl", -1}, {"alt", -1},
    {"win", 9}, {"super", 9}, {"windows", 9},
    {"-", 55}, {"[", 56}, {"'", 57}, {"/", 58}, {"=", 60}, {"]", 61},
    {",", 48}, {";", 52}, {".", 53}, {"\\", 66},
    {"bksp", 65}, {"return", 67}, {"spacebar", 29}, {"context", 59}
};

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

int kbc_key_position(const char *name)
{
    size_t i;
    if (name == NULL || name[0] == '\0') return -1;
    for (i = 0; i < KBC_KEY_POSITIONS; i++) {
        if (KEYS[i] != NULL && ieq(name, KEYS[i])) return (int)i;
    }
    for (i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); i++) {
        if (ieq(name, ALIASES[i].alias)) return ALIASES[i].pos;
    }
    return -1;
}

const char *kbc_key_name(size_t pos)
{
    return pos < KBC_KEY_POSITIONS ? KEYS[pos] : NULL;
}

size_t kbc_key_group(const char *group, int *out, size_t max)
{
    static const char *const WASD[] = {"w", "a", "s", "d", NULL};
    static const char *const NUMBERS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NULL};
    static const char *const MODS[] = {"lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt",
                                       "lwin", "fn", "menu", NULL};
    const char *const *list = NULL;
    size_t n = 0, i;

    if (group == NULL || out == NULL) return 0;
    if (ieq(group, "wasd")) list = WASD;
    else if (ieq(group, "numbers")) list = NUMBERS;
    else if (ieq(group, "mods")) list = MODS;
    if (list != NULL) {
        for (i = 0; list[i] != NULL && n < max; i++) out[n++] = kbc_key_position(list[i]);
        return n;
    }
    if (ieq(group, "letters") || ieq(group, "all")) {
        int letters = ieq(group, "letters");
        for (i = 0; i < KBC_KEY_POSITIONS && n < max; i++) {
            if (KEYS[i] == NULL) continue;
            if (letters && !(KEYS[i][0] >= 'a' && KEYS[i][0] <= 'z' && KEYS[i][1] == '\0')) continue;
            out[n++] = (int)i;
        }
    }
    return n;
}
