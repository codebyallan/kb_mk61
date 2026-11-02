/* Tests for the daemon device layer using a mock transport (no hardware). */
#include <stdio.h>
#include <string.h>

#include "kbcore.h"
#include "../../src/daemon/kbdd.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

struct mock {
    uint8_t sent[8][264];
    size_t nsent;
    unsigned sleeps[16];
    size_t nsleeps;
    int fail_set;
    int fail_get;
    uint8_t reply[264];
    ssize_t reply_len;
};

static ssize_t m_set(void *ctx, const uint8_t *buf, size_t len)
{
    struct mock *m = ctx;
    if (m->fail_set) return -1;
    if (len != 264 || m->nsent >= 8) return -1;
    memcpy(m->sent[m->nsent++], buf, len);
    return (ssize_t)len;
}

static ssize_t m_get(void *ctx, uint8_t *buf, size_t len)
{
    struct mock *m = ctx;
    if (m->fail_get) return -1;
    memcpy(buf, m->reply, len < 264 ? len : 264);
    return m->reply_len;
}

static void m_sleep(void *ctx, unsigned ms)
{
    struct mock *m = ctx;
    if (m->nsleeps < 16) m->sleeps[m->nsleeps++] = ms;
}

static void m_sleep_nop(void *ctx, unsigned ms) { (void)ctx; (void)ms; }

static void mock_init(struct mock *m, kbdd_dev_io_t *io)
{
    memset(m, 0, sizeof(*m));
    m->reply[0] = 0x07;
    m->reply[1] = 0x02;
    m->reply_len = 264;
    io->ctx = m;
    io->set_feature = m_set;
    io->get_feature = m_get;
    io->sleep_ms = m_sleep;
}

static void test_query_active(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t out[264];
    mock_init(&m, &io);
    CHECK(kbdd_dev_query(&io, 0x81, 0, out) == KBDD_DEV_OK);
    CHECK(m.nsent == 1 && m.sent[0][0] == 0x07 && m.sent[0][1] == 0x81);
    CHECK(m.nsleeps == 1 && m.sleeps[0] == 100);
    CHECK(out[0] == 0x07 && out[1] == 0x02);
}

static void test_query_block(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t out[264];
    mock_init(&m, &io);
    CHECK(kbdd_dev_query(&io, 0x82, 3, out) == KBDD_DEV_OK);
    CHECK(m.sent[0][1] == 0x82 && m.sent[0][2] == 3);
    CHECK(m.sleeps[0] == 50);
}

static void test_query_allowlist(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t out[264]; int op;
    mock_init(&m, &io);
    /* no opcode other than 0x81/0x82 may reach the device */
    for (op = 0; op < 256; op++) {
        if (op == 0x81 || op == 0x82) continue;
        CHECK(kbdd_dev_query(&io, (uint8_t)op, 1, out) == KBDD_DEV_EINVAL);
    }
    CHECK(kbdd_dev_query(&io, 0x82, 0, out) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_query(&io, 0x82, 9, out) == KBDD_DEV_EINVAL);
    CHECK(m.nsent == 0);
}

static void test_query_errors(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t out[264];
    mock_init(&m, &io); m.fail_set = 1;
    CHECK(kbdd_dev_query(&io, 0x81, 0, out) == KBDD_DEV_EIO);
    mock_init(&m, &io); m.fail_get = 1;
    CHECK(kbdd_dev_query(&io, 0x81, 0, out) == KBDD_DEV_EIO);
    mock_init(&m, &io); m.reply[0] = 0x05;            /* wrong report id */
    CHECK(kbdd_dev_query(&io, 0x81, 0, out) == KBDD_DEV_EPROTO);
    mock_init(&m, &io); m.reply_len = 1;              /* too short */
    CHECK(kbdd_dev_query(&io, 0x81, 0, out) == KBDD_DEV_EPROTO);
    CHECK(kbdd_dev_query(NULL, 0x81, 0, out) == KBDD_DEV_EINVAL);
}

