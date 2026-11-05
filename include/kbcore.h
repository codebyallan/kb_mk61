/*
 * libkbcore - public API.
 *
 * This header is the only supported entry point into the core library.
 * All state is held behind opaque handles; callers must not depend on the
 * layout of any structure declared here.  The library performs no global
 * I/O, does not read environment variables or configuration files, and has
 * no GUI toolkit dependency.
 */
#ifndef KBCORE_H
#define KBCORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
#  define KBCORE_API __attribute__((visibility("default")))
#else
#  define KBCORE_API
#endif

#define KBCORE_VERSION_MAJOR 1
#define KBCORE_VERSION_MINOR 0
#define KBCORE_VERSION_PATCH 0
#define KBCORE_STR_(x) #x
#define KBCORE_STR(x)  KBCORE_STR_(x)
#define KBCORE_VERSION_STRING KBCORE_STR(KBCORE_VERSION_MAJOR) "." \
                              KBCORE_STR(KBCORE_VERSION_MINOR) "." \
                              KBCORE_STR(KBCORE_VERSION_PATCH)

/* ------------------------------------------------------------------ */
/* Opaque handles.  Definitions live in src/core and are private.       */
/* ------------------------------------------------------------------ */
typedef struct kbc_device    kbc_device;
typedef struct kbc_profile   kbc_profile;
typedef struct kbc_macro     kbc_macro;
typedef struct kbc_transport kbc_transport;

typedef enum kbc_status {
    KBC_OK                    = 0,
    KBC_ERR_INVALID_ARGUMENT  = -1,
    KBC_ERR_NO_MEMORY         = -2,
    KBC_ERR_IO                = -3,
    KBC_ERR_PROTOCOL          = -4,
    KBC_ERR_UNSUPPORTED       = -5,
    KBC_ERR_BUSY              = -6,
    KBC_ERR_RANGE             = -7
} kbc_status;

/* ------------------------------------------------------------------ */
/* Protocol constants                                                  */
/* ------------------------------------------------------------------ */

/* Report IDs. */
#define KBC_FEATURE_REPORT_ID 0x07
#define KBC_INPUT_REPORT_ID   0x04

/* Report sizes (bytes), including the report ID in byte 0. */
#define KBC_FEATURE_REPORT_SIZE 264
#define KBC_FEATURE_REPORT_LEN  264
/* Effective payload: bytes 1..263 (263 bytes), per Q1 (conservative). */
#define KBC_FEATURE_PAYLOAD_SIZE 263
/* Data region inside the feature report: bytes 8..263 (256 bytes). */
#define KBC_FEATURE_DATA_OFFSET 8
#define KBC_FEATURE_DATA_SIZE   256
#define KBC_FEATURE_PARAMS_SIZE 4   /* bytes 4..7 */

/* Input report. */
#define KBC_INPUT_REPORT_MIN_SIZE 6
#define KBC_INPUT_REPORT_SIZE     6

/* Macro / LED buffer sizes. */
#define KBC_MACRO_SIZE        256
#define KBC_MACRO_MAX_STEPS   80
#define KBC_MACRO_MAX_REPETITIONS 1000
#define KBC_MACRO_MIN_DELAY   20
#define KBC_MACRO_MAX_CLASSES 20
#define KBC_MACRO_MAX_PER_CLASS 51
#define KBC_LED_BUFFER_SIZE   432
#define KBC_LED_PLANAR_STRIDE 144
#define KBC_PAGE_SIZE         256

/* Matrix / LED limits (from the validated model table). */
#define KBC_MAX_MATRIX_SIZE 75
#define KBC_MAX_LED_COUNT   144

/* Shared-library ABI version (soname libkbcore.so.<KBCORE_SOVERSION>).
 * Bump on any incompatible change to the exported API. */
#define KBCORE_SOVERSION 1

/* Number of feature-report opcodes in the allowlist. */
#define KBC_OPCODE_COUNT 13

