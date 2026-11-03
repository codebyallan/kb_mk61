#include "kbcore.h"

#include <string.h>

static int opcode_in_allowlist(uint8_t opcode)
{
    switch (opcode) {
        case KBC_OP_SELECT_PROFILE:
        case KBC_OP_PROFILE_PARAMS:
        case KBC_OP_KEY_MATRIX:
        case KBC_OP_LED_DATA:
        case KBC_OP_MACRO:
        case KBC_OP_ENABLE_INPUT:
        case KBC_OP_LED_PAGE:
        case KBC_OP_CUSTOM_COLOR:
        case KBC_OP_CUSTOM_FRAME:
        case KBC_OP_CUSTOM_EFFECT:
        case KBC_OP_PROFILE_EXTRA:
        case KBC_OP_READ_ACTIVE_PROFILE:
        case KBC_OP_READ_PROFILE_BLOCK:
            return 1;
        default:
            return 0;
    }
}

kbc_status kbc_is_valid_opcode(uint8_t opcode)
{
    return opcode_in_allowlist(opcode) ? KBC_OK : KBC_ERR_PROTOCOL;
}

kbc_status kbc_validate_opcode(uint8_t opcode)
{
    return kbc_is_valid_opcode(opcode);
}

kbc_status kbc_validate_report_id(uint8_t id, uint8_t expected)
{
    return (id == expected) ? KBC_OK : KBC_ERR_PROTOCOL;
}