static void test_enable_notifications(void)
{
    struct mock m;
    kbdd_dev_io_t io;

    mock_init(&m, &io);
    CHECK(kbdd_dev_enable_notifications(&io) == KBDD_DEV_OK);
    CHECK(m.nsent == 1);
    CHECK(m.sent[0][0] == 0x07 && m.sent[0][1] == 0x06);
    CHECK(m.sent[0][2] == 1);
    CHECK(m.nsleeps == 1 && m.sleeps[0] == 100);
    m.fail_set = 1;
    CHECK(kbdd_dev_enable_notifications(&io) == KBDD_DEV_EIO);
}

static void test_send_frame_single_page(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t frame[432];
    memset(frame, 0, sizeof frame);
    kbc_led_frame_fill(frame, 70, 70, 0xAA, 0xBB, 0xCC);
    mock_init(&m, &io);
    CHECK(kbdd_dev_send_frame(&io, frame, 70) == KBDD_DEV_OK);
    CHECK(m.nsent == 1);
    CHECK(m.sent[0][0] == 0x07 && m.sent[0][1] == 0x07 && m.sent[0][2] == 0);
    CHECK(m.sent[0][8] == 0xAA && m.sent[0][8 + 70] == 0xBB && m.sent[0][8 + 140] == 0xCC);
    CHECK(m.nsleeps == 0);
}

static void test_send_frame_two_pages(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t frame[432]; size_t i;
    for (i = 0; i < sizeof frame; i++) frame[i] = (uint8_t)i;
    mock_init(&m, &io);
    CHECK(kbdd_dev_send_frame(&io, frame, 144) == KBDD_DEV_OK);
    CHECK(m.nsent == 2);
    CHECK(m.sent[0][2] == 0 && m.sent[1][2] == 1);
    CHECK(m.sent[0][8] == 0 && m.sent[0][8 + 255] == 255);
    CHECK(m.sent[1][8] == (uint8_t)256 && m.sent[1][8 + 175] == (uint8_t)431);
    CHECK(m.sent[1][8 + 176] == 0);                   /* zero padded */
    CHECK(m.nsleeps == 1 && m.sleeps[0] == 15);
}

static void test_send_frame_errors(void)
{
    struct mock m; kbdd_dev_io_t io; uint8_t frame[432] = {0};
    mock_init(&m, &io);
    CHECK(kbdd_dev_send_frame(&io, frame, 0) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_send_frame(&io, frame, 145) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_send_frame(&io, NULL, 70) == KBDD_DEV_EINVAL);
    CHECK(m.nsent == 0);
    m.fail_set = 1;
    CHECK(kbdd_dev_send_frame(&io, frame, 70) == KBDD_DEV_EIO);
}

/* Stateful keyboard simulation for the effect change flow. */
struct kb {
    uint8_t block[264];
    uint8_t active;
    uint8_t last_op;
    int ignore_write;
    int fail_after_arm;
    int ignore_select;
    uint8_t flash[264];
    size_t ncommits;
    size_t nselects;
    size_t nwrites;
    uint8_t writes[4][264];
};

static ssize_t kb_set(void *ctx, const uint8_t *buf, size_t len)
{
    struct kb *k = ctx;
    if (len != 264) return -1;
    k->last_op = buf[1];
    if (buf[1] == 0x01) {
        k->nselects++;
        if (!k->ignore_select) k->active = buf[2];
    }
    if (buf[1] == 0x02) {
        if (k->nwrites < 4) memcpy(k->writes[k->nwrites], buf, len);
        k->nwrites++;
        if (k->fail_after_arm && k->nwrites > 1) {
            return -1;
        }
        if (!k->ignore_write) {
            uint8_t oldc = k->block[134];
            memcpy(k->block + 3, buf + 3, 261);   /* keyboard stores the data */
            if (buf[134] != oldc && (buf[134] == 1 || buf[134] == 255)) {
                memcpy(k->flash, k->block, 264);  /* the count change commits to flash */
                k->ncommits++;
            }
        }
    }
    return (ssize_t)len;
}