/* Feature-report opcodes. */
#define KBC_OP_SELECT_PROFILE       0x01
#define KBC_OP_PROFILE_PARAMS       0x02
#define KBC_OP_KEY_MATRIX           0x03
#define KBC_OP_LED_DATA             0x04
#define KBC_OP_MACRO                0x05
#define KBC_OP_ENABLE_INPUT         0x06
#define KBC_OP_LED_PAGE             0x07
#define KBC_OP_CUSTOM_COLOR         0x08
#define KBC_OP_CUSTOM_FRAME         0x09
#define KBC_OP_CUSTOM_EFFECT        0x0A
#define KBC_OP_PROFILE_EXTRA        0x0B
#define KBC_OP_READ_ACTIVE_PROFILE  0x81
#define KBC_OP_READ_PROFILE_BLOCK   0x82

/* ------------------------------------------------------------------ */
/* Decoded report structures                                           */
/* ------------------------------------------------------------------ */

typedef struct kbc_feature_report {
    uint8_t report_id;                 /* byte 0, must be 0x07 */
    uint8_t opcode;                    /* byte 1 */
    uint8_t param0;                    /* byte 2 */
    uint8_t param1;                    /* byte 3 */
    uint8_t params[KBC_FEATURE_PARAMS_SIZE]; /* bytes 4..7 */
    uint8_t param[KBC_FEATURE_PARAMS_SIZE + 2]; /* param[0]=byte2 ... param[5]=byte7 */
    uint8_t data[KBC_FEATURE_DATA_SIZE]; /* bytes 8..263 */
    size_t  data_len;                  /* number of meaningful bytes in data */
} kbc_feature_report;

/* ------------------------------------------------------------------ */
/* Decoded report structures (continued)                               */
/* ------------------------------------------------------------------ */

typedef struct kbc_input_report {
    uint8_t report_id;       /* byte 0, must be 0x04 */
    uint8_t marker;          /* byte 1 */
    uint8_t subtype;         /* marker / system subtype */
    uint8_t system_subtype;  /* byte 2 when marker == 0xE1 */
    uint8_t col;             /* column (0-based) */
    uint8_t row;             /* row (0-based) */
    uint8_t state;           /* state byte (pp) */
    uint8_t extra;           /* extra byte (GK8150) */
    uint8_t value;           /* byte 3 for system events (profile id, led mode, ...) */
    uint16_t matrix_index;   /* col * 5 + row */
    int pressed;             /* 1 if the event should be emitted */
    uint8_t raw[KBC_INPUT_REPORT_MIN_SIZE];
} kbc_input_report;

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */
KBCORE_API const char *kbc_version_string(void);
KBCORE_API unsigned int kbc_version_number(void);

/* ------------------------------------------------------------------ */
/* Feature report 0x07: decode / encode                                */
/* ------------------------------------------------------------------ */

/* Validate report ID, size, opcode and offsets. Returns KBC_OK on success. */
KBCORE_API kbc_status kbc_validate_feature_report(const uint8_t *buf, size_t len);
KBCORE_API kbc_status kbc_validate_input_report(const uint8_t *buf, size_t len);
KBCORE_API kbc_status kbc_validate_opcode(uint8_t opcode);
KBCORE_API kbc_status  kbc_is_valid_opcode(uint8_t opcode);

/* Decode a 264-byte feature report into *out. */
KBCORE_API kbc_status kbc_parse_feature_report(const uint8_t *buf, size_t len,
                                               kbc_feature_report *out);

