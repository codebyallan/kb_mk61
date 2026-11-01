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

#define KBCORE_VERSION_MAJOR 0
#define KBCORE_VERSION_MINOR 3
#define KBCORE_VERSION_PATCH 2
#define KBCORE_VERSION_STRING "0.3.2"

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
#define KBC_REPORT_ID_FEATURE 0x07
#define KBC_INPUT_REPORT_ID   0x04
#define KBC_REPORT_ID_INPUT   0x04

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

/* Number of feature-report opcodes in the allowlist. */
#define KBC_OPCODE_COUNT 13

/* Feature-report opcodes. */
#define KBC_OP_SELECT_PROFILE       0x01
#define KBC_OPCODE_SELECT_PROFILE   0x01
#define KBC_OP_PROFILE_PARAMS       0x02
#define KBC_OPCODE_PROFILE_PARAMS   0x02
#define KBC_OP_KEY_MATRIX           0x03
#define KBC_OPCODE_KEY_MATRIX       0x03
#define KBC_OP_LED_DATA             0x04
#define KBC_OPCODE_LED_DATA         0x04
#define KBC_OP_MACRO                0x05
#define KBC_OPCODE_MACRO            0x05
#define KBC_OP_ENABLE_INPUT         0x06
#define KBC_OPCODE_ENABLE_INPUT     0x06
#define KBC_OP_LED_PAGE             0x07
#define KBC_OPCODE_LED_PAGE         0x07
#define KBC_OP_CUSTOM_COLOR         0x08
#define KBC_OPCODE_CUSTOM_COLOR     0x08
#define KBC_OP_CUSTOM_FRAME         0x09
#define KBC_OPCODE_CUSTOM_FRAME     0x09
#define KBC_OP_CUSTOM_EFFECT        0x0A
#define KBC_OPCODE_CUSTOM_EFFECT    0x0A
#define KBC_OP_PROFILE_EXTRA        0x0B
#define KBC_OPCODE_PROFILE_EXTRA    0x0B
#define KBC_OP_READ_ACTIVE_PROFILE  0x81
#define KBC_OPCODE_READ_ACTIVE_PROFILE 0x81
#define KBC_OP_READ_PROFILE_BLOCK   0x82
#define KBC_OPCODE_READ_PROFILE_BLOCK 0x82

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

typedef kbc_feature_report kbc_feature_report_t;

typedef struct kbc_input_report {
    uint8_t report_id;       /* byte 0, must be 0x04 */
    uint8_t marker;          /* byte 1 */
    uint8_t subtype;         /* alias of marker / system subtype */
    uint8_t system_subtype;  /* byte 2 when marker == 0xE1 */
    uint8_t col;             /* column (0-based) */
    uint8_t column;          /* alias of col */
    uint8_t row;             /* row (0-based) */
    uint8_t line;            /* alias of row */
    uint8_t state;           /* state byte (pp) */
    uint8_t pressed_byte;    /* alias of state */
    uint8_t extra;           /* extra byte (GK8150) */
    uint8_t value;           /* byte 3 for system events (profile id, led mode, ...) */
    uint8_t id;              /* alias of value */
    uint8_t mode;            /* alias of value */
    uint8_t profile_id;      /* alias of value */
    uint8_t led_mode;        /* alias of value */
    uint16_t matrix_index;   /* col * 5 + row */
    uint16_t index;          /* alias of matrix_index */
    int pressed;             /* 1 if the event should be emitted */
    int emit;                /* alias of pressed */
    int has_event;           /* alias of pressed */
    uint8_t raw[KBC_INPUT_REPORT_MIN_SIZE];
} kbc_input_report;

typedef kbc_input_report kbc_input_report_t;

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
KBCORE_API kbc_status kbc_decode_feature_report(const uint8_t *buf, size_t len,
                                                kbc_feature_report *out);
KBCORE_API kbc_status kbc_read_feature_report(const uint8_t *buf, size_t len,
                                              kbc_feature_report *out);

