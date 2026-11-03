#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../src/daemon/kbdd.h"
#include "kbipc.h"
#include "kbcore.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/* Mock transport (reused from test_device.c)                          */
/* ------------------------------------------------------------------ */

struct mock {
    uint8_t sent[16][264];
    size_t nsent;
    uint8_t reply[264];
    size_t reply_len;
    int get_calls;
    int get_fail;          /* if set, get_feature returns -1 */
    unsigned sleeps[16];
    size_t nsleeps;
};

static struct mock *g_mock;

static ssize_t m_set(void *ctx, const uint8_t *buf, size_t len)
{
    struct mock *m = (struct mock *)ctx;
    if (m->nsent < 16 && len <= 264) {
        memcpy(m->sent[m->nsent], buf, len);
        m->nsent++;
    }
    return (ssize_t)len;
}

static ssize_t m_get(void *ctx, uint8_t *buf, size_t len)
{
    struct mock *m = (struct mock *)ctx;
    m->get_calls++;
    if (m->get_fail) return -1;
    if (m->reply_len == 0) m->reply_len = 264;
    size_t n = m->reply_len < len ? m->reply_len : len;
    memcpy(buf, m->reply, n);
    /* zero the rest */
    if (n < len) memset(buf + n, 0, len - n);
    return (ssize_t)n;
}

static void m_sleep(void *ctx, unsigned ms)
{
    struct mock *m = (struct mock *)ctx;
    if (m->nsleeps < 16) m->sleeps[m->nsleeps++] = ms;
}

static void mock_init(struct mock *m)
{
    memset(m, 0, sizeof(*m));
    m->reply[0] = 0x07;
    m->reply[1] = 0x02;   /* default: profile params reply */
    m->reply[2] = 1;      /* profile 1 */
    m->reply_len = 264;
}

/* Override the weak init function to bind mock transport. */
void kbdd_dev_io_init_hidraw(kbdd_dev_io_t *io, const kbdd_hid_device_t *dev)
{
    (void)dev;
    io->ctx = g_mock;
    io->set_feature = m_set;
    io->get_feature = m_get;
    io->sleep_ms = m_sleep;
}

/* ------------------------------------------------------------------ */
/* Test harness helpers                                                */
/* ------------------------------------------------------------------ */

static kbdd_hid_device_t g_dev;
static kbdd_ctx_t g_ctx;

static void setup_ctx(int allow_led, int allow_pw)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.role = 0;
    g_dev.fd = 1; /* >= 0 so control_dev accepts it */
    g_dev.model = &KBDD_MODELS[0];
    g_ctx.devices = &g_dev;
    g_ctx.count = 1;
    g_ctx.allow_led = allow_led;
    g_ctx.allow_profile_write = allow_pw;
    g_ctx.led_brightness = KBC_BRIGHTNESS_RAW_MAX;
    g_ctx.ipc = NULL;
}

static void setup_resp(kbdd_ipc_message_t *resp)
{
    free(resp->payload);
    memset(resp, 0, sizeof(*resp));
}

static void free_resp(kbdd_ipc_message_t *resp)
{
    free(resp->payload);
    resp->payload = NULL;
    resp->payload_len = 0;
}

static void make_msg(kbdd_ipc_message_t *msg, uint16_t opcode,
                     const uint8_t *payload, uint32_t plen)
{
    memset(msg, 0, sizeof(*msg));
    msg->opcode = opcode;
    msg->payload = (uint8_t *)payload;
    msg->payload_len = plen;
}

/* ------------------------------------------------------------------ */
/* Env gate tests                                                      */
/* ------------------------------------------------------------------ */