kbc_status kbc_validate_feature_report(const uint8_t *buf, size_t len)
{
    if (buf == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (len != KBC_FEATURE_REPORT_SIZE) {
        return KBC_ERR_PROTOCOL;
    }
    if (buf[0] != KBC_FEATURE_REPORT_ID) {
        return KBC_ERR_PROTOCOL;
    }
    if (!opcode_in_allowlist(buf[1])) {
        return KBC_ERR_PROTOCOL;
    }

    {
        uint8_t op = buf[1];

        if (op == KBC_OP_SELECT_PROFILE) {
            uint8_t p = buf[2];
            /* The factory-reset value (0xFF) must never be sent for profile
             * selection; only hardware profiles 1..4 are valid. */
            if (p < 1 || p > 4) {
                return KBC_ERR_PROTOCOL;
            }
        } else if (op == KBC_OP_PROFILE_PARAMS || op == KBC_OP_KEY_MATRIX ||
                   op == KBC_OP_LED_DATA || op == KBC_OP_MACRO ||
                   op == KBC_OP_CUSTOM_COLOR || op == KBC_OP_CUSTOM_FRAME ||
                   op == KBC_OP_CUSTOM_EFFECT || op == KBC_OP_PROFILE_EXTRA ||
                   op == KBC_OP_READ_PROFILE_BLOCK) {
            uint8_t p = buf[2];
            if (p < 1 || p > 4) {
                return KBC_ERR_PROTOCOL;
            }
        } else if (op == KBC_OP_ENABLE_INPUT) {
            if (buf[2] != 1) {
                return KBC_ERR_PROTOCOL;
            }
        } else if (op == KBC_OP_LED_PAGE) {
            if (buf[2] > 1) {
                return KBC_ERR_PROTOCOL;
            }
        } else if (op == KBC_OP_READ_ACTIVE_PROFILE) {
            /* byte 2 ignored by spec (0 in TV-F14). */
        }

        if (op == KBC_OP_KEY_MATRIX) {
            if (buf[3] != 0) {
                return KBC_ERR_PROTOCOL;
            }
        }
        if (op == KBC_OP_MACRO) {
            if (buf[3] >= KBC_MAX_MATRIX_SIZE) {
                return KBC_ERR_PROTOCOL;
            }
        }
        if (op == KBC_OP_CUSTOM_COLOR || op == KBC_OP_CUSTOM_FRAME ||
            op == KBC_OP_CUSTOM_EFFECT) {
            if (buf[3] > 4) {
                return KBC_ERR_PROTOCOL;
            }
        }
        if (op == KBC_OP_PROFILE_EXTRA) {
            /* direction/mode/winlock are bytes 11..13; no strict range enforced here. */
        }
    }

    return KBC_OK;
}

kbc_status kbc_validate_input_report(const uint8_t *buf, size_t len)
{
    if (buf == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (len < KBC_INPUT_REPORT_MIN_SIZE) {
        return KBC_ERR_PROTOCOL;
    }
    if (buf[0] != KBC_INPUT_REPORT_ID) {
        return KBC_ERR_PROTOCOL;
    }
    return KBC_OK;
}

static void fill_feature_report(const uint8_t *buf, kbc_feature_report *out)
{
    size_t i;

    memset(out, 0, sizeof(*out));
    out->report_id = buf[0];
    out->opcode = buf[1];
    out->param0 = buf[2];
    out->param1 = buf[3];
    for (i = 0; i < KBC_FEATURE_PARAMS_SIZE; i++) {
        out->params[i] = buf[4 + i];
    }
    out->param[0] = out->param0;
    out->param[1] = out->param1;
    for (i = 0; i < KBC_FEATURE_PARAMS_SIZE; i++) {
        out->param[2 + i] = out->params[i];
    }
    memcpy(out->data, buf + KBC_FEATURE_DATA_OFFSET, KBC_FEATURE_DATA_SIZE);
    out->data_len = KBC_FEATURE_DATA_SIZE;
}

kbc_status kbc_parse_feature_report(const uint8_t *buf, size_t len,
                                   kbc_feature_report *out)
{
    kbc_status st;

    if (out == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    st = kbc_validate_feature_report(buf, len);
    if (st != KBC_OK) {
        return st;
    }
    fill_feature_report(buf, out);
    return KBC_OK;
}

static kbc_status encode_feature_from_fields(uint8_t opcode, uint8_t param0,
                                            uint8_t param1,
                                            const uint8_t params[KBC_FEATURE_PARAMS_SIZE],
                                            const uint8_t *data, size_t data_len,
                                            uint8_t *buf, size_t buf_len)
{
    size_t i;

    if (buf == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (buf_len != KBC_FEATURE_REPORT_SIZE) {
        return KBC_ERR_PROTOCOL;
    }
    if (!opcode_in_allowlist(opcode)) {
        return KBC_ERR_PROTOCOL;
    }
    if (data_len > KBC_FEATURE_DATA_SIZE) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    if (data == NULL && data_len != 0) {
        return KBC_ERR_INVALID_ARGUMENT;
    }

    memset(buf, 0, KBC_FEATURE_REPORT_SIZE);
    buf[0] = KBC_FEATURE_REPORT_ID;
    buf[1] = opcode;
    buf[2] = param0;
    buf[3] = param1;
    if (params != NULL) {
        for (i = 0; i < KBC_FEATURE_PARAMS_SIZE; i++) {
            buf[4 + i] = params[i];
        }
    }
    if (data != NULL && data_len > 0) {
        memcpy(buf + KBC_FEATURE_DATA_OFFSET, data, data_len);
    }
    return KBC_OK;
}

kbc_status kbc_encode_feature_report(const kbc_feature_report *in,
                                    uint8_t *buf, size_t buf_len)
{
    if (in == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    return encode_feature_from_fields(in->opcode, in->param0, in->param1,
                                     in->params, in->data, in->data_len,
                                     buf, buf_len);
}

kbc_status kbc_build_feature_report(uint8_t opcode, uint8_t param0,
                                   uint8_t param1,
                                   const uint8_t params[KBC_FEATURE_PARAMS_SIZE],
                                   const uint8_t *data, size_t data_len,
                                   uint8_t *buf, size_t buf_len)
{
    return encode_feature_from_fields(opcode, param0, param1, params, data,
                                     data_len, buf, buf_len);
}

/* Input report parsing. */

static void fill_input_report(const uint8_t *buf, kbc_input_report *out)
{
    memset(out, 0, sizeof(*out));
    out->report_id = buf[0];
    out->marker = buf[1];
    out->subtype = buf[1];
    memcpy(out->raw, buf, KBC_INPUT_REPORT_MIN_SIZE);

    if (buf[1] == 0xF1 && buf[2] == 0xF2) {
        out->col = buf[3];
        out->row = buf[4];
        out->state = buf[5];
        out->matrix_index = (uint16_t)buf[3] * 5u + (uint16_t)buf[4];
        out->pressed = (buf[5] != 0) ? 1 : 0;
        out->subtype = 0xF1;
    } else if (buf[1] == 0xF5) {
        out->col = buf[2];
        out->row = buf[3];
        out->extra = buf[4];
        out->state = buf[5];
        out->matrix_index = (uint16_t)buf[2] * 5u + (uint16_t)buf[3];
        out->pressed = (buf[5] == 0x01) ? 1 : 0;
        out->subtype = 0xF5;
    } else if (buf[1] == 0xE1) {
        out->system_subtype = buf[2];
        out->subtype = buf[2];
        out->value = buf[3];
        out->state = buf[3];
        out->pressed = 0;
    } else {
        out->subtype = buf[1];
        out->state = (KBC_INPUT_REPORT_MIN_SIZE > 5) ? buf[5] : 0;
    }
}

kbc_status kbc_parse_input_report(const uint8_t *buf, size_t len,
                                 kbc_input_report *out)
{
    kbc_status st;

    if (out == NULL) {
        return KBC_ERR_INVALID_ARGUMENT;
    }
    st = kbc_validate_input_report(buf, len);
    if (st != KBC_OK) {
        return st;
    }
    fill_input_report(buf, out);
    return KBC_OK;
}