static ssize_t kb_get(void *ctx, uint8_t *buf, size_t len)
{
    struct kb *k = ctx;
    memset(buf, 0, len);
    buf[0] = 0x07;
    if (k->last_op == 0x81) { buf[1] = 0x81; buf[2] = k->active; return 264; }
    memcpy(buf, k->block, len < 264 ? len : 264);
    return 264;
}

static void kb_init(struct kb *k, kbdd_dev_io_t *io)
{
    size_t i;
    memset(k, 0, sizeof(*k));
    for (i = 8; i < 264; i++) k->block[i] = (uint8_t)(i * 5 + 1);
    k->block[0] = 0x07; k->block[1] = 0x82; k->block[2] = 3; k->block[11] = 0;
    k->active = 3;
    io->ctx = k; io->set_feature = kb_set; io->get_feature = kb_get; io->sleep_ms = m_sleep_nop;
}

static void test_set_effect_ok(void)
{
    struct kb k; kbdd_dev_io_t io; uint8_t prof = 0; size_t i; uint8_t orig[264];
    kb_init(&k, &io); memcpy(orig, k.block, 264);
    CHECK(kbdd_dev_set_effect(&io, 1, &prof) == KBDD_DEV_OK);
    CHECK(prof == 3 && k.nwrites == 1);
    CHECK(k.writes[0][1] == 0x02 && k.writes[0][2] == 3 && k.writes[0][11] == 1);
    for (i = 3; i < 264; i++) if (i != 11) CHECK(k.block[i] == orig[i]);
    CHECK(k.block[11] == 1);
}

static void test_set_effect_rejects(void)
{
    struct kb k; kbdd_dev_io_t io;
    kb_init(&k, &io);
    CHECK(kbdd_dev_set_effect(&io, 11, NULL) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_set_effect(&io, 15, NULL) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_set_effect(&io, 99, NULL) == KBDD_DEV_EINVAL);
    CHECK(k.nwrites == 0);
    kb_init(&k, &io); k.active = 9;                       /* bogus active profile */
    CHECK(kbdd_dev_set_effect(&io, 1, NULL) == KBDD_DEV_EPROTO && k.nwrites == 0);
    kb_init(&k, &io); k.block[11] = 0x77;                 /* implausible layout */
    CHECK(kbdd_dev_set_effect(&io, 1, NULL) == KBDD_DEV_EPROTO && k.nwrites == 0);
    kb_init(&k, &io); k.block[2] = 1;                     /* reply for another profile */
    CHECK(kbdd_dev_set_effect(&io, 1, NULL) == KBDD_DEV_EPROTO && k.nwrites == 0);
}

static void test_set_effect_rollback(void)
{
    struct kb k; kbdd_dev_io_t io;
    kb_init(&k, &io); k.ignore_write = 1;                 /* keyboard drops the write */
    CHECK(kbdd_dev_set_effect(&io, 1, NULL) == KBDD_DEV_EVERIFY);
    CHECK(k.nwrites == 2);                                /* write + rollback */
    CHECK(k.writes[1][11] == 0);                          /* rollback restores old effect */
}

static void test_set_field(void)
{
    struct kb k; kbdd_dev_io_t io; uint8_t orig[264]; size_t i, changed;

    kb_init(&k, &io); memcpy(orig, k.block, 264);
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 7, NULL) == KBDD_DEV_OK);
    changed = 0;
    for (i = 3; i < 264; i++) if (k.block[i] != orig[i]) { changed++; CHECK(i == 12); }
    CHECK(changed == 1 && k.block[12] == 7 && k.nwrites == 1);

    kb_init(&k, &io); k.block[11] = 1; memcpy(orig, k.block, 264);   /* breath active */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_SPEED, 40, NULL) == KBDD_DEV_OK);
    changed = 0;
    for (i = 3; i < 264; i++) if (k.block[i] != orig[i]) { changed++; CHECK(i == 19); }
    CHECK(changed == 1 && k.block[19] == 40 && k.block[20] == orig[20]);

    kb_init(&k, &io);                                    /* out-of-range values never reach the device */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 11, NULL) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_SPEED, 1, NULL) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_SPEED, 200, NULL) == KBDD_DEV_EINVAL);
    CHECK(kbdd_dev_set_field(&io, 42, 1, NULL) == KBDD_DEV_EINVAL);
    CHECK(k.nwrites == 0);

    kb_init(&k, &io); k.ignore_write = 1;                /* dropped write -> rollback */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 5, NULL) == KBDD_DEV_EVERIFY);
    CHECK(k.nwrites == 2 && k.writes[1][12] == k.block[12]);
}

