#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

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
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
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

static void test_async_frame_queue(void)
{
    struct mock m;
    uint8_t frame[KBC_LED_BUFFER_SIZE];
    int waited = 0;

    mock_init(&m);
    g_mock = &m;
    setup_ctx(1, 0);
    CHECK(kbc_led_frame_fill(frame, 70, 70, 1, 2, 3) == KBC_OK);
    kbdd_op_queue_frame_from_base(NULL, &g_dev, frame, 70, 70, KBDD_LED_FLAG_HOLD);
    CHECK(kbdd_op_pending());
    while (kbdd_op_pending() && waited < 200) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
        kbdd_op_poll(&g_ctx);
        waited++;
    }
    CHECK(!kbdd_op_pending());
    CHECK(g_ctx.led_active == 1);
    CHECK(g_ctx.led_hold == 1);
    CHECK(g_ctx.led_stride == 70);
    kbdd_op_shutdown();
}

static void test_async_frame_cancel(void)
{
    struct mock m;
    uint8_t frame[KBC_LED_BUFFER_SIZE];
    int waited = 0;

    mock_init(&m);
    g_mock = &m;
    setup_ctx(1, 0);
    /* Pretend a stream is already running: a stale completion must not turn
     * streaming back on after the release path clears these flags. */
    g_ctx.led_active = 1;
    g_ctx.led_hold = 1;
    g_ctx.led_stride = 70;
    CHECK(kbc_led_frame_fill(frame, 70, 70, 4, 5, 6) == KBC_OK);
    CHECK(kbdd_op_queue_frame_from_base(NULL, &g_dev, frame, 70, 70,
                                        KBDD_LED_FLAG_HOLD) == 0);
    /* A release arriving while the frame is in flight must win. The child has
     * been forked but its completion has not been processed yet. */
    kbdd_op_cancel_led();
    g_ctx.led_active = 0;
    g_ctx.led_hold = 0;
    while (kbdd_op_pending() && waited < 200) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
        kbdd_op_poll(&g_ctx);
        waited++;
    }
    CHECK(!kbdd_op_pending());
    CHECK(g_ctx.led_active == 0);
    CHECK(g_ctx.led_hold == 0);
    kbdd_op_shutdown();
}

static void test_async_queue_full(void)
{
    struct mock m;
    kbdd_ipc_message_t req;
    uint8_t field[2] = {KBC_FIELD_BRIGHTNESS, 128};
    uint8_t frame[KBC_LED_BUFFER_SIZE];
    int queued = 0;

    mock_init(&m);
    g_mock = &m;
    setup_ctx(1, 1);
    CHECK(kbc_led_frame_fill(frame, 70, 70, 1, 2, 3) == KBC_OK);
    make_msg(&req, KBDD_IPC_OP_SET_FIELD, field, sizeof(field));

    while (kbdd_op_has_room() && queued < 64) {
        CHECK(kbdd_op_queue_field(&req, &g_dev, 70, 0, frame) == 0);
        queued++;
    }
    CHECK(queued > 0 && queued < 64); /* stopped by the queue cap, not the loop */
    CHECK(!kbdd_op_has_room());
    CHECK(kbdd_op_queue_field(&req, &g_dev, 70, 0, frame) == -1);
    kbdd_op_shutdown();
    CHECK(kbdd_op_has_room());
    CHECK(!kbdd_op_pending());
}

static void test_async_shutdown_discards_queue(void)
{
    struct mock m;
    kbdd_ipc_message_t req;
    uint8_t field[2] = {KBC_FIELD_BRIGHTNESS, 128};
    uint8_t frame[KBC_LED_BUFFER_SIZE];

    mock_init(&m);
    g_mock = &m;
    setup_ctx(1, 1);
    CHECK(kbc_led_frame_fill(frame, 70, 70, 3, 3, 3) == KBC_OK);
    make_msg(&req, KBDD_IPC_OP_SET_FIELD, field, sizeof(field));

    for (int i = 0; i < 4; i++) {
        CHECK(kbdd_op_queue_field(&req, &g_dev, 70, 0, frame) == 0);
    }
    CHECK(kbdd_op_pending());
    kbdd_op_shutdown();
    CHECK(!kbdd_op_pending());
    CHECK(kbdd_op_has_room());
}

/* ------------------------------------------------------------------ */
/* End-to-end: async handler -> worker child -> deferred IPC reply     */
/* ------------------------------------------------------------------ */

typedef struct ipc_fixture {
    char dir[108];
    char path[108];
    kbdd_ipc_server_t *server;
    int client;
} ipc_fixture_t;

