/* Tests for the kbdd IPC wire framing using a real Unix socket server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "../src/daemon/kbdd.h"
#include "kbipc.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int test_handler(const kbdd_ipc_message_t *req, kbdd_ipc_message_t *resp,
                        void *ctx)
{
    (void)ctx;
    uint8_t *out = malloc(req->payload_len > 0 ? req->payload_len : 1);
    if (out == NULL) {
        return -1;
    }
    if (req->payload_len > 0) {
        memcpy(out, req->payload, req->payload_len);
    }
    resp->payload = out;
    resp->payload_len = req->payload_len;
    return 0;
}

typedef struct test_srv {
    char dir[108];
    char path[108];
    kbdd_ipc_server_t *server;
    int server_fd;
    int client;
} test_srv_t;

static void test_setup(test_srv_t *ts)
{
    char tmpl[] = "/tmp/kbdd-ipc-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    struct sockaddr_un addr;

    memset(ts, 0, sizeof(*ts));
    ts->server_fd = -1;
    ts->client = -1;
    CHECK(dir != NULL);

    snprintf(ts->dir, sizeof(ts->dir), "%s", dir);
    size_t dlen = strlen(ts->dir);
    CHECK(dlen + sizeof("/kbdd.sock") <= sizeof(ts->path));
    memcpy(ts->path, ts->dir, dlen);
    memcpy(ts->path + dlen, "/kbdd.sock", sizeof("/kbdd.sock"));
    CHECK(setenv("XDG_RUNTIME_DIR", ts->dir, 1) == 0);
    ts->server = kbdd_ipc_server_create();
    CHECK(ts->server != NULL);
    if (ts->server == NULL) {
        return;
    }
    ts->server_fd = kbdd_ipc_server_fd(ts->server);
    CHECK(ts->server_fd >= 0);

    ts->client = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(ts->client >= 0);
    if (ts->client < 0) {
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ts->path);
    CHECK(connect(ts->client, (struct sockaddr *)&addr, sizeof(addr)) == 0);
}

static void test_teardown(test_srv_t *ts)
{
    if (ts->client >= 0) {
        close(ts->client);
    }
    if (ts->server != NULL) {
        kbdd_ipc_server_destroy(ts->server); /* also unlinks socket */
    }
    CHECK(rmdir(ts->dir) == 0);
    unsetenv("XDG_RUNTIME_DIR");
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_request(uint8_t *buf, uint32_t magic, uint16_t version,
                        uint16_t opcode, uint32_t payload_len, uint16_t request_id,
                        uint32_t flags)
{
    memset(buf, 0, KBDD_IPC_HEADER_SIZE);
    buf[0] = (uint8_t)magic;
    buf[1] = (uint8_t)(magic >> 8);
    buf[2] = (uint8_t)(magic >> 16);
    buf[3] = (uint8_t)(magic >> 24);
    buf[4] = (uint8_t)version;
    buf[5] = (uint8_t)(version >> 8);
    buf[6] = (uint8_t)flags;
    buf[7] = (uint8_t)(flags >> 8);
    buf[8] = (uint8_t)payload_len;
    buf[9] = (uint8_t)(payload_len >> 8);
    buf[10] = (uint8_t)(payload_len >> 16);
    buf[11] = (uint8_t)(payload_len >> 24);
    buf[12] = (uint8_t)opcode;
    buf[13] = (uint8_t)(opcode >> 8);
    buf[14] = (uint8_t)request_id;
    buf[15] = (uint8_t)(request_id >> 8);
}

static void build_message(uint8_t *buf, size_t *out_len, uint16_t opcode,
                          const uint8_t *payload, uint32_t payload_len,
                          uint16_t request_id, uint32_t flags)
{
    put_request(buf, KBDD_IPC_MAGIC, KBDD_IPC_VERSION, opcode,
                payload_len, request_id, flags);
    if (payload_len > 0 && payload != NULL) {
        memcpy(buf + KBDD_IPC_HEADER_SIZE, payload, payload_len);
    }
    *out_len = KBDD_IPC_HEADER_SIZE + payload_len;
}