/* Encode *in into buf (must have room for KBC_FEATURE_REPORT_SIZE bytes). */
KBCORE_API kbc_status kbc_encode_feature_report(const kbc_feature_report *in,
                                                 uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_decode_encode_feature_report(const kbc_feature_report *in,
                                                       uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_build_feature_report(uint8_t opcode, uint8_t param0,
                                               uint8_t param1,
                                               const uint8_t params[KBC_FEATURE_PARAMS_SIZE],
                                               const uint8_t *data, size_t data_len,
                                               uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_make_feature_report(uint8_t opcode, uint8_t param0,
                                              uint8_t param1,
                                              const uint8_t params[KBC_FEATURE_PARAMS_SIZE],
                                              const uint8_t *data, size_t data_len,
                                              uint8_t *buf, size_t buf_len);

/* Decode an input report 0x04 into *out. */
KBCORE_API kbc_status kbc_parse_input_report(const uint8_t *buf, size_t len,
                                              kbc_input_report *out);
KBCORE_API kbc_status kbc_decode_input_report(const uint8_t *buf, size_t len,
                                               kbc_input_report *out);

/* Convenience: validate a single report ID value. */
KBCORE_API kbc_status kbc_validate_report_id(uint8_t id, uint8_t expected);


/* ------------------------------------------------------------------ */
/* Profiles (opaque handle kbc_profile)                                 */
/* ------------------------------------------------------------------ */

KBCORE_API kbc_profile *kbc_profile_create(size_t matrix_size, size_t led_count);
KBCORE_API kbc_profile *kbc_profile_new(size_t matrix_size, size_t led_count);
KBCORE_API void kbc_profile_destroy(kbc_profile *p);
KBCORE_API void kbc_profile_free(kbc_profile *p);
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

KBCORE_API kbc_status kbc_profile_encode_params_a(const kbc_profile *p, uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_encode_params_b(const kbc_profile *p, uint8_t hw_profile, uint8_t *buf, size_t buf_len);
KBCORE_API kbc_status kbc_profile_decode_params_a(kbc_profile *p, const uint8_t *buf, size_t len, uint8_t *hw_profile);
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
KBCORE_API kbc_macro *kbc_macro_new(void);
KBCORE_API void kbc_macro_destroy(kbc_macro *m);
KBCORE_API void kbc_macro_free(kbc_macro *m);

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
#define KBC_PBLK_OFF_RATE        8   /* observed 1 on a default profile (inferred: report rate index + 1) */
#define KBC_PBLK_OFF_MODE        11  /* CONFIRMED on hardware: LED effect index */
#define KBC_PBLK_OFF_BRIGHTNESS  12  /* CONFIRMED on hardware: linear 0..10, step 1 */
#define KBC_PBLK_OFF_SPEED       19  /* CONFIRMED on hardware (block[11]): ONE byte shared by all effects; inverted, lower = faster */
#define KBC_BRIGHTNESS_RAW_MAX   10
#define KBC_SPEED_RAW_MIN        2   /* fastest observed; lower value = faster */
#define KBC_SPEED_RAW_MAX        102 /* slowest observed (0x66) */

/* Converts a 0x82 response into a 0x02 write report (same data, header fixed). */
KBCORE_API kbc_status kbc_profile_block_to_write(const uint8_t *block, size_t block_len, uint8_t profile, uint8_t *out, size_t out_len);
typedef enum { KBC_FIELD_MODE = 1, KBC_FIELD_BRIGHTNESS = 2, KBC_FIELD_SPEED = 3 } kbc_profile_field;
/* Builds a 0x02 write report that changes exactly ONE byte of the block:
 *  MODE        effect index (same rules as kbc_profile_block_set_mode)
 *  BRIGHTNESS  0..KBC_BRIGHTNESS_RAW_MAX
 *  SPEED       raw KBC_SPEED_RAW_MIN..MAX, one byte (KBC_PBLK_OFF_SPEED) for every effect;
 *              effects 0..11 only); lower = faster */
KBCORE_API kbc_status kbc_profile_block_set_field(const uint8_t *block, size_t block_len, uint8_t profile, kbc_profile_field field, uint8_t value, uint8_t *out, size_t out_len);
/* Same, and sets the LED effect. Allowed modes: 0..10 and 12 (11 = game mode
 * and 15 = custom need extra commands and are rejected). */
KBCORE_API kbc_status kbc_profile_block_set_mode(const uint8_t *block, size_t block_len, uint8_t profile, uint8_t mode, uint8_t *out, size_t out_len);

/* Built-in LED effects (hardware mode index 0..12; 14 = observed device
 * mode preserved by profile writes; 15 = custom). */
#define KBC_LED_EFFECT_COUNT   13
#define KBC_LED_EFFECT_LIGHTING 12   /* constant single colour */
#define KBC_LED_EFFECT_OBSERVED_14 14
#define KBC_LED_SPEED_MIN       1
#define KBC_LED_SPEED_MAX       3
#define KBC_LED_BRIGHTNESS_MIN  0
#define KBC_LED_BRIGHTNESS_MAX  5
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



/* ------------------------------------------------------------------ */
/* Convenience aliases                                                 */
/* ------------------------------------------------------------------ */
#define kbc_profile_new kbc_profile_create
#define kbc_profile_free kbc_profile_destroy
#define kbc_macro_new kbc_macro_create
#define kbc_macro_free kbc_macro_destroy

#define kbc_encode_select_profile kbc_profile_encode_select
#define kbc_encode_key_matrix kbc_profile_encode_key_matrix
#define kbc_encode_profile_params_a kbc_profile_encode_params_a
#define kbc_encode_profile_params_b kbc_profile_encode_params_b
#define kbc_decode_profile_params_a kbc_profile_decode_params_a
#define kbc_decode_profile_params_b kbc_profile_decode_params_b
#define kbc_encode_profile_extra kbc_profile_encode_extra
#define kbc_decode_profile_extra kbc_profile_decode_extra

#endif /* KBCORE_H */