static void ipc_fixture_open(ipc_fixture_t *f)
{
    char tmpl[] = "/tmp/kbdd-handler-e2e-XXXXXX";
    char *dir = mkdtemp(tmpl);
    struct sockaddr_un addr;

    memset(f, 0, sizeof(*f));
    f->client = -1;
    CHECK(dir != NULL);
    if (dir == NULL) {
        return;
    }
    size_t dlen = strlen(dir);
    CHECK(dlen < sizeof(f->dir));
    memcpy(f->dir, dir, dlen + 1);
    CHECK(dlen + sizeof("/kbdd.sock") <= sizeof(f->path));
    memcpy(f->path, f->dir, dlen);
    memcpy(f->path + dlen, "/kbdd.sock", sizeof("/kbdd.sock"));
    CHECK(setenv("XDG_RUNTIME_DIR", f->dir, 1) == 0);

    f->server = kbdd_ipc_server_create();
    CHECK(f->server != NULL);
    if (f->server == NULL) {
        return;
    }
    f->client = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(f->client >= 0);
    if (f->client < 0) {
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, f->path, strlen(f->path) + 1);
    CHECK(connect(f->client, (struct sockaddr *)&addr, sizeof(addr)) == 0);
}

static void ipc_fixture_close(ipc_fixture_t *f)
{
    if (f->client >= 0) {
        close(f->client);
    }
    if (f->server != NULL) {
        kbdd_ipc_server_destroy(f->server);
    }
    if (f->dir[0] != '\0') {
        CHECK(rmdir(f->dir) == 0);
    }
    unsetenv("XDG_RUNTIME_DIR");
}

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static ssize_t read_exact_fd(int fd, void *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, (uint8_t *)buf + got, len - got, 0);
        if (r == 0) {
            return (ssize_t)got;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static void build_wire_request(uint8_t *buf, size_t *out_len, uint16_t opcode,
                               const uint8_t *payload, uint32_t payload_len,
                               uint16_t request_id)
{
    uint32_t magic = KBDD_IPC_MAGIC;

    memset(buf, 0, KBDD_IPC_HEADER_SIZE);
    buf[0] = (uint8_t)magic;
    buf[1] = (uint8_t)(magic >> 8);
    buf[2] = (uint8_t)(magic >> 16);
    buf[3] = (uint8_t)(magic >> 24);
    buf[4] = (uint8_t)KBDD_IPC_VERSION;
    buf[5] = (uint8_t)(KBDD_IPC_VERSION >> 8);
    buf[8] = (uint8_t)payload_len;
    buf[9] = (uint8_t)(payload_len >> 8);
    buf[10] = (uint8_t)(payload_len >> 16);
    buf[11] = (uint8_t)(payload_len >> 24);
    buf[12] = (uint8_t)opcode;
    buf[13] = (uint8_t)(opcode >> 8);
    buf[14] = (uint8_t)request_id;
    buf[15] = (uint8_t)(request_id >> 8);
    if (payload_len > 0 && payload != NULL) {
        memcpy(buf + KBDD_IPC_HEADER_SIZE, payload, payload_len);
    }
    *out_len = KBDD_IPC_HEADER_SIZE + payload_len;
}

static void test_async_led_deferred_reply(void)
{
    struct mock m;
    ipc_fixture_t f;
    uint8_t payload[KBDD_LED_PAYLOAD_SIZE];
    uint8_t frame[KBDD_IPC_HEADER_SIZE + KBDD_LED_PAYLOAD_SIZE];
    uint8_t header[KBDD_IPC_HEADER_SIZE];
    size_t frame_len = sizeof(frame);
    int waited = 0;

    mock_init(&m);
    g_mock = &m;
    setup_ctx(1, 0);
    ipc_fixture_open(&f);
    if (f.server == NULL || f.client < 0) {
        ipc_fixture_close(&f);
        return;
    }
    g_ctx.ipc = f.server;
    kbdd_ipc_sync_mode = 0;

    memset(payload, 0, sizeof(payload));
    payload[0] = KBDD_LED_FLAG_HOLD;
    payload[1] = 70;
    payload[2] = KBDD_LED_MODE_SOLID;
    payload[4] = 0xFF;
    build_wire_request(frame, &frame_len, KBDD_IPC_OP_LED_SET, payload,
                       sizeof(payload), 0x4242);
    CHECK(send(f.client, frame, frame_len, 0) == (ssize_t)frame_len);

    /* The handler must defer instead of blocking on the (mock) keyboard. The
     * deferred reply is sent later by op_complete via kbdd_ipc_server_send. */
    CHECK(kbdd_ipc_server_poll(f.server, 50, kbdd_ipc_handle, &g_ctx) == 0);
    CHECK(kbdd_op_pending());

    while (kbdd_op_pending() && waited < 500) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
        kbdd_op_poll(&g_ctx);
        waited++;
    }
    CHECK(!kbdd_op_pending());
    CHECK(g_ctx.led_active == 1);
    CHECK(g_ctx.led_hold == 1);

    CHECK(read_exact_fd(f.client, header, sizeof(header)) ==
          (ssize_t)sizeof(header));
    CHECK(rd32le(header) == KBDD_IPC_MAGIC);
    CHECK(rd16le(header + 12) == KBDD_IPC_OP_LED_SET);
    CHECK(rd16le(header + 14) == 0x4242);
    uint32_t plen = rd32le(header + 8);
    CHECK(plen > 0 && plen <= 256);
    if (plen > 0 && plen <= 256) {
        char body[256];
        CHECK(read_exact_fd(f.client, body, plen) == (ssize_t)plen);
        body[plen < sizeof(body) ? plen : sizeof(body) - 1] = '\0';
        CHECK(strstr(body, "ok: LED frame sent") != NULL);
    }

    kbdd_op_shutdown();
    g_ctx.ipc = NULL;
    ipc_fixture_close(&f);
    kbdd_ipc_sync_mode = 1;
}