static int poll_server(test_srv_t *ts)
{
    return kbdd_ipc_server_poll(ts->server, 50, test_handler, NULL);
}

static int client_wait_readable(int fd, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return rc;   /* 0 = timeout, <0 = error */
    return (pfd.revents & (POLLIN | POLLHUP | POLLERR)) ? 1 : 0;
}

static ssize_t read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, buf + got, len - got, 0);
        if (r == 0) {
            return (ssize_t)got; /* EOF */
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

typedef struct test_reply {
    uint32_t magic;
    uint16_t version;
    uint32_t flags;
    uint16_t opcode;
    uint16_t request_id;
    uint32_t payload_len;
    uint8_t *payload;
} test_reply_t;

static int read_reply(int fd, test_reply_t *reply)
{
    memset(reply, 0, sizeof(*reply));

    uint8_t header[KBDD_IPC_HEADER_SIZE];
    ssize_t n = read_exact(fd, header, sizeof(header));
    if (n != (ssize_t)sizeof(header)) {
        return -1;
    }
    reply->magic = le32(header);
    reply->version = le16(header + 4);
    reply->flags = le16(header + 6);
    reply->payload_len = le32(header + 8);
    reply->opcode = le16(header + 12);
    reply->request_id = le16(header + 14);
    if (reply->payload_len > KBDD_IPC_MAX_PAYLOAD) {
        return -1;
    }
    if (reply->payload_len > 0) {
        reply->payload = malloc(reply->payload_len);
        if (reply->payload == NULL) {
            return -1;
        }
        n = read_exact(fd, reply->payload, reply->payload_len);
        if (n != (ssize_t)reply->payload_len) {
            free(reply->payload);
            reply->payload = NULL;
            return -1;
        }
    }
    return 0;
}

static void free_reply(test_reply_t *reply)
{
    free(reply->payload);
    reply->payload = NULL;
}

static void test_valid_ping(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[64];
    size_t len;
    build_message(frame, &len, KBDD_IPC_OP_PING, (const uint8_t *)"hello", 5, 0x1234, 0x00AB);
    CHECK(write(ts.client, frame, len) == (ssize_t)len);
    CHECK(poll_server(&ts) == 0);

    test_reply_t reply;
    CHECK(read_reply(ts.client, &reply) == 0);
    CHECK(reply.magic == KBDD_IPC_MAGIC);
    CHECK(reply.version == KBDD_IPC_VERSION);
    CHECK(reply.flags == 0); /* flags are ignored by design */
    CHECK(reply.opcode == KBDD_IPC_OP_PING);
    CHECK(reply.request_id == 0x1234);
    CHECK(reply.payload_len == 5);
    CHECK(reply.payload != NULL && memcmp(reply.payload, "hello", 5) == 0);
    free_reply(&reply);

    test_teardown(&ts);
}

static void test_multiple_messages_in_one_send(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[2 * (KBDD_IPC_HEADER_SIZE + 4)];
    size_t len1, len2;
    build_message(frame, &len1, KBDD_IPC_OP_PING, (const uint8_t *)"one", 3, 0x0001, 0);
    build_message(frame + len1, &len2, KBDD_IPC_OP_PING, (const uint8_t *)"two", 3, 0x0002, 0);

    CHECK(write(ts.client, frame, len1 + len2) == (ssize_t)(len1 + len2));
    CHECK(poll_server(&ts) == 0);

    test_reply_t r1, r2;
    CHECK(read_reply(ts.client, &r1) == 0);
    CHECK(r1.request_id == 0x0001);
    CHECK(r1.payload_len == 3 && memcmp(r1.payload, "one", 3) == 0);
    free_reply(&r1);

    CHECK(read_reply(ts.client, &r2) == 0);
    CHECK(r2.request_id == 0x0002);
    CHECK(r2.payload_len == 3 && memcmp(r2.payload, "two", 3) == 0);
    free_reply(&r2);

    test_teardown(&ts);
}

static void test_partial_message_waits_for_payload(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[KBDD_IPC_HEADER_SIZE + 4];
    size_t len;
    build_message(frame, &len, KBDD_IPC_OP_PING, (const uint8_t *)"ping", 4, 0x0042, 0);

    /* Send only half the header first. */
    CHECK(write(ts.client, frame, KBDD_IPC_HEADER_SIZE / 2) == (ssize_t)(KBDD_IPC_HEADER_SIZE / 2));
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 25) == 0); /* no reply yet */

    /* Send the rest of the header but no payload yet. */
    CHECK(write(ts.client, frame + KBDD_IPC_HEADER_SIZE / 2,
                KBDD_IPC_HEADER_SIZE - KBDD_IPC_HEADER_SIZE / 2) ==
          (ssize_t)(KBDD_IPC_HEADER_SIZE - KBDD_IPC_HEADER_SIZE / 2));
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 25) == 0); /* still no reply */

    /* Send the payload; the reply should appear now. */
    CHECK(write(ts.client, frame + KBDD_IPC_HEADER_SIZE, 4) == 4);
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 250) == 1);

    test_reply_t reply;
    CHECK(read_reply(ts.client, &reply) == 0);
    CHECK(reply.request_id == 0x0042);
    CHECK(reply.payload_len == 4 && memcmp(reply.payload, "ping", 4) == 0);
    free_reply(&reply);

    test_teardown(&ts);
}

