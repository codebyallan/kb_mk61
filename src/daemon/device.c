#include "kbdd.h"

#include "kbcore.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- hidraw adapter ---------------------------------------------------- */

static ssize_t hr_set(void *ctx, const uint8_t *buf, size_t len)
{
    return kbdd_hidraw_set_feature((const kbdd_hid_device_t *)ctx, buf, len);
}

static ssize_t hr_get(void *ctx, uint8_t *buf, size_t len)
{
    return kbdd_hidraw_get_feature((const kbdd_hid_device_t *)ctx, buf, len);
}

static void hr_sleep(void *ctx, unsigned ms)
{
    kbdd_hid_device_t *dev = (kbdd_hid_device_t *)ctx;
    unsigned slept = 0;
    const unsigned chunk_ms = 10u;

    while (slept < ms) {
        unsigned this_chunk = (ms - slept) < chunk_ms ? (ms - slept) : chunk_ms;
        struct timespec ts;
        ts.tv_sec = (time_t)(this_chunk / 1000u);
        ts.tv_nsec = (long)(this_chunk % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
        slept += this_chunk;
        /* Yield to the daemon while blocked, so input reports (keyboard
         * notifications) keep being processed during long HID writes. */
        if (dev != NULL && dev->yield != NULL && dev->kctx != NULL) {
            dev->yield(dev->kctx);
        }
    }
}

__attribute__((weak))
void kbdd_dev_io_init_hidraw(kbdd_dev_io_t *io, const kbdd_hid_device_t *dev)
{
    io->ctx = (void *)dev;
    io->set_feature = hr_set;
    io->get_feature = hr_get;
    io->sleep_ms = hr_sleep;
}

/* ---- allowlisted operations -------------------------------------------- */

int kbdd_dev_query(const kbdd_dev_io_t *io, uint8_t opcode, uint8_t profile,
                   uint8_t out[264])
{
    uint8_t req[KBC_FEATURE_REPORT_SIZE];
    uint8_t rsp[KBC_FEATURE_REPORT_SIZE];
    ssize_t n;

    if (io == NULL || out == NULL) return KBDD_DEV_EINVAL;
    if (opcode != KBC_OP_READ_ACTIVE_PROFILE &&
        opcode != KBC_OP_READ_PROFILE_BLOCK) {
        return KBDD_DEV_EINVAL;
    }
    if (kbc_build_read_request(opcode, profile, req, sizeof(req)) != KBC_OK) {
        return KBDD_DEV_EINVAL;
    }
    if (io->set_feature(io->ctx, req, sizeof(req)) < 0) return KBDD_DEV_EIO;

    /* spec: 100 ms after 0x81, 50 ms after 0x82 */
    io->sleep_ms(io->ctx, opcode == KBC_OP_READ_ACTIVE_PROFILE ? 100u : 50u);

    memset(rsp, 0, sizeof(rsp));
    rsp[0] = KBC_FEATURE_REPORT_ID;
    n = io->get_feature(io->ctx, rsp, sizeof(rsp));
    if (n < 0) return KBDD_DEV_EIO;
    /* A short read leaves the tail zeroed and would pass for a real block, so
     * require the full report, echoing the opcode (and profile) we asked for. */
    if (n != KBC_FEATURE_REPORT_SIZE || rsp[0] != KBC_FEATURE_REPORT_ID ||
        rsp[1] != opcode) {
        return KBDD_DEV_EPROTO;
    }
    if (opcode == KBC_OP_READ_PROFILE_BLOCK ? rsp[2] != profile
                                            : (rsp[2] < 1 || rsp[2] > 4)) {
        return KBDD_DEV_EPROTO;
    }
    memcpy(out, rsp, sizeof(rsp));
    return KBDD_DEV_OK;
}

int kbdd_dev_send_frame(const kbdd_dev_io_t *io, const uint8_t frame[432],
                        size_t stride)
{
    uint8_t rep[KBC_FEATURE_REPORT_SIZE];
    size_t pages, p;

    if (io == NULL || frame == NULL) return KBDD_DEV_EINVAL;
    pages = kbc_led_frame_page_count(stride);
    if (pages == 0) return KBDD_DEV_EINVAL;

    for (p = 0; p < pages; p++) {
        if (kbc_led_encode_page(frame, KBC_LED_BUFFER_SIZE, p, rep,
                                sizeof(rep)) != KBC_OK) {
            return KBDD_DEV_EINVAL;
        }
        /* allowlist: LED page reports only */
        if (rep[0] != KBC_FEATURE_REPORT_ID || rep[1] != KBC_OP_LED_PAGE) {
            return KBDD_DEV_EINVAL;
        }
        if (io->set_feature(io->ctx, rep, sizeof(rep)) < 0) return KBDD_DEV_EIO;
        if (p + 1 < pages) io->sleep_ms(io->ctx, 15u); /* spec: 15 ms/page */
    }
    return KBDD_DEV_OK;
}

int kbdd_dev_enable_notifications(const kbdd_dev_io_t *io)
{
    uint8_t req[KBC_FEATURE_REPORT_SIZE];
    static const uint8_t params[KBC_FEATURE_PARAMS_SIZE] = {0, 0, 0, 0};

    if (io == NULL ||
        kbc_build_feature_report(KBC_OP_ENABLE_INPUT, 1, 0, params, NULL, 0,
                                 req, sizeof(req)) != KBC_OK) {
        return KBDD_DEV_EINVAL;
    }
    if (io->set_feature(io->ctx, req, sizeof(req)) < 0) return KBDD_DEV_EIO;
    io->sleep_ms(io->ctx, 100u);
    return KBDD_DEV_OK;
}

static char g_diag[256];

const char *kbdd_dev_last_diag(void)
{
    return g_diag;
}

void kbdd_dev_clear_diag(void)
{
    g_diag[0] = '\0';
}

/* Appends "@off want 0x.. got 0x.." for the first differences (bytes 3..263). */
static void diag_append_diffs(const uint8_t *want, const uint8_t *got)
{
    size_t i, shown = 0, total = 0, len = strlen(g_diag);

    for (i = 3; i < 264; i++) {
        if (want[i] == got[i]) continue;
        total++;
        if (shown < 6 && len + 40 < sizeof(g_diag)) {
            len += (size_t)snprintf(g_diag + len, sizeof(g_diag) - len,
                                    "@%zu want 0x%02x got 0x%02x; ", i, want[i], got[i]);
            shown++;
        }
    }
    if (total > shown && len + 24 < sizeof(g_diag)) {
        (void)snprintf(g_diag + len, sizeof(g_diag) - len, "(+%zu more) ", total - shown);
    }
}

static int edit_profile(const kbdd_dev_io_t *io, int field, uint8_t value,
                        const kbdd_color_edit_t *ce, uint8_t *profile_out)
{
    uint8_t act[264], before[264], after[264], rep[264], undo[264], chk[264];
    uint8_t prof;
    kbc_status st;
    int rc;

    g_diag[0] = '\0';
    if (io == NULL) return KBDD_DEV_EINVAL;
    rc = kbdd_dev_query(io, KBC_OP_READ_ACTIVE_PROFILE, 0, act);
    if (rc != KBDD_DEV_OK) return rc;
    /* hardware reply: 07 81 <profile> ... */
    if (act[1] != KBC_OP_READ_ACTIVE_PROFILE) return KBDD_DEV_EPROTO;
    prof = act[2];
    if (prof < 1 || prof > 4) return KBDD_DEV_EPROTO;

    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, before);
    if (rc != KBDD_DEV_OK) return rc;

    if (ce != NULL) {
        st = kbc_profile_block_set_colors(before, sizeof(before), prof, ce->effect,
                                          (const uint8_t (*)[3])ce->rgb, ce->count,
                                          rep, sizeof(rep));
    } else {
        st = kbc_profile_block_set_field(before, sizeof(before), prof,
                                         (kbc_profile_field)field, value, rep, sizeof(rep));
    }
    if (st == KBC_ERR_RANGE || st == KBC_ERR_INVALID_ARGUMENT) return KBDD_DEV_EINVAL;
    if (st != KBC_OK) {
        (void)snprintf(g_diag, sizeof(g_diag),
                       "profile %u: reply header %02x %02x %02x, effect byte %u",
                       (unsigned)prof, before[0], before[1], before[2],
                       (unsigned)before[KBC_PBLK_OFF_MODE]);
        return KBDD_DEV_EPROTO;
    }
    if (profile_out != NULL) *profile_out = prof;

    if (io->set_feature(io->ctx, rep, sizeof(rep)) < 0) return KBDD_DEV_EIO;
    io->sleep_ms(io->ctx, 200u);

    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, after);
    if (rc != KBDD_DEV_OK) return rc;

    /* the keyboard must now report exactly what we wrote (data + params) */
    if (memcmp(after + 3, rep + 3, sizeof(after) - 3) == 0) return KBDD_DEV_OK;

    /* maybe the firmware was just slow: wait and read once more */
    io->sleep_ms(io->ctx, 400u);
    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, after);
    if (rc != KBDD_DEV_OK) return rc;
    if (memcmp(after + 3, rep + 3, sizeof(after) - 3) == 0) return KBDD_DEV_OK;

    (void)snprintf(g_diag, sizeof(g_diag), "profile %u, differences vs written block: ",
                   (unsigned)prof);
    diag_append_diffs(rep, after);

    /* roll back to exactly what the keyboard reported before, and check it */
    if (kbc_profile_block_to_write(before, sizeof(before), prof, undo, sizeof(undo)) == KBC_OK) {
        (void)io->set_feature(io->ctx, undo, sizeof(undo));
        io->sleep_ms(io->ctx, 300u);
        if (kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, chk) == KBDD_DEV_OK &&
            memcmp(chk + 3, before + 3, sizeof(chk) - 3) == 0) {
            strncat(g_diag, "original block restored (confirmed)",
                    sizeof(g_diag) - strlen(g_diag) - 1);
        } else {
            strncat(g_diag, "RESTORE NOT CONFIRMED: check with kbctl dump-block",
                    sizeof(g_diag) - strlen(g_diag) - 1);
        }
    }
    return KBDD_DEV_EVERIFY;
}

