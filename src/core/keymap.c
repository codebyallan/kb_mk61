#include "kbcore.h"

#include <stdlib.h>
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

static const uint8_t DEF_NORMAL[KBC_KEY_POSITIONS] = {
    0x29,0x2b,0x39,0xe1,0xe0,0x1e,0x14,0x04,0x00,0xe3,0x1f,0x1a,0x16,0x1d,0xe2,
    0x20,0x08,0x07,0x1b,0x00,0x21,0x15,0x09,0x06,0x00,0x22,0x17,0x0a,0x19,0x2c,
    0x23,0x1c,0x0b,0x05,0x00,0x24,0x18,0x0d,0x11,0x00,0x25,0x0c,0x0e,0x10,0x00,
    0x26,0x12,0x0f,0x36,0x00,0x27,0x13,0x33,0x37,0xe6,0x2d,0x2f,0x34,0x38,0x65,
    0x2e,0x30,0x00,0x00,0xe4,0x2a,0x31,0x28,0xe5,0xac
};

static const uint8_t DEF_FN[KBC_KEY_POSITIONS] = {
    0x35,0x00,0x39,0xe1,0xe0,0x3a,0xad,0xa7,0x00,0xb1,0x3b,0xae,0xa6,0x1d,0xe2,
    0x3c,0xaf,0xa8,0x1b,0x00,0x3d,0xb0,0xa9,0x06,0x00,0x3e,0x17,0xaa,0x19,0xf3,
    0x3f,0x1c,0xab,0x05,0x00,0x40,0x46,0x49,0xb5,0x00,0x41,0x47,0x4a,0x4c,0x00,
    0x42,0x48,0x4b,0x4d,0x00,0x43,0xd7,0xde,0x4e,0x50,0x44,0xd6,0xdf,0x52,0x51,
    0x45,0xf8,0x00,0x00,0x4f,0xf9,0xd9,0xfc,0xe5,0xac
};

int kbc_keymap_default(size_t layer, uint8_t out[KBC_KEY_POSITIONS])
{
    if (out == NULL || layer >= KBC_KEYMAP_LAYERS) return -1;
    if (layer == 0) memcpy(out, DEF_NORMAL, KBC_KEY_POSITIONS);
    else if (layer == 1) memcpy(out, DEF_FN, KBC_KEY_POSITIONS);
    else memset(out, 0, KBC_KEY_POSITIONS);
    return 0;
}

static const struct { const char *name; uint8_t code; } HID[] = {
    {"none", 0x00}, {"off", 0x00}, {"enter", 0x28}, {"return", 0x28}, {"esc", 0x29},
    {"escape", 0x29}, {"backspace", 0x2a}, {"bksp", 0x2a}, {"tab", 0x2b}, {"space", 0x2c},
    {"minus", 0x2d}, {"-", 0x2d}, {"equals", 0x2e}, {"=", 0x2e}, {"lbracket", 0x2f},
    {"[", 0x2f}, {"rbracket", 0x30}, {"]", 0x30}, {"backslash", 0x31}, {"\\", 0x31},
    {"semicolon", 0x33}, {";", 0x33}, {"quote", 0x34}, {"'", 0x34}, {"grave", 0x35},
    {"`", 0x35}, {"comma", 0x36}, {",", 0x36}, {"period", 0x37}, {".", 0x37},
    {"slash", 0x38}, {"/", 0x38}, {"caps", 0x39}, {"capslock", 0x39},
    {"prtsc", 0x46}, {"scroll", 0x47}, {"pause", 0x48}, {"ins", 0x49}, {"home", 0x4a},
    {"pgup", 0x4b}, {"del", 0x4c}, {"end", 0x4d}, {"pgdn", 0x4e}, {"right", 0x4f},
    {"left", 0x50}, {"down", 0x51}, {"up", 0x52}, {"menu", 0x65},
    {"lctrl", 0xe0}, {"lshift", 0xe1}, {"lalt", 0xe2}, {"lwin", 0xe3},
    {"rctrl", 0xe4}, {"rshift", 0xe5}, {"ralt", 0xe6}, {"rwin", 0xe7},
    {"fn", 0xac}, {"fnlock", 0xf3},
    {"stop", 0xa5}, {"prev", 0xa6}, {"play", 0xa7}, {"next", 0xa8},
    {"mute", 0xa9}, {"volup", 0xaa}, {"voldown", 0xab},
    {"profile1", 0xad}, {"profile2", 0xae}, {"profile3", 0xaf}, {"profile4", 0xb0},
    {"bright+", 0xd6}, {"bright-", 0xd7}, {"effect+", 0xd9}, {"speed-", 0xde},
    {"speed+", 0xdf}, {"customfx", 0xf8}, {"ledtoggle", 0xf9}, {"endurance", 0xfb},
    {"mouse1", 0xb7}, {"mouse2", 0xb8}, {"mouse3", 0xb9}, {"mouse4", 0xba}, {"mouse5", 0xbb}
};

int kbc_hid_code(const char *name)
{
    size_t i;
    if (name == NULL || name[0] == '\0') return -1;
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X') && name[2] != '\0') {
        char *end;
        unsigned long v = strtoul(name + 2, &end, 16);
        return (*end != '\0' || v > 0xFF || v == 0xB2) ? -1 : (int)v;
    }
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
        if (c >= '1' && c <= '9') return 0x1d + (c - '0');
        if (c == '0') return 0x27;
    }
    if ((name[0] == 'f' || name[0] == 'F') && name[1] >= '1' && name[1] <= '9') {
        char *end;
        long n = strtol(name + 1, &end, 10);
        if (*end == '\0' && n >= 1 && n <= 12) return 0x3a + (int)(n - 1);
    }
    for (i = 0; i < sizeof(HID) / sizeof(HID[0]); i++) {
        if (ieq(name, HID[i].name)) return HID[i].code;
    }
    return -1;
}