/* LED-only deployments (ENABLE_LED=1, no PROFILE_WRITE) must not reach flash. */
static void test_led_only_has_no_flash_access(void)
{
    kbdd_ipc_message_t req, resp;
    uint8_t blk[265];
    uint8_t fp[2] = {1, 5};

    memset(&resp, 0, sizeof(resp));
    memset(blk, 0, sizeof(blk));
    blk[0] = 1;
    setup_ctx(1, 0);
    g_mock = &g_m;
    mock_init(g_mock);

    make_msg(&req, KBDD_IPC_OP_SAVE_BLOCK, NULL, 0);
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    make_msg(&req, KBDD_IPC_OP_SET_FIELD, fp, 2);
    setup_resp(&resp);
    CHECK(kbdd_handle_field(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    make_msg(&req, KBDD_IPC_OP_RESTORE_PROFILE, blk, sizeof(blk));
    setup_resp(&resp);
    CHECK(kbdd_ipc_handle(&req, &resp, &g_ctx) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "disabled") != NULL);

    CHECK(g_mock->nsent == 0);       /* nothing reached the device */
    CHECK(g_ctx.save_next_ms == 0);  /* a refused save must not arm the cooldown */
    free_resp(&resp);
}

static void test_save_cooldown(void)
{
    kbdd_ipc_message_t req, resp;
    size_t sent_after_first;

    memset(&resp, 0, sizeof(resp));
    setup_ctx(0, 1);
    g_mock = &g_m;
    mock_init(g_mock);
    make_msg(&req, KBDD_IPC_OP_SAVE_BLOCK, NULL, 0);

    /* first attempt reaches the device (whatever its outcome) and arms the cooldown */
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "rate-limited") == NULL);
    CHECK(g_mock->get_calls > 0);
    CHECK(g_ctx.save_next_ms != 0);
    sent_after_first = g_mock->nsent;

    /* an immediate retry is refused without touching the device */
    {
        int gets = g_mock->get_calls;
        setup_resp(&resp);
        CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
        CHECK(resp.payload != NULL && strstr((char *)resp.payload, "rate-limited") != NULL);
        CHECK(g_mock->nsent == sent_after_first);
        CHECK(g_mock->get_calls == gets);
    }

    /* once the window has passed, saving is allowed again */
    g_ctx.save_next_ms = 1;          /* monotonic clock is long past 1 ms */
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "rate-limited") == NULL);
    CHECK(g_ctx.save_next_ms > 1);
    free_resp(&resp);
}

/* Async (production) path: the second save inside the window never reaches the queue. */
static void test_save_cooldown_async(void)
{
    kbdd_ipc_message_t req, resp;
    int waited = 0;

    memset(&resp, 0, sizeof(resp));
    setup_ctx(0, 1);
    g_mock = &g_m;
    mock_init(g_mock);
    make_msg(&req, KBDD_IPC_OP_SAVE_BLOCK, NULL, 0);

    kbdd_ipc_sync_mode = 0;
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == KBDD_HANDLER_DEFER);
    CHECK(g_ctx.save_next_ms != 0);
    while (kbdd_op_pending() && waited < 400) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
        kbdd_op_poll(&g_ctx);
        waited++;
    }
    setup_resp(&resp);
    CHECK(kbdd_handle_save(&g_ctx, &req, &resp) == 0);
    CHECK(resp.payload != NULL && strstr((char *)resp.payload, "rate-limited") != NULL);
    CHECK(!kbdd_op_pending());
    kbdd_op_shutdown();
    kbdd_ipc_sync_mode = 1;
    free_resp(&resp);
}

int main(void)
{
    kbdd_ipc_sync_mode = 1;
    test_env_gates();
    test_led_only_has_no_flash_access();
    test_save_cooldown();
    test_save_cooldown_async();
    test_payload_validation();
    test_no_device();
    test_led_release();
    test_led_solid_success();
    test_anim_validation();
    test_colors_validation();
    test_async_frame_queue();
    test_async_frame_cancel();
    test_async_queue_full();
    test_async_shutdown_discards_queue();
    test_async_led_deferred_reply();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all handler tests");
    return 0;
}