static void check_invalid_magic(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[KBDD_IPC_HEADER_SIZE];
    put_request(frame, 0xDEADBEEF, KBDD_IPC_VERSION, KBDD_IPC_OP_PING, 0, 0x1111, 0);
    CHECK(write(ts.client, frame, sizeof(frame)) == (ssize_t)sizeof(frame));
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 250) == 1); /* EOF */

    uint8_t buf[4];
    CHECK(read_exact(ts.client, buf, sizeof(buf)) == 0); /* closed by server */
    test_teardown(&ts);
}

static void check_invalid_version(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[KBDD_IPC_HEADER_SIZE];
    put_request(frame, KBDD_IPC_MAGIC, 42, KBDD_IPC_OP_PING, 0, 0x2222, 0);
    CHECK(write(ts.client, frame, sizeof(frame)) == (ssize_t)sizeof(frame));
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 250) == 1); /* EOF */

    uint8_t buf[4];
    CHECK(read_exact(ts.client, buf, sizeof(buf)) == 0);
    test_teardown(&ts);
}

static void check_oversized_payload_len(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[KBDD_IPC_HEADER_SIZE];
    put_request(frame, KBDD_IPC_MAGIC, KBDD_IPC_VERSION, KBDD_IPC_OP_PING,
                KBDD_IPC_MAX_PAYLOAD + 1, 0x3333, 0);
    CHECK(write(ts.client, frame, sizeof(frame)) == (ssize_t)sizeof(frame));
    CHECK(poll_server(&ts) == 0);
    CHECK(client_wait_readable(ts.client, 250) == 1); /* EOF */

    uint8_t buf[4];
    CHECK(read_exact(ts.client, buf, sizeof(buf)) == 0);
    test_teardown(&ts);
}

static void test_watch_receives_event(void)
{
    test_srv_t ts;
    test_setup(&ts);

    uint8_t frame[KBDD_IPC_HEADER_SIZE];
    size_t len;
    build_message(frame, &len, KBDD_IPC_OP_WATCH, NULL, 0, 0x0007, 0);
    CHECK(write(ts.client, frame, len) == (ssize_t)len);
    CHECK(poll_server(&ts) == 0);

    test_reply_t watch_reply;
    CHECK(read_reply(ts.client, &watch_reply) == 0);
    CHECK(watch_reply.opcode == KBDD_IPC_OP_WATCH);
    free_reply(&watch_reply);

    /* Server should now stream events to this client. */
    CHECK(kbdd_ipc_server_publish(ts.server, "profile changed: 2") == 0);

    test_reply_t event;
    CHECK(read_reply(ts.client, &event) == 0);
    CHECK(event.opcode == KBDD_IPC_OP_EVENT);
    CHECK(event.payload != NULL && strcmp((char *)event.payload, "profile changed: 2") == 0);
    free_reply(&event);

    test_teardown(&ts);
}