int kbdd_dev_set_field(const kbdd_dev_io_t *io, int field, uint8_t value, uint8_t *profile_out)
{
    return edit_profile(io, field, value, NULL, profile_out);
}

int kbdd_dev_set_colors(const kbdd_dev_io_t *io, const kbdd_color_edit_t *ce, uint8_t *profile_out)
{
    if (ce == NULL || ce->count < 1 || ce->count > 14) return KBDD_DEV_EINVAL;
    return edit_profile(io, 0, 0, ce, profile_out);
}

int kbdd_dev_restore_profile(const kbdd_dev_io_t *io, uint8_t profile,
                             const uint8_t block[264])
{
    uint8_t current[264], write[264], check[264], undo[264];
    int rc;

    if (io == NULL || block == NULL || profile < 1 || profile > 4) {
        return KBDD_DEV_EINVAL;
    }
    if (block[0] != KBC_FEATURE_REPORT_ID ||
        block[1] != KBC_OP_READ_PROFILE_BLOCK ||
        block[2] != profile ||
        kbc_profile_block_to_write(block, sizeof(write), profile,
                                   write, sizeof(write)) != KBC_OK) {
        return KBDD_DEV_EPROTO;
    }

    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, profile, current);
    if (rc != KBDD_DEV_OK) return rc;
    if (io->set_feature(io->ctx, write, sizeof(write)) < 0) {
        return KBDD_DEV_EIO;
    }
    io->sleep_ms(io->ctx, 300u);

    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, profile, check);
    if (rc == KBDD_DEV_OK &&
        memcmp(check + 3, block + 3, sizeof(check) - 3) == 0) {
        return KBDD_DEV_OK;
    }

    if (kbc_profile_block_to_write(current, sizeof(current), profile,
                                   undo, sizeof(undo)) == KBC_OK &&
        io->set_feature(io->ctx, undo, sizeof(undo)) >= 0) {
        io->sleep_ms(io->ctx, 300u);
        if (kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, profile, check) == KBDD_DEV_OK &&
            memcmp(check + 3, current + 3, sizeof(check) - 3) == 0) {
            return KBDD_DEV_EVERIFY;
        }
    }
    return rc == KBDD_DEV_EIO ? KBDD_DEV_EIO : KBDD_DEV_EVERIFY;
}

