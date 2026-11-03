/* Tests for the kbdd IPC wire framing using a real Unix socket server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("ok: all ipc framing tests");
    return 0;
}