static void test_non_watch_does_not_receive_event(void)
{
    test_srv_t ts;
    test_setup(&ts);

    CHECK(kbdd_ipc_server_publish(ts.server, "unexpected event") == 0);
    CHECK(client_wait_readable(ts.client, 100) == 0);

    test_teardown(&ts);
}

typedef struct defer_ctx {
    int calls;
    int fd;
    uint16_t opcode;
    uint16_t request_id;
} defer_ctx_t;

static int defer_first_handler(const kbdd_ipc_message_t *req,
                               kbdd_ipc_message_t *resp, void *ctx)
{
    defer_ctx_t *d = (defer_ctx_t *)ctx;
    if (d != NULL) {
        d->calls++;
        if (d->calls == 1) {
            d->fd = req->client_fd;
            d->opcode = req->opcode;
            d->request_id = req->request_id;
            return KBDD_HANDLER_DEFER;
        }
    }
    return test_handler(req, resp, NULL);
}

static void test_deferred_reply(void)
{
    test_srv_t ts;
    defer_ctx_t d;
    test_setup(&ts);

    memset(&d, 0, sizeof(d));
    d.fd = -1;

    uint8_t frame[2 * (KBDD_IPC_HEADER_SIZE + 4)];
    size_t len1, len2;
    build_message(frame, &len1, KBDD_IPC_OP_PING, (const uint8_t *)"slow", 4, 0x0001, 0);
    build_message(frame + len1, &len2, KBDD_IPC_OP_PING, (const uint8_t *)"fast", 4, 0x0002, 0);
    CHECK(write(ts.client, frame, len1 + len2) == (ssize_t)(len1 + len2));
    CHECK(kbdd_ipc_server_poll(ts.server, 50, defer_first_handler, &d) == 0);
    CHECK(d.calls == 2);
    CHECK(d.fd >= 0);
    CHECK(d.opcode == KBDD_IPC_OP_PING);
    CHECK(d.request_id == 0x0001);

    /* The second (non-deferred) request is answered immediately. */
    test_reply_t fast;
    CHECK(read_reply(ts.client, &fast) == 0);
    CHECK(fast.request_id == 0x0002);
    CHECK(fast.payload_len == 4 && memcmp(fast.payload, "fast", 4) == 0);
    free_reply(&fast);

    /* The deferred request is answered from outside the poll loop. */
    CHECK(kbdd_ipc_server_send(ts.server, d.fd, d.opcode, d.request_id,
                               (const uint8_t *)"slow-reply", 11) == 0);
    test_reply_t slow;
    CHECK(read_reply(ts.client, &slow) == 0);
    CHECK(slow.request_id == 0x0001);
    CHECK(slow.opcode == KBDD_IPC_OP_PING);
    CHECK(slow.payload_len == 11 && memcmp(slow.payload, "slow-reply", 11) == 0);
    free_reply(&slow);

    test_teardown(&ts);
}

/* ---- TX queue / back-pressure ------------------------------------------ */

static void set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    CHECK(fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0);
}

/* Parse complete frames at buf[*off..len). Returns 1 and fills the out
 * fields when a whole frame is present, else 0. */
static int next_frame(const uint8_t *buf, size_t len, size_t *off,
                      uint16_t *opcode, uint16_t *rid,
                      const uint8_t **payload, uint32_t *plen)
{
    uint32_t n;

    if (len - *off < KBDD_IPC_HEADER_SIZE) return 0;
    CHECK(le32(buf + *off) == KBDD_IPC_MAGIC);
    n = le32(buf + *off + 8);
    if (len - *off < KBDD_IPC_HEADER_SIZE + n) return 0;
    *opcode = le16(buf + *off + 12);
    *rid = le16(buf + *off + 14);
    *payload = buf + *off + KBDD_IPC_HEADER_SIZE;
    *plen = n;
    *off += KBDD_IPC_HEADER_SIZE + n;
    return 1;
}

