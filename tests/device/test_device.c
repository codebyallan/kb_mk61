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

int main(void)
{
    test_query_active();
    test_query_block();
    test_query_allowlist();
    test_query_errors();
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