static void test_switch_profile(void)
{
    struct kb k; kbdd_dev_io_t io; int changed = -1; uint8_t bad;

    kb_init(&k, &io);                                   /* active = 3 */
    CHECK(kbdd_dev_switch_profile(&io, 2, &changed) == KBDD_DEV_OK);
    CHECK(changed == 1 && k.active == 2 && k.nselects == 1);

    kb_init(&k, &io);                                   /* already active: no write */
    CHECK(kbdd_dev_switch_profile(&io, 3, &changed) == KBDD_DEV_OK);
    CHECK(changed == 0 && k.nselects == 0);

    kb_init(&k, &io);                                   /* invalid values: no I/O at all */
    for (bad = 0; bad < 255; bad++) {
        if (bad >= 1 && bad <= 4) continue;
        CHECK(kbdd_dev_switch_profile(&io, bad, NULL) == KBDD_DEV_EINVAL);
    }
    CHECK(kbdd_dev_switch_profile(&io, 0xFF, NULL) == KBDD_DEV_EINVAL);
    CHECK(k.nselects == 0);

    kb_init(&k, &io); k.ignore_select = 1;              /* keyboard ignores it: go back */
    CHECK(kbdd_dev_switch_profile(&io, 2, &changed) == KBDD_DEV_EVERIFY);
    CHECK(changed == 0 && k.active == 3 && k.nselects == 2);

    kb_init(&k, &io); k.active = 9;                     /* bogus 0x81 reply */
    CHECK(kbdd_dev_switch_profile(&io, 2, NULL) == KBDD_DEV_EPROTO && k.nselects == 0);
}

static void test_save_profile(void)
{
    struct kb k; kbdd_dev_io_t io; uint8_t prof = 0;

    kb_init(&k, &io); k.block[134] = 255; k.block[11] = 1; k.block[12] = 7; k.block[19] = 40;
    CHECK(kbdd_dev_save_profile(&io, &prof) == KBDD_DEV_OK);
    CHECK(prof == 3 && k.ncommits == 2 && k.nwrites == 2);
    CHECK(k.writes[0][134] == 1 && k.writes[1][134] == 255);       /* arm, then clean */
    CHECK(k.flash[12] == 7 && k.flash[19] == 40 && k.flash[134] == 255 && k.flash[11] == 1);
    CHECK(k.block[134] == 255);

    kb_init(&k, &io); k.block[134] = 9;                            /* not clean: refuse, no write */
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_ENOSAVE && k.nwrites == 0);
    kb_init(&k, &io); k.block[134] = 255; k.block[11] = 15;        /* custom effect: refuse */
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_ENOSAVE && k.nwrites == 0);
    kb_init(&k, &io); k.block[134] = 255; k.block[11] = 14;
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_ENOSAVE && k.nwrites == 0);

    kb_init(&k, &io); k.block[134] = 255; k.ignore_write = 1;      /* keyboard ignores writes */
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_EVERIFY);
    CHECK(k.nwrites == 1 && k.ncommits == 0);                      /* stops after the arm write */

    kb_init(&k, &io); k.block[134] = 255; k.fail_after_arm = 1;    /* clean write and restoration both fail */
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_EIO);
    CHECK(k.nwrites == 4 && k.block[134] == 1);                   /* restoration was attempted */

    kb_init(&k, &io); k.block[134] = 255; k.active = 9;            /* bogus 0x81 */
    CHECK(kbdd_dev_save_profile(&io, NULL) == KBDD_DEV_EPROTO && k.nwrites == 0);
}