/* Run the server and read everything the client can get, until `idle`
 * consecutive rounds produce no bytes. Returns the new buffer length. */
static size_t pump(test_srv_t *ts, uint8_t *buf, size_t cap, size_t len, int idle)
{
    int quiet = 0;

    while (quiet < idle && len < cap) {
        ssize_t r;
        (void)poll_server(ts);
        r = recv(ts->client, buf + len, cap - len, MSG_DONTWAIT);
        if (r > 0) {
            len += (size_t)r;
            quiet = 0;
        } else {
            quiet++;
        }
    }
    return len;
}

/* More pipelined replies than fit in the socket buffer must all arrive, in
 * order, without the connection being dropped (EAGAIN is not fatal). */
static void test_pipelined_replies_not_lost(void)
{
    enum { N = 200, PL = 40 };
    test_srv_t ts;
    uint8_t req[N * (KBDD_IPC_HEADER_SIZE + PL)];
    size_t off = 0, rlen = 0, roff = 0, len;
    uint8_t *rbuf;
    int got = 0;

    test_setup(&ts);
    set_nonblock(ts.client);
    rbuf = malloc(N * (KBDD_IPC_HEADER_SIZE + PL) + 64);
    CHECK(rbuf != NULL);
    for (int i = 0; i < N; i++) {
        uint8_t pl[PL];
        memset(pl, 'a' + (i % 26), sizeof(pl));
        build_message(req + off, &len, KBDD_IPC_OP_PING, pl, PL, (uint16_t)(i + 1), 0);
        off += len;
    }
    CHECK(write(ts.client, req, off) == (ssize_t)off);   /* never read until now */
    rlen = pump(&ts, rbuf, N * (KBDD_IPC_HEADER_SIZE + PL) + 64, 0, 10);
    for (;;) {
        uint16_t op, rid; const uint8_t *pl; uint32_t pn;
        if (!next_frame(rbuf, rlen, &roff, &op, &rid, &pl, &pn)) break;
        CHECK(op == KBDD_IPC_OP_PING);
        CHECK(rid == (uint16_t)(got + 1));
        CHECK(pn == PL && pl[0] == (uint8_t)('a' + (got % 26)));
        got++;
    }
    CHECK(got == N);
    free(rbuf);
    test_teardown(&ts);
}

/* A client that sends more request data than the daemon will answer at once
 * gets every reply: reading pauses while its backlog is large, then resumes. */
static void test_backpressure_resumes_processing(void)
{
    enum { N = 40, PL = 60000 };
    test_srv_t ts;
    uint8_t *pl = malloc(PL), *msg = malloc(KBDD_IPC_HEADER_SIZE + PL);
    uint8_t *rbuf = malloc((size_t)N * (KBDD_IPC_HEADER_SIZE + PL));
    size_t sent_total = 0, msg_len = 0, rlen = 0, roff = 0;
    int next = 0, got = 0, idle = 0;

    CHECK(pl != NULL && msg != NULL && rbuf != NULL);
    memset(pl, 'z', PL);
    test_setup(&ts);
    set_nonblock(ts.client);

    while (got < N && idle < 200) {
        int progressed = 0;
        ssize_t r;
        uint16_t op, rid; const uint8_t *p; uint32_t pn;

        if (next < N && sent_total == msg_len) {
            build_message(msg, &msg_len, KBDD_IPC_OP_PING, pl, PL, (uint16_t)(next + 1), 0);
            sent_total = 0;
            next++;
        }
        if (sent_total < msg_len) {
            r = send(ts.client, msg + sent_total, msg_len - sent_total, MSG_DONTWAIT);
            if (r > 0) { sent_total += (size_t)r; progressed = 1; }
        }
        (void)poll_server(&ts);
        r = recv(ts.client, rbuf + rlen, (size_t)N * (KBDD_IPC_HEADER_SIZE + PL) - rlen, MSG_DONTWAIT);
        if (r > 0) { rlen += (size_t)r; progressed = 1; }
        while (next_frame(rbuf, rlen, &roff, &op, &rid, &p, &pn)) {
            CHECK(rid == (uint16_t)(got + 1) && pn == PL);
            got++;
            progressed = 1;
        }
        idle = progressed ? 0 : idle + 1;
    }
    CHECK(got == N);
    free(pl); free(msg); free(rbuf);
    test_teardown(&ts);
}