/* Encode *in into buf (must have room for KBC_FEATURE_REPORT_SIZE bytes). */
KBCORE_API kbc_status kbc_encode_feature_report(const kbc_feature_report *in,
                                                 uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_build_feature_report(uint8_t opcode, uint8_t param0,
                                               uint8_t param1,
                                               const uint8_t params[KBC_FEATURE_PARAMS_SIZE],
                                               const uint8_t *data, size_t data_len,
                                               uint8_t *buf, size_t buf_len);

/* Decode an input report 0x04 into *out. */
KBCORE_API kbc_status kbc_parse_input_report(const uint8_t *buf, size_t len,
                                              kbc_input_report *out);

/* Convenience: validate a single report ID value. */
KBCORE_API kbc_status kbc_validate_report_id(uint8_t id, uint8_t expected);


/* ------------------------------------------------------------------ */
/* Profiles (opaque handle kbc_profile)                                 */
/* ------------------------------------------------------------------ */

KBCORE_API kbc_profile *kbc_profile_create(size_t matrix_size, size_t led_count);
KBCORE_API void kbc_profile_destroy(kbc_profile *p);
KBCORE_API kbc_status kbc_profile_validate(const kbc_profile *p);

KBCORE_API size_t kbc_profile_matrix_size(const kbc_profile *p);
KBCORE_API size_t kbc_profile_led_count(const kbc_profile *p);
KBCORE_API size_t kbc_profile_layer_count(const kbc_profile *p);

KBCORE_API kbc_status kbc_profile_set_key(kbc_profile *p, size_t layer, size_t pos, uint8_t value);
KBCORE_API kbc_status kbc_profile_get_key(const kbc_profile *p, size_t layer, size_t pos, uint8_t *out);
KBCORE_API kbc_status kbc_profile_set_layer(kbc_profile *p, size_t layer, const uint8_t *values, size_t count);
KBCORE_API kbc_status kbc_profile_get_layer(const kbc_profile *p, size_t layer, uint8_t *out, size_t count);

KBCORE_API kbc_status kbc_profile_set_hw_profile(kbc_profile *p, uint8_t hw);
KBCORE_API uint8_t kbc_profile_get_hw_profile(const kbc_profile *p);
KBCORE_API kbc_status kbc_profile_set_report_rate_index(kbc_profile *p, uint8_t idx);
KBCORE_API uint8_t kbc_profile_get_report_rate_index(const kbc_profile *p);
KBCORE_API kbc_status kbc_profile_set_led_mode(kbc_profile *p, uint8_t mode);
KBCORE_API uint8_t kbc_profile_get_led_mode(const kbc_profile *p);
KBCORE_API kbc_status kbc_profile_set_led_intensity(kbc_profile *p, uint8_t v);
KBCORE_API uint8_t kbc_profile_get_led_intensity(const kbc_profile *p);
KBCORE_API kbc_status kbc_profile_set_led_speed(kbc_profile *p, uint8_t v);
KBCORE_API uint8_t kbc_profile_get_led_speed(const kbc_profile *p);
KBCORE_API kbc_status kbc_profile_set_spectrum(kbc_profile *p, size_t index, uint8_t v);
KBCORE_API uint8_t kbc_profile_get_spectrum(const kbc_profile *p, size_t index);
KBCORE_API kbc_status kbc_profile_set_color(kbc_profile *p, size_t index, uint8_t r, uint8_t g, uint8_t b);
KBCORE_API kbc_status kbc_profile_get_color(const kbc_profile *p, size_t index, uint8_t *r, uint8_t *g, uint8_t *b);
KBCORE_API kbc_status kbc_profile_set_direction(kbc_profile *p, uint8_t v);
KBCORE_API kbc_status kbc_profile_set_win_lock(kbc_profile *p, uint8_t v);
KBCORE_API kbc_status kbc_profile_set_six_key_mode(kbc_profile *p, uint8_t v);
KBCORE_API kbc_status kbc_profile_set_custom_total(kbc_profile *p, uint8_t v);
KBCORE_API kbc_status kbc_profile_set_custom_current(kbc_profile *p, uint8_t v);
KBCORE_API kbc_status kbc_profile_set_led_state(kbc_profile *p, uint8_t v);
KBCORE_API uint8_t kbc_profile_get_direction(const kbc_profile *p);
KBCORE_API uint8_t kbc_profile_get_win_lock(const kbc_profile *p);
KBCORE_API uint8_t kbc_profile_get_six_key_mode(const kbc_profile *p);
KBCORE_API uint8_t kbc_profile_get_custom_total(const kbc_profile *p);
KBCORE_API uint8_t kbc_profile_get_custom_current(const kbc_profile *p);
KBCORE_API uint8_t kbc_profile_get_led_state(const kbc_profile *p);

KBCORE_API kbc_status kbc_profile_encode_select(uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_encode_select_logical(size_t logical, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_encode_key_matrix(const kbc_profile *p, uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_decode_key_matrix(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile);

KBCORE_API kbc_status kbc_profile_encode_params_b(const kbc_profile *p, uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_decode_params_b(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile);
KBCORE_API kbc_status kbc_profile_decode_block_shifted(kbc_profile *p, const uint8_t *buf, size_t len, int apply_shift, uint8_t *hw_profile);

KBCORE_API kbc_status kbc_profile_encode_extra(const kbc_profile *p, uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_decode_extra(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile);

KBCORE_API kbc_status kbc_profile_encode_custom(kbc_profile *p, uint8_t hw_profile, uint8_t slot_index, uint8_t opcode, const uint8_t *data, size_t data_len, uint8_t *buf, size_t buf_len);

KBCORE_API kbc_status kbc_read_active_profile_decode(const uint8_t *buf, size_t len, uint8_t *hw_profile);

/* ------------------------------------------------------------------ */
/* Macros (opaque handle kbc_macro)                                    */
/* ------------------------------------------------------------------ */

typedef struct kbc_macro_step_public { uint8_t delay; int key_down; uint8_t keycode; } kbc_macro_step_public;

KBCORE_API kbc_macro *kbc_macro_create(void);
KBCORE_API void kbc_macro_destroy(kbc_macro *m);

KBCORE_API kbc_status kbc_macro_set_repetitions(kbc_macro *m, uint16_t reps);
KBCORE_API uint16_t kbc_macro_get_repetitions(const kbc_macro *m);
KBCORE_API kbc_status kbc_macro_add_step(kbc_macro *m, uint8_t delay, int key_down, uint8_t keycode);
KBCORE_API size_t kbc_macro_step_count(const kbc_macro *m);
KBCORE_API kbc_status kbc_macro_get_step(const kbc_macro *m, size_t index, uint8_t *delay, int *key_down, uint8_t *keycode);
KBCORE_API kbc_status kbc_macro_encode(const kbc_macro *m, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_macro_decode(kbc_macro *m, const uint8_t *buf, size_t len);
KBCORE_API kbc_status kbc_macro_encode_report(const kbc_macro *m, uint8_t hw_profile, uint8_t position, uint8_t *buf, size_t buf_len);

/* ------------------------------------------------------------------ */
/* LED / lighting helpers                                              */
/* ------------------------------------------------------------------ */

KBCORE_API kbc_status kbc_led_buffer_validate(const uint8_t *buf, size_t len);
KBCORE_API kbc_status kbc_led_set_rgb(uint8_t *buf, size_t led_count, size_t index, uint8_t r, uint8_t g, uint8_t b);
KBCORE_API kbc_status kbc_led_get_rgb(const uint8_t *buf, size_t led_count, size_t index, uint8_t *r, uint8_t *g, uint8_t *b);
KBCORE_API size_t kbc_led_page_count(size_t buffer_size);
KBCORE_API kbc_status kbc_led_encode_page(const uint8_t *buf, size_t buf_len, size_t page_index, uint8_t *out, size_t out_len);
KBCORE_API kbc_status kbc_led_encode_page_observed_single(const uint8_t *buf, size_t buf_len, uint8_t *out, size_t out_len);
KBCORE_API kbc_status kbc_led_encode_game_mode(const uint8_t *colors, size_t colors_len, uint8_t hw_profile, uint8_t *buf, size_t buf_len);

/* Planar LED frame with a configurable stride (positions per colour plane).
 * Layout: R plane at [0, stride), G plane at [stride, 2*stride), B plane at
 * [2*stride, 3*stride).  The frame buffer is always KBC_LED_BUFFER_SIZE bytes.
 * stride 1..KBC_MAX_LED_COUNT and 3*stride <= KBC_LED_BUFFER_SIZE. */
KBCORE_API size_t kbc_led_frame_page_count(size_t stride);
KBCORE_API kbc_status kbc_led_frame_set(uint8_t *frame, size_t stride, size_t pos, uint8_t r, uint8_t g, uint8_t b);
KBCORE_API kbc_status kbc_led_frame_fill(uint8_t *frame, size_t stride, size_t count, uint8_t r, uint8_t g, uint8_t b);
/* Applies the profile brightness scale (0..KBC_BRIGHTNESS_RAW_MAX) to a frame. */
KBCORE_API kbc_status kbc_led_frame_apply_brightness(uint8_t *frame, size_t stride,
                                                      uint8_t brightness);

/* Profile parameter block (response of 0x82 / payload of 0x02, path B).
 * Offsets are absolute within the 264-byte report. */
#define KBC_PBLK_OFF_RATE        8   /* report rate index + 1 */
#define KBC_PBLK_OFF_MODE        11  /* CONFIRMED on hardware: LED effect index */
#define KBC_PBLK_OFF_BRIGHTNESS  12  /* CONFIRMED on hardware: linear 0..10, step 1 */
#define KBC_PBLK_OFF_DIRECTION   13  /* animation direction, 0..3 */
#define KBC_PBLK_OFF_SPEED       19  /* one byte shared by all effects; lower = faster */
#define KBC_PBLK_OFF_CUSTOM_COUNT 134 /* "custom LED count": a change to 1 or 255 makes the keyboard commit the block to flash */
#define KBC_PBLK_OFF_CUSTOM_CURRENT 135 /* current custom LED slot index */
#define KBC_PBLK_OFF_LED_STATE 136      /* LED on/off state byte */
#define KBC_PBLK_OFF_COLOR_BASE 92      /* first per-effect colour slot (planar R/G/B planes) */
#define KBC_COMMIT_ARM           1
#define KBC_COMMIT_CLEAN         255   /* factory value, also the final state after a save */
#define KBC_BRIGHTNESS_RAW_MAX   10
#define KBC_SPEED_RAW_MIN        2   /* fastest observed; lower value = faster */
#define KBC_SPEED_RAW_MAX        102 /* slowest observed (0x66) */

/* Per-effect colour slots (absolute offsets; planar R,G,B planes of `count` entries each).
 * Effects without slots: 2 (cycle = rainbow) and 11 (game mode). */
#define KBC_PBLK_OFF_SPECTRUM    32  /* 9 per-effect "factory rainbow" flags, 32..40; non-zero hijacks the palette */
#define KBC_PBLK_SPECTRUM_COUNT  9
#define KBC_MAX_EFFECT_COLORS    14  /* wave */
KBCORE_API int kbc_effect_color_slots(uint8_t effect, size_t *offset, size_t *count);   /* 1 if the effect has slots */
/* Builds a 0x02 write report that sets the colours of `effect` (1..count given; slots
 * are filled cyclically when fewer colours than slots are given) and clears the nine
 * spectrum flags, as the official driver does on every write. Nothing else changes. */
KBCORE_API kbc_status kbc_profile_block_set_colors(const uint8_t *block, size_t block_len, uint8_t profile,
                                                   uint8_t effect, const uint8_t (*rgb)[3], size_t rgb_count,
                                                   uint8_t *out, size_t out_len);

/* Converts a 0x82 response into a 0x02 write report (same data, header fixed). */
KBCORE_API kbc_status kbc_profile_block_to_write(const uint8_t *block, size_t block_len, uint8_t profile, uint8_t *out, size_t out_len);
/* Builds a 0x02 report that carries `block` unchanged except byte 134 = `count`
 * (KBC_COMMIT_ARM or KBC_COMMIT_CLEAN). The block must be "clean" (byte 134 == 255)
 * and the effect <= 12 (14/15 are never saved): otherwise KBC_ERR_UNSUPPORTED. */
KBCORE_API kbc_status kbc_profile_block_commit_report(const uint8_t *block, size_t block_len, uint8_t profile, uint8_t count, uint8_t *out, size_t out_len);
typedef enum {
    KBC_FIELD_MODE = 1,
    KBC_FIELD_BRIGHTNESS = 2,
    KBC_FIELD_SPEED = 3,
    KBC_FIELD_DIRECTION = 4,
    KBC_FIELD_REPORT_RATE = 5
} kbc_profile_field;
/* Builds a 0x02 write report that changes exactly ONE byte of the block:
 *  MODE        effect index (same rules as kbc_profile_block_set_mode)
 *  BRIGHTNESS  0..KBC_BRIGHTNESS_RAW_MAX
 *  SPEED       raw KBC_SPEED_RAW_MIN..MAX, one byte (KBC_PBLK_OFF_SPEED) for every effect;
 *              lower = faster */
KBCORE_API kbc_status kbc_profile_block_set_field(const uint8_t *block, size_t block_len, uint8_t profile, kbc_profile_field field, uint8_t value, uint8_t *out, size_t out_len);
/* Same, and sets the LED effect. Allowed modes: 0..10 and 12 (11 = game mode
 * and 15 = custom need extra commands and are rejected). */
KBCORE_API kbc_status kbc_profile_block_set_mode(const uint8_t *block, size_t block_len, uint8_t profile, uint8_t mode, uint8_t *out, size_t out_len);

/* Per-key custom LED data stored in the keyboard (effect 15, flash-backed).
 * One slot = three reports: 0x08 colours (planar R/G/B x 70), 0x09 frames (one frame
 * holding every key) and 0x0A effects (static).  The slot only becomes active, and is
 * committed to flash, by the 0x02 block write built by kbc_profile_block_custom_report.
 *
 * Only slot 0 can be activated/committed: the keyboard commits a block when byte 134
 * changes to 1 or 255, and slot 0 is the only slot whose count is 1.  Slots 1..4 are
 * still accepted by kbc_custom_slot_reports for raw writes, but
 * kbc_profile_block_custom_report returns KBC_ERR_UNSUPPORTED for them. */
#define KBC_CUSTOM_KEYS          70   /* = KBC_KEY_POSITIONS */
#define KBC_CUSTOM_SLOTS         5
#define KBC_LED_EFFECT_CUSTOM    15
#define KBC_CUSTOM_EFFECT_STATIC 2
/* out[0]=0x08, out[1]=0x09, out[2]=0x0A reports for `slot` (0..4) of `hw_profile` (1..4). */
KBCORE_API kbc_status kbc_custom_slot_reports(uint8_t hw_profile, uint8_t slot,
                                              const uint8_t rgb[KBC_CUSTOM_KEYS][3],
                                              uint8_t out[3][KBC_FEATURE_REPORT_SIZE]);
/* 0x02 report: `block` unchanged except effect = 15, slot count = 1 (byte 134),
 * current slot = 0 (byte 135) and the spectrum flags cleared.  A change of byte 134
 * to 1 makes the keyboard commit the block to flash.  Only slot 0 is supported;
 * KBC_ERR_UNSUPPORTED is returned for other slots and for unknown block states
 * (effect 14 or a count outside 1..5/255).
 *
 * If the block is already at count 1 (a previous custom save), writing this report
 * does not change byte 134 and therefore does not commit.  Use
 * kbc_profile_block_custom_arm_report() first; the 1 -> 255 -> 1 sequence commits
 * twice and ends in the same custom state. */
KBCORE_API kbc_status kbc_profile_block_custom_report(const uint8_t *block, size_t block_len,
                                                      uint8_t profile, uint8_t slot,
                                                      uint8_t *out, size_t out_len);

/* Same as kbc_profile_block_custom_report(..., slot 0) but with byte 134 set to 255.
 * Used as the intermediate step when the block is already at count 1: changing 1 -> 255
 * commits the old slot data, then the final report changes 255 -> 1 and commits the new
 * slot data. */
KBCORE_API kbc_status kbc_profile_block_custom_arm_report(const uint8_t *block, size_t block_len,
                                                          uint8_t profile, uint8_t *out, size_t out_len);

/* 0x02 report that re-selects the stored custom layout without touching the slot data:
 * effect = 15, current slot = 0 and the spectrum flags cleared.  Byte 134 (count) is
 * left unchanged, so this does not commit to flash; the layout must already be stored
 * (count 1..KBC_CUSTOM_SLOTS).  KBC_ERR_UNSUPPORTED when there is no custom data or
 * the block is in the unknown effect-14 state. */
KBCORE_API kbc_status kbc_profile_block_custom_select_report(const uint8_t *block, size_t block_len,
                                                             uint8_t profile, uint8_t *out, size_t out_len);

/* 0x02 report that leaves custom mode: byte 134 back to 255 (commits to flash) and, if the
 * effect is 15, effect 0 (wave). Everything else unchanged. KBC_ERR_UNSUPPORTED when the
 * block holds no custom data (byte 134 not in 1..KBC_CUSTOM_SLOTS) or is effect 14. */
KBCORE_API kbc_status kbc_profile_block_custom_clear_report(const uint8_t *block, size_t block_len,
                                                            uint8_t profile, uint8_t *out, size_t out_len);

/* Built-in LED effects (hardware mode index 0..12; 14 = observed device
 * mode preserved by profile writes; 15 = custom). */
#define KBC_LED_EFFECT_COUNT   13
#define KBC_LED_EFFECT_LIGHTING 12   /* constant single colour */
#define KBC_LED_EFFECT_OBSERVED_14 14
#define KBC_LED_SPEED_MIN       2
#define KBC_LED_SPEED_MAX       102
#define KBC_LED_BRIGHTNESS_MIN  0
#define KBC_LED_BRIGHTNESS_MAX  10
KBCORE_API const char *kbc_led_effect_name(uint8_t mode);

/* MK61L/SK61L key names <-> matrix positions (index = column * 5 + row).
 * Names are lowercase: esc tab caps lshift rshift lctrl rctrl lalt ralt lwin menu fn
 * space enter backspace backslash, 1..0 a..z, minus equals lbracket rbracket
 * semicolon quote comma period slash; many aliases ("-", "escape", "win"...) and
 * case-insensitive. Ambiguous names (shift, ctrl, alt) return -1. */
#define KBC_KEY_POSITIONS 70
KBCORE_API int kbc_key_position(const char *name);        /* -1 if unknown */
KBCORE_API const char *kbc_key_name(size_t pos);          /* NULL: no key at that position */
/* Groups: wasd, numbers, letters, mods, all. Returns how many positions were written. */
KBCORE_API size_t kbc_key_group(const char *group, int *out, size_t max);

/* Key remap (0x03): 3 layers x 70 positions of raw HID codes (0 normal, 1 Fn, 2 custom).
 * kbc_keymap_default fills one layer with the factory map (0 on success, -1 on bad layer).
 * kbc_hid_code parses a name ("a", "f5", "lctrl", "play", ...) or "0xNN"; -1 if unknown
 * (0xB2, the macro marker, is refused: it needs macro data). */
#define KBC_KEYMAP_LAYERS 3
KBCORE_API int kbc_keymap_default(size_t layer, uint8_t out[KBC_KEY_POSITIONS]);
KBCORE_API int kbc_hid_code(const char *name);

/* Host-computed animations (streamed as 0x07 frames at 25 fps by kbdd).
 * Pure functions: no I/O. Colour planes use the same stride layout as frames. */
typedef enum { KBC_ANIM_BREATH = 1, KBC_ANIM_CYCLE = 2, KBC_ANIM_RAINBOW = 3 } kbc_led_anim;
#define KBC_MATRIX_ROWS        5        /* matrix index = column * 5 + row */
#define KBC_ANIM_PERIOD_MIN    200u
#define KBC_ANIM_PERIOD_MAX    60000u
KBCORE_API uint32_t kbc_led_anim_default_period(kbc_led_anim kind);
/* Renders the frame at time t_ms. breath: (r,g,b) pulsing; cycle: whole keyboard
 * hue rotation; rainbow: hue moving across the matrix columns. `count` positions
 * (<= stride) are written; the rest of the 3*stride bytes are zeroed. */
KBCORE_API kbc_status kbc_led_anim_render(kbc_led_anim kind, uint32_t t_ms, uint32_t period_ms,
                                          uint8_t r, uint8_t g, uint8_t b,
                                          uint8_t *frame, size_t stride, size_t count);

/* 0x01 select profile (1..4 only; the factory-reset value 0xFF is rejected). */
KBCORE_API kbc_status kbc_build_select_profile(uint8_t profile, uint8_t *buf, size_t buf_len);

/* Query requests: opcode 0x81 (active profile) or 0x82 (profile block,
 * profile 1..4).  Builds a 264-byte feature report. */
KBCORE_API kbc_status kbc_build_read_request(uint8_t opcode, uint8_t profile, uint8_t *buf, size_t buf_len);


#ifdef __cplusplus
}
#endif



#endif /* KBCORE_H */