static void test_env_gates(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];
    uint8_t byte = 5;

    /* LED disabled */
    setup_ctx(0, 1);
    memset(payload, 0, sizeof(payload));
    make_msg(&req, KBDD_IPC_OP_LED_SET, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_led(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    /* profile write disabled */
    setup_ctx(1, 0);
    make_msg(&req, KBDD_IPC_OP_SET_PROFILE, &byte, 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_profile(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    setup_ctx(1, 0);
    make_msg(&req, KBDD_IPC_OP_SET_EFFECT, &byte, 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_effect(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    setup_ctx(1, 0);
    make_msg(&req, KBDD_IPC_OP_LED_ANIM, payload, sizeof(payload));
    /* anim requires led, not profile write — but allow_led=1 is set */
    setup_ctx(1, 0);
    setup_resp(&resp);
    /* anim payload must be valid size; use minimal valid */
    /* skip detailed anim test here */
    free_resp(&resp);
}

static void test_payload_validation(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t byte;

    /* profile: bad payload (wrong size) */
    setup_ctx(1, 1);
    byte = 2;
    make_msg(&req, KBDD_IPC_OP_SET_PROFILE, &byte, 0); /* plen 0, need 1 */
    setup_resp(&resp);
    CHECK(kbdd_handle_profile(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload") != NULL);

    /* profile: out of range */
    setup_ctx(1, 1);
    byte = 5;
    make_msg(&req, KBDD_IPC_OP_SET_PROFILE, &byte, 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_profile(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload") != NULL);

    /* effect: bad payload size */
    setup_ctx(1, 1);
    byte = 3;
    make_msg(&req, KBDD_IPC_OP_SET_EFFECT, &byte, 0);
    setup_resp(&resp);
    CHECK(kbdd_handle_effect(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload") != NULL);

    /* field: bad payload size */
    setup_ctx(1, 1);
    {
        uint8_t fp[2] = {1, 5};
        make_msg(&req, KBDD_IPC_OP_SET_FIELD, fp, 1); /* need 2 */
        setup_resp(&resp);
        CHECK(kbdd_handle_field(&g_ctx, &req, &resp) == 0);
        CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload") != NULL);
    }

    /* field: bad field number */
    setup_ctx(1, 1);
    {
        uint8_t fp[2] = {6, 5}; /* field 6 invalid */
        make_msg(&req, KBDD_IPC_OP_SET_FIELD, fp, 2);
        setup_resp(&resp);
        CHECK(kbdd_handle_field(&g_ctx, &req, &resp) == 0);
        CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload") != NULL);
    }

    /* LED: bad payload size */
    setup_ctx(1, 1);
    {
        uint8_t lp[KBDD_LED_PAYLOAD_SIZE];
        memset(lp, 0, sizeof(lp));
        make_msg(&req, KBDD_IPC_OP_LED_SET, lp, sizeof(lp) - 1);
        setup_resp(&resp);
        CHECK(kbdd_handle_led(&g_ctx, &req, &resp) == 0);
        CHECK(resp.payload != NULL && strstr((char *)resp.payload, "bad LED payload") != NULL);
    }
    free_resp(&resp);
}

static void test_no_device(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t byte = 1;
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];

    g_ctx.count = 0; /* no devices */
    g_ctx.allow_led = 1;
    g_ctx.allow_profile_write = 1;

    /* LED */
    memset(payload, 0, sizeof(payload));
    make_msg(&req, KBDD_IPC_OP_LED_SET, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_led(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "no keyboard") != NULL);

    /* effect */
    make_msg(&req, KBDD_IPC_OP_SET_EFFECT, &byte, 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_effect(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "no keyboard") != NULL);

    /* profile */
    make_msg(&req, KBDD_IPC_OP_SET_PROFILE, &byte, 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_profile(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "no keyboard") != NULL);

    /* save */
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "no keyboard") != NULL);

    /* field */
    {
        uint8_t fp[2] = {1, 5};
        make_msg(&req, KBDD_IPC_OP_SET_FIELD, fp, 2);
        setup_resp(&resp);
        CHECK(kbdd_handle_field(&g_ctx, &req, &resp) == 0);
        CHECK(resp.payload != NULL && strstr((char *)resp.payload, "no keyboard") != NULL);
    }

    /* restore count */
    g_ctx.count = 1;
    g_ctx.devices = &g_dev;
    g_dev.role = 0;
    g_dev.fd = 1;
    g_dev.model = &KBDD_MODELS[0];
    free_resp(&resp);
}

static struct mock g_m;

static void test_led_release(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];

    setup_ctx(1, 1);
    g_mock = &g_m;
    mock_init(g_mock);

    memset(payload, 0, sizeof(payload));
    payload[0] = 0; /* flags */
    payload[2] = KBDD_LED_MODE_RELEASE;
    make_msg(&req, KBDD_IPC_OP_LED_SET, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_led(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "streaming stopped") != NULL);
    CHECK(g_ctx.led_hold == 0);
    CHECK(g_ctx.led_active == 0);
    free_resp(&resp);
}

static void test_led_solid_success(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];

    setup_ctx(1, 1);
    g_mock = &g_m;
    mock_init(g_mock);

    memset(payload, 0, sizeof(payload));
    payload[0] = KBDD_LED_FLAG_HOLD; /* flags: hold */
    payload[1] = 70;                 /* stride */
    payload[2] = KBDD_LED_MODE_SOLID; /* mode */
    payload[3] = 0;                  /* pos */
    payload[4] = 0xFF; payload[5] = 0x00; payload[6] = 0x00; /* red */
    make_msg(&req, KBDD_IPC_OP_LED_SET, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_led(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "ok: LED frame sent") != NULL);
    CHECK(g_ctx.led_active == 1);
    CHECK(g_ctx.led_hold == 1);
    CHECK(g_mock->nsent >= 1);
    free_resp(&resp);
}

static void test_anim_validation(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t payload[KBDD_ANIM_PAYLOAD_SIZE];

    /* env gate: LED disabled */
    setup_ctx(0, 1);
    memset(payload, 0, sizeof(payload));
    payload[0] = 1; /* breath */
    make_msg(&req, KBDD_IPC_OP_LED_ANIM, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_anim(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    /* bad payload size */
    setup_ctx(1, 1);
    make_msg(&req, KBDD_IPC_OP_LED_ANIM, payload, sizeof(payload) - 1);
    setup_resp(&resp);
    CHECK(kbdd_handle_anim(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "bad animation payload") != NULL);

    /* bad kind (0) */
    setup_ctx(1, 1);
    memset(payload, 0, sizeof(payload));
    payload[0] = 0; /* invalid kind */
    make_msg(&req, KBDD_IPC_OP_LED_ANIM, payload, sizeof(payload));
    setup_resp(&resp);
    CHECK(kbdd_handle_anim(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "bad animation payload") != NULL);
    free_resp(&resp);
}

static void test_colors_validation(void)
{
    kbdd_ipc_message_t req, resp;
    memset(&resp, 0, sizeof(resp));
    uint8_t payload[8];

    /* env gate: profile write disabled */
    setup_ctx(1, 0);
    memset(payload, 0, sizeof(payload));
    payload[0] = 12; /* lighting */
    payload[1] = 1;  /* count */
    payload[2] = 0xFF; payload[3] = 0; payload[4] = 0;
    make_msg(&req, KBDD_IPC_OP_SET_COLORS, payload, 5);
    setup_resp(&resp);
    CHECK(kbdd_handle_colors(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    /* bad count (0) */
    setup_ctx(1, 1);
    memset(payload, 0, sizeof(payload));
    payload[0] = 12;
    payload[1] = 0; /* count 0 invalid */
    make_msg(&req, KBDD_IPC_OP_SET_COLORS, payload, 2);
    setup_resp(&resp);
    CHECK(kbdd_handle_colors(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "payload must be") != NULL);
    free_resp(&resp);
}

int main(void)
{
    test_env_gates();
    test_payload_validation();
    test_no_device();
    test_led_release();
    test_led_solid_success();
    test_anim_validation();
    test_colors_validation();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all handler tests");
    return 0;
}