int kbdd_dev_set_effect(const kbdd_dev_io_t *io, uint8_t mode, uint8_t *profile_out)
{
    return kbdd_dev_set_field(io, KBC_FIELD_MODE, mode, profile_out);
}

int kbdd_dev_switch_profile(const kbdd_dev_io_t *io, uint8_t profile, int *changed)
{
    uint8_t act[264], req[KBC_FEATURE_REPORT_SIZE], prev;
    int rc;

    if (changed != NULL) *changed = 0;
    if (io == NULL || kbc_build_select_profile(profile, req, sizeof(req)) != KBC_OK) {
        return KBDD_DEV_EINVAL;
    }
    rc = kbdd_dev_query(io, KBC_OP_READ_ACTIVE_PROFILE, 0, act);
    if (rc != KBDD_DEV_OK) return rc;
    if (act[1] != KBC_OP_READ_ACTIVE_PROFILE) return KBDD_DEV_EPROTO;
    prev = act[2];
    if (prev < 1 || prev > 4) return KBDD_DEV_EPROTO;
    if (prev == profile) return KBDD_DEV_OK;           /* nothing to do */

    if (io->set_feature(io->ctx, req, sizeof(req)) < 0) return KBDD_DEV_EIO;
    io->sleep_ms(io->ctx, 100u);

    rc = kbdd_dev_query(io, KBC_OP_READ_ACTIVE_PROFILE, 0, act);
    if (rc != KBDD_DEV_OK) return rc;
    if (act[1] == KBC_OP_READ_ACTIVE_PROFILE && act[2] == profile) {
        if (changed != NULL) *changed = 1;
        return KBDD_DEV_OK;
    }
    /* not confirmed: best-effort return to the previous profile */
    if (kbc_build_select_profile(prev, req, sizeof(req)) == KBC_OK) {
        (void)io->set_feature(io->ctx, req, sizeof(req));
        io->sleep_ms(io->ctx, 100u);
    }
    return KBDD_DEV_EVERIFY;
}