/* A watcher that does not read loses events, not its connection, and is told
 * how many it missed once it catches up. */
static void test_slow_watcher_keeps_connection(void)
{
    enum { EVENTS = 20000 };
    test_srv_t ts;
    uint8_t frame[KBDD_IPC_HEADER_SIZE];
    size_t len, cap = (size_t)8 * 1024 * 1024, rlen, roff = 0;
    uint8_t *rbuf = malloc(cap);
    char ev[128];
    int last = -1, events = 0, warned = 0;
    uint16_t op, rid; const uint8_t *p; uint32_t pn;

    CHECK(rbuf != NULL);
    test_setup(&ts);
    set_nonblock(ts.client);
    build_message(frame, &len, KBDD_IPC_OP_WATCH, NULL, 0, 7, 0);
    CHECK(write(ts.client, frame, len) == (ssize_t)len);
    rlen = pump(&ts, rbuf, cap, 0, 3);              /* WATCH reply */
    CHECK(next_frame(rbuf, rlen, &roff, &op, &rid, &p, &pn) && op == KBDD_IPC_OP_WATCH);

    for (int i = 0; i < EVENTS; i++) {               /* 20000 x ~120 B, never read */
        snprintf(ev, sizeof(ev), "event %06d %0*d", i, 90, 0);
        CHECK(kbdd_ipc_server_publish(ts.server, ev) == 0);
    }
    rlen = pump(&ts, rbuf, cap, rlen, 5);
    while (next_frame(rbuf, rlen, &roff, &op, &rid, &p, &pn)) {
        CHECK(op == KBDD_IPC_OP_EVENT);
        if (strncmp((const char *)p, "event ", 6) == 0) {
            int n = atoi((const char *)p + 6);
            CHECK(n > last);                         /* ordered, never torn */
            last = n;
            events++;
        }
    }
    CHECK(events > 0 && events < EVENTS);            /* some were dropped */
    CHECK(last >= 0);

    /* Still connected: next event is preceded by a drop notice. */
    CHECK(kbdd_ipc_server_publish(ts.server, "event after") == 0);
    rlen = pump(&ts, rbuf, cap, rlen, 3);
    while (next_frame(rbuf, rlen, &roff, &op, &rid, &p, &pn)) {
        if (strstr((const char *)p, "dropped") != NULL) warned = 1;
    }
    CHECK(warned);

    /* ...and requests still work on the same connection. */
    build_message(frame, &len, KBDD_IPC_OP_PING, NULL, 0, 0x55, 0);
    CHECK(write(ts.client, frame, len) == (ssize_t)len);
    rlen = pump(&ts, rbuf, cap, rlen, 3);
    {
        int pong = 0;
        while (next_frame(rbuf, rlen, &roff, &op, &rid, &p, &pn)) {
            if (op == KBDD_IPC_OP_PING && rid == 0x55) pong = 1;
        }
        CHECK(pong);
    }
    free(rbuf);
    test_teardown(&ts);
}

int main(void)
{
    test_valid_ping();
    test_multiple_messages_in_one_send();
    test_partial_message_waits_for_payload();
    check_invalid_magic();
    check_invalid_version();
    check_oversized_payload_len();
    test_watch_receives_event();
    test_non_watch_does_not_receive_event();
    test_deferred_reply();
    test_pipelined_replies_not_lost();
    test_backpressure_resumes_processing();
    test_slow_watcher_keeps_connection();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all ipc framing tests");
    return 0;
}