static void test_set_field_diag(void)
{
    struct kb k; kbdd_dev_io_t io;

    kb_init(&k, &io); k.ignore_write = 1;                 /* firmware ignores the write */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 5, NULL) == KBDD_DEV_EVERIFY);
    CHECK(strstr(kbdd_dev_last_diag(), "@12 want 0x05") != NULL);
    CHECK(strstr(kbdd_dev_last_diag(), "restored (confirmed)") != NULL);
    CHECK(strstr(kbdd_dev_last_diag(), "profile 3") != NULL);

    kb_init(&k, &io); k.block[11] = 0x77;                 /* implausible layout */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 5, NULL) == KBDD_DEV_EPROTO);
    CHECK(strstr(kbdd_dev_last_diag(), "effect byte 119") != NULL);

    kb_init(&k, &io);                                     /* success clears the diagnostic */
    CHECK(kbdd_dev_set_field(&io, KBC_FIELD_BRIGHTNESS, 5, NULL) == KBDD_DEV_OK);
    CHECK(kbdd_dev_last_diag()[0] == '\0');
}

static void test_set_colors(void)
{
    struct kb k; kbdd_dev_io_t io; kbdd_color_edit_t ce; uint8_t orig[264], prof = 0;
    size_t i, changed;

    memset(&ce, 0, sizeof ce);
    ce.effect = 12; ce.count = 1; ce.rgb[0][0] = 9; ce.rgb[0][1] = 8; ce.rgb[0][2] = 7;
    kb_init(&k, &io); memcpy(orig, k.block, 264);
    CHECK(kbdd_dev_set_colors(&io, &ce, &prof) == KBDD_DEV_OK);
    CHECK(prof == 3 && k.nwrites == 1);
    CHECK(k.block[89] == 9 && k.block[90] == 8 && k.block[91] == 7);
    for (i = 32; i < 41; i++) CHECK(k.block[i] == 0);          /* spectrum flags cleared */
    changed = 0;
    for (i = 3; i < 264; i++) if (k.block[i] != orig[i]) { changed++; CHECK((i >= 89 && i <= 91) || (i >= 32 && i <= 40)); }
    CHECK(changed >= 3 && changed <= 12);

    kb_init(&k, &io);                                          /* effect without slots: no write */
    ce.effect = 2;
    CHECK(kbdd_dev_set_colors(&io, &ce, NULL) == KBDD_DEV_EINVAL && k.nwrites == 0);
    ce.effect = 12; ce.count = 0;
    CHECK(kbdd_dev_set_colors(&io, &ce, NULL) == KBDD_DEV_EINVAL && k.nwrites == 0);
    CHECK(kbdd_dev_set_colors(&io, NULL, NULL) == KBDD_DEV_EINVAL);

    kb_init(&k, &io); k.ignore_write = 1;                      /* ignored write: diagnostic + rollback */
    ce.count = 1;
    CHECK(kbdd_dev_set_colors(&io, &ce, NULL) == KBDD_DEV_EVERIFY);
    CHECK(strstr(kbdd_dev_last_diag(), "restored (confirmed)") != NULL);
}

int main(void)
{
    test_set_colors();
    test_set_field_diag();
    test_save_profile();
    test_switch_profile();
    test_set_field();
    test_set_effect_ok();
    test_set_effect_rejects();
    test_set_effect_rollback();
    test_query_active();
    test_query_block();
    test_query_allowlist();
    test_query_errors();
    test_enable_notifications();
    test_send_frame_single_page();
    test_send_frame_two_pages();
    test_send_frame_errors();
    if (failures) {
        fprintf(stderr, "%d device test failure(s)\n", failures);
        return 1;
    }
    printf("ok: all device tests\n");
    return 0;
}