int kbdd_dev_save_profile(const kbdd_dev_io_t *io, uint8_t *profile_out)
{
    uint8_t act[264], cur[264], arm[264], clean[264], chk[264];
    uint8_t prof;
    kbc_status st;
    int rc, clean_rc;

    if (io == NULL) return KBDD_DEV_EINVAL;
    rc = kbdd_dev_query(io, KBC_OP_READ_ACTIVE_PROFILE, 0, act);
    if (rc != KBDD_DEV_OK) return rc;
    if (act[1] != KBC_OP_READ_ACTIVE_PROFILE) return KBDD_DEV_EPROTO;
    prof = act[2];
    if (prof < 1 || prof > 4) return KBDD_DEV_EPROTO;
    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, cur);
    if (rc != KBDD_DEV_OK) return rc;

    st = kbc_profile_block_commit_report(cur, sizeof(cur), prof, KBC_COMMIT_ARM, arm, sizeof(arm));
    if (st == KBC_ERR_UNSUPPORTED) return KBDD_DEV_ENOSAVE;
    if (st != KBC_OK) return KBDD_DEV_EPROTO;
    if (kbc_profile_block_commit_report(cur, sizeof(cur), prof, KBC_COMMIT_CLEAN,
                                        clean, sizeof(clean)) != KBC_OK) {
        return KBDD_DEV_EPROTO;
    }
    if (profile_out != NULL) *profile_out = prof;

    /* 1) arm: the keyboard must accept the write (byte 134 reads back as 1) */
    if (io->set_feature(io->ctx, arm, sizeof(arm)) < 0) return KBDD_DEV_EIO;
    io->sleep_ms(io->ctx, 200u);
    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, chk);
    if (rc != KBDD_DEV_OK) return rc;
    if (chk[KBC_PBLK_OFF_CUSTOM_COUNT] != KBC_COMMIT_ARM) return KBDD_DEV_EVERIFY; /* write ignored: nothing changed */

    /* 2) back to the clean value (this also commits); retry once if it fails */
    clean_rc = io->set_feature(io->ctx, clean, sizeof(clean));
    if (clean_rc < 0) {
        io->sleep_ms(io->ctx, 200u);
        clean_rc = io->set_feature(io->ctx, clean, sizeof(clean));
    }
    if (clean_rc < 0) {
        /*
         * The arm write may already have changed device state. Make one
         * explicit restoration attempt and verify it before reporting failure.
         */
        if (io->set_feature(io->ctx, clean, sizeof(clean)) < 0) return KBDD_DEV_EIO;
        io->sleep_ms(io->ctx, 200u);
        rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, chk);
        if (rc != KBDD_DEV_OK ||
            memcmp(chk + 3, cur + 3, sizeof(chk) - 3) != 0) {
            return rc != KBDD_DEV_OK ? rc : KBDD_DEV_EIO;
        }
        return KBDD_DEV_EVERIFY;
    }
    io->sleep_ms(io->ctx, 200u);
    rc = kbdd_dev_query(io, KBC_OP_READ_PROFILE_BLOCK, prof, chk);
    if (rc != KBDD_DEV_OK) return rc;
    if (memcmp(chk + 3, cur + 3, sizeof(chk) - 3) != 0) {
        (void)io->set_feature(io->ctx, clean, sizeof(clean));      /* best effort */
        return KBDD_DEV_EVERIFY;
    }
    return KBDD_DEV_OK;
}
