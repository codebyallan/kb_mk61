#define _GNU_SOURCE
#include "kbdd.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct kbdd_ipc_client {
    int fd;
    uid_t uid;
    uint8_t *rx;
    size_t rx_len;
    size_t rx_cap;
    int has_header;
    uint32_t expected_payload;
    time_t rl_window_start;
    unsigned int rl_count;
    int watching;
    uint8_t *tx;                /* queued outbound bytes: [tx_off, tx_len) */
    size_t tx_off;
    size_t tx_len;
    size_t tx_cap;
    unsigned long tx_dropped;   /* watch events dropped while the queue was full */
};

struct kbdd_ipc_server {
    int fd;
    char path[256];
    struct kbdd_ipc_client clients[KBDD_MAX_CLIENTS];
    size_t client_count;
};

static void (*kbdd_ipc_close_cb)(int fd, void *ctx);
static void *kbdd_ipc_close_cb_ctx;

void kbdd_ipc_server_set_close_cb(void (*cb)(int fd, void *ctx), void *ctx)
{
    kbdd_ipc_close_cb = cb;
    kbdd_ipc_close_cb_ctx = ctx;
}

static void kbdd_ipc_client_reset(struct kbdd_ipc_client *c)
{
    if (c == NULL) {
        return;
    }
    free(c->rx);
    free(c->tx);
    c->tx = NULL;
    c->tx_off = c->tx_len = c->tx_cap = 0;
    c->rx = NULL;
    c->rx_len = 0;
    c->rx_cap = 0;
    c->has_header = 0;
    c->expected_payload = 0;
}

static void kbdd_ipc_client_close(struct kbdd_ipc_server *s, size_t idx)
{
    if (s == NULL || idx >= KBDD_MAX_CLIENTS) {
        return;
    }
    struct kbdd_ipc_client *c = &s->clients[idx];
    if (c->fd >= 0) {
        int old_fd = c->fd;
        close(c->fd);
        if (kbdd_ipc_close_cb) {
            kbdd_ipc_close_cb(old_fd, kbdd_ipc_close_cb_ctx);
        }
    }
    kbdd_ipc_client_reset(c);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    /* compact: move last to idx */
    if (idx + 1 < s->client_count) {
        s->clients[idx] = s->clients[s->client_count - 1];
        memset(&s->clients[s->client_count - 1], 0, sizeof(s->clients[0]));
        s->clients[s->client_count - 1].fd = -1;
    }
    if (s->client_count > 0) {
        s->client_count--;
    }
}

/* Buffers above this size are released once the queue drains. */
#define KBDD_IPC_TX_KEEP 65536u

static size_t kbdd_ipc_tx_pending(const struct kbdd_ipc_client *c)
{
    return c->tx_len - c->tx_off;
}

/* Write as much queued data as the socket accepts. A full socket buffer
 * (EAGAIN) is normal back-pressure: the rest stays queued and is retried on
 * POLLOUT. Returns 0, or -1 if the client had to be closed. */
static int kbdd_ipc_client_flush(struct kbdd_ipc_server *s, size_t idx)
{
    struct kbdd_ipc_client *c = &s->clients[idx];

    while (c->tx_off < c->tx_len) {
        ssize_t w = send(c->fd, c->tx + c->tx_off, c->tx_len - c->tx_off,
                         MSG_NOSIGNAL);
        if (w > 0) {
            c->tx_off += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        kbdd_ipc_client_close(s, idx);
        return -1;
    }
    c->tx_off = 0;
    c->tx_len = 0;
    if (c->tx_cap > KBDD_IPC_TX_KEEP) {
        free(c->tx);
        c->tx = NULL;
        c->tx_cap = 0;
    }
    return 0;
}

static void kbdd_ipc_put_header(uint8_t *h, uint16_t opcode, uint16_t request_id,
                                uint16_t flags, uint32_t plen)
{
    uint32_t magic = KBDD_IPC_MAGIC;

    h[0] = (uint8_t)magic;        h[1] = (uint8_t)(magic >> 8);
    h[2] = (uint8_t)(magic >> 16); h[3] = (uint8_t)(magic >> 24);
    h[4] = (uint8_t)KBDD_IPC_VERSION; h[5] = (uint8_t)(KBDD_IPC_VERSION >> 8);
    h[6] = (uint8_t)flags;        h[7] = (uint8_t)(flags >> 8);
    h[8] = (uint8_t)plen;         h[9] = (uint8_t)(plen >> 8);
    h[10] = (uint8_t)(plen >> 16); h[11] = (uint8_t)(plen >> 24);
    h[12] = (uint8_t)opcode;      h[13] = (uint8_t)(opcode >> 8);
    h[14] = (uint8_t)request_id;  h[15] = (uint8_t)(request_id >> 8);
}

/* Append one frame (header + payload, contiguous) to the client's TX queue and
 * try to send it. Returns 0 if queued, 1 if dropped (only when `droppable`
 * and the queue is full), -1 if the client was closed. Replies are never
 * dropped: a client that leaves KBDD_IPC_TX_MAX bytes unread is closed. */
static int kbdd_ipc_queue_msg(struct kbdd_ipc_server *s, size_t idx,
                              uint16_t opcode, uint16_t request_id,
                              uint16_t flags, const uint8_t *payload,
                              uint32_t plen, int droppable)
{
    struct kbdd_ipc_client *c = &s->clients[idx];
    size_t total = KBDD_IPC_HEADER_SIZE + (size_t)plen;
    size_t pending = kbdd_ipc_tx_pending(c);

    if (pending + total > KBDD_IPC_TX_MAX) {
        if (droppable) {
            return 1;
        }
        fprintf(stderr, "kbdd: client fd %d is not reading replies; closing\n", c->fd);
        kbdd_ipc_client_close(s, idx);
        return -1;
    }
    if (c->tx_off > 0 && c->tx_len + total > c->tx_cap) {
        memmove(c->tx, c->tx + c->tx_off, pending);
        c->tx_len = pending;
        c->tx_off = 0;
    }
    if (c->tx_len + total > c->tx_cap) {
        size_t ncap = c->tx_cap != 0 ? c->tx_cap : 1024;
        uint8_t *nb;

        while (ncap < c->tx_len + total) {
            ncap *= 2;
        }
        nb = realloc(c->tx, ncap);
        if (nb == NULL) {
            kbdd_ipc_client_close(s, idx);
            return -1;
        }
        c->tx = nb;
        c->tx_cap = ncap;
    }
    kbdd_ipc_put_header(c->tx + c->tx_len, opcode, request_id, flags, plen);
    if (plen > 0) {
        memcpy(c->tx + c->tx_len + KBDD_IPC_HEADER_SIZE, payload, plen);
    }
    c->tx_len += total;
    return kbdd_ipc_client_flush(s, idx);
}

kbdd_ipc_server_t *kbdd_ipc_server_create(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == NULL || runtime[0] == '\0') {
        return NULL;
    }
    struct stat rst;
    if (lstat(runtime, &rst) != 0 || !S_ISDIR(rst.st_mode) ||
        rst.st_uid != getuid() || (rst.st_mode & 0777) != 0700) {
        return NULL;
    }

    kbdd_ipc_server_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->fd = -1;
    for (size_t i = 0; i < KBDD_MAX_CLIENTS; i++) {
        s->clients[i].fd = -1;
    }

    snprintf(s->path, sizeof(s->path), "%s/kbdd.sock", runtime);

    umask(077);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        free(s);
        return NULL;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(s->path) >= sizeof(addr.sun_path)) {
        close(fd);
        free(s);
        return NULL;
    }
    strncpy(addr.sun_path, s->path, sizeof(addr.sun_path) - 1);

    unlink(s->path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        free(s);
        return NULL;
    }
    chmod(s->path, 0600);
    if (listen(fd, 16) < 0) {
        close(fd);
        unlink(s->path);
        free(s);
        return NULL;
    }
    s->fd = fd;
    return s;
}

void kbdd_ipc_server_destroy(kbdd_ipc_server_t *s)
{
    if (s == NULL) {
        return;
    }
    for (size_t i = 0; i < s->client_count; i++) {
        if (s->clients[i].fd >= 0) {
            close(s->clients[i].fd);
        }
        kbdd_ipc_client_reset(&s->clients[i]);
    }
    if (s->fd >= 0) {
        close(s->fd);
    }
    unlink(s->path);
    free(s);
}

int kbdd_ipc_server_fd(const kbdd_ipc_server_t *s)
{
    return s != NULL ? s->fd : -1;
}

int kbdd_ipc_server_accept(kbdd_ipc_server_t *s)
{
    if (s == NULL || s->fd < 0) {
        return -1;
    }
    for (;;) {
        int cfd = accept4(s->fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }
        if (s->client_count >= KBDD_MAX_CLIENTS) {
            close(cfd);
            continue;
        }

        struct ucred cr;
        socklen_t crlen = sizeof(cr);
        if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) != 0 ||
            crlen != sizeof(cr) || cr.uid != getuid()) {
            close(cfd);
            continue;
        }

        size_t idx = s->client_count;
        memset(&s->clients[idx], 0, sizeof(s->clients[idx]));
        s->clients[idx].fd = cfd;
        s->clients[idx].uid = cr.uid;
        s->client_count++;
    }
    return 0;
}

/* Read a uint32/uint16 little-endian from buffer. */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int kbdd_ipc_ensure_cap(struct kbdd_ipc_client *c, size_t need)
{
    if (c->rx_cap >= need) {
        return 0;
    }
    size_t ncap = c->rx_cap == 0 ? 256 : c->rx_cap;
    while (ncap < need) {
        if (ncap > (KBDD_IPC_HEADER_SIZE + KBDD_IPC_MAX_PAYLOAD)) {
            return -1;
        }
        ncap *= 2;
    }
    uint8_t *nr = realloc(c->rx, ncap);
    if (nr == NULL) {
        return -1;
    }
    c->rx = nr;
    c->rx_cap = ncap;
    return 0;
}

static size_t kbdd_ipc_client_find(const kbdd_ipc_server_t *s, int fd)
{
    for (size_t i = 0; i < s->client_count; i++) {
        if (s->clients[i].fd == fd) {
            return i;
        }
    }
    return SIZE_MAX;
}

static int kbdd_ipc_handle_client(struct kbdd_ipc_server *s, size_t idx,
                                  kbdd_ipc_handler_t handler, void *ctx,
                                  int do_read)
{
    struct kbdd_ipc_client *c = &s->clients[idx];
    const int client_fd = c->fd;
    /* read available data */
    for (; do_read;) {
        /* One maximal frame is buffered: process it before reading more
         * (the rest stays in the socket; poll wakes us again). Growing the
         * buffer past this point used to close pipelining clients. */
        if (c->rx_len >= KBDD_IPC_HEADER_SIZE + KBDD_IPC_MAX_PAYLOAD) {
            break;
        }
        if (kbdd_ipc_ensure_cap(c, c->rx_len + 4096) != 0) {
            kbdd_ipc_client_close(s, idx);
            return -1;
        }
        ssize_t r = recv(c->fd, c->rx + c->rx_len, c->rx_cap - c->rx_len, 0);
        if (r > 0) {
            c->rx_len += (size_t)r;
        } else if (r == 0) {
            kbdd_ipc_client_close(s, idx);
            return 0;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            kbdd_ipc_client_close(s, idx);
            return -1;
        }
    }

    /* process complete messages */
    for (;;) {
        /* A handler may close this client (which compacts the array), so
         * re-resolve it by descriptor before touching any client state. */
        size_t cidx = kbdd_ipc_client_find(s, client_fd);
        if (cidx == SIZE_MAX) {
            return 0;
        }
        c = &s->clients[cidx];

        /* Back-pressure: leave further requests unprocessed until the client
         * has drained its replies (resumed from the POLLOUT path). */
        if (kbdd_ipc_tx_pending(c) >= KBDD_IPC_TX_HIGH) {
            break;
        }

        if (!c->has_header) {
            if (c->rx_len < KBDD_IPC_HEADER_SIZE) {
                break;
            }
            uint32_t magic = rd32(c->rx);
            if (magic != KBDD_IPC_MAGIC) {
                kbdd_ipc_client_close(s, cidx);
                return -1;
            }
            uint16_t version = rd16(c->rx + 4);
            if (version != KBDD_IPC_VERSION) {
                kbdd_ipc_client_close(s, cidx);
                return -1;
            }
            /* flags at +6 ignored */
            uint32_t plen = rd32(c->rx + 8);
            if (plen > KBDD_IPC_MAX_PAYLOAD) {
                kbdd_ipc_client_close(s, cidx);
                return -1;
            }
            c->expected_payload = plen;
            c->has_header = 1;
        }

        size_t total = KBDD_IPC_HEADER_SIZE + c->expected_payload;
        if (c->rx_len < total) {
            break;
        }

        /* build request */
        kbdd_ipc_message_t req;
        memset(&req, 0, sizeof(req));
        req.client_fd = client_fd;
        req.magic = KBDD_IPC_MAGIC;
        req.version = KBDD_IPC_VERSION;
        req.flags = rd16(c->rx + 6);
        req.payload_len = c->expected_payload;
        req.opcode = rd16(c->rx + 12);
        req.request_id = rd16(c->rx + 14);
        if (req.payload_len > 0) {
            req.payload = malloc(req.payload_len);
            if (req.payload == NULL) {
                kbdd_ipc_client_close(s, cidx);
                return -1;
            }
            memcpy(req.payload, c->rx + KBDD_IPC_HEADER_SIZE, req.payload_len);
        }

        kbdd_ipc_message_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.version = KBDD_IPC_VERSION;
        resp.request_id = req.request_id;
        resp.opcode = req.opcode;
        if (req.opcode == KBDD_IPC_OP_WATCH) {
            c->watching = 1;
        }

        /* simple per-client rate limiting (requests per second) */
        time_t now = time(NULL);
        if (c->rl_window_start == 0 || c->rl_window_start != now) {
            c->rl_window_start = now;
            c->rl_count = 0;
        }
        if (c->rl_count >= 256) {
            kbdd_ipc_client_close(s, cidx);
            free(req.payload);
            return -1;
        }
        c->rl_count++;

        /* Consume the request before invoking the handler: deferred replies
         * are keyed by descriptor, and the handler is free to close the
         * client, which would invalidate this receive buffer. */
        if (c->rx_len > total) {
            memmove(c->rx, c->rx + total, c->rx_len - total);
        }
        c->rx_len -= total;
        c->has_header = 0;
        c->expected_payload = 0;

        int rc = -1;
        if (handler != NULL) {
            rc = handler(&req, &resp, ctx);
        }
        free(req.payload);

        if (rc == KBDD_HANDLER_DEFER) {
            free(resp.payload);
            continue;
        }

        /* The handler may have closed or replaced the client: find the
         * destination again before writing the reply. */
        cidx = kbdd_ipc_client_find(s, client_fd);
        if (cidx == SIZE_MAX) {
            free(resp.payload);
            return 0;
        }
        c = &s->clients[cidx];

        /* Queue the reply (one contiguous frame); a full socket buffer is not
         * an error, the rest is flushed when the client becomes writable. */
        if (kbdd_ipc_queue_msg(s, cidx, resp.opcode, resp.request_id, resp.flags,
                               resp.payload, resp.payload != NULL ? resp.payload_len : 0,
                               0) < 0) {
            free(resp.payload);
            return -1;
        }
        free(resp.payload);
    }

    return 0;
}

int kbdd_ipc_server_publish(kbdd_ipc_server_t *s, const char *event)
{
    size_t i;
    size_t len;

    if (s == NULL || event == NULL) return -1;
    len = strlen(event) + 1;
    if (len > KBDD_IPC_MAX_PAYLOAD) return -1;
    for (i = 0; i < s->client_count;) {
        struct kbdd_ipc_client *c = &s->clients[i];
        int rc = 0;

        if (!c->watching) {
            i++;
            continue;
        }
        /* A slow watcher loses events instead of its connection; tell it. */
        if (c->tx_dropped > 0) {
            char note[96];
            int n = snprintf(note, sizeof(note),
                             "warning: %lu event(s) dropped, client reading too slowly",
                             c->tx_dropped);
            rc = kbdd_ipc_queue_msg(s, i, KBDD_IPC_OP_EVENT, 0, 0,
                                    (const uint8_t *)note, (uint32_t)n + 1, 1);
            if (rc == 0) {
                c->tx_dropped = 0;
            }
        }
        if (rc >= 0) {
            rc = kbdd_ipc_queue_msg(s, i, KBDD_IPC_OP_EVENT, 0, 0,
                                    (const uint8_t *)event, (uint32_t)len, 1);
            if (rc == 1) {
                c->tx_dropped++;
            }
        }
        if (rc < 0) {
            continue;   /* client closed; slot now holds another client */
        }
        i++;
    }
    return 0;
}

int kbdd_ipc_server_send(kbdd_ipc_server_t *s, int fd, uint16_t opcode,
                         uint16_t request_id, const uint8_t *payload,
                         uint32_t payload_len)
{
    struct kbdd_ipc_client *c = NULL;
    if (s == NULL || fd < 0 || (payload_len > 0 && payload == NULL)) {
        return -1;
    }
    if (payload_len > KBDD_IPC_MAX_PAYLOAD) {
        return -1;
    }
    for (size_t i = 0; i < s->client_count; i++) {
        if (s->clients[i].fd == fd) {
            c = &s->clients[i];
            break;
        }
    }
    if (c == NULL) {
        return -1;
    }

    return kbdd_ipc_queue_msg(s, (size_t)(c - s->clients), opcode, request_id, 0,
                              payload, payload_len, 0) < 0 ? -1 : 0;
}

int kbdd_ipc_server_poll(kbdd_ipc_server_t *s, int timeout_ms,
                         kbdd_ipc_handler_t handler, void *ctx)
{
    if (s == NULL) {
        return -1;
    }
    kbdd_ipc_server_accept(s);

    struct pollfd fds[1 + KBDD_MAX_CLIENTS];
    nfds_t nfds = 0;
    fds[nfds].fd = s->fd;
    fds[nfds].events = POLLIN;
    fds[nfds].revents = 0;
    nfds++;

    for (size_t i = 0; i < s->client_count; i++) {
        const struct kbdd_ipc_client *c = &s->clients[i];
        size_t pending = kbdd_ipc_tx_pending(c);

        fds[nfds].fd = c->fd;
        /* Stop reading new requests while the client has a large unread backlog. */
        fds[nfds].events = (short)((pending < KBDD_IPC_TX_HIGH ? POLLIN : 0) |
                                   (pending > 0 ? POLLOUT : 0));
        fds[nfds].revents = 0;
        nfds++;
    }

    int r = poll(fds, nfds, timeout_ms);
    if (r < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    if (nfds > 0 && (fds[0].revents & POLLIN)) {
        kbdd_ipc_server_accept(s);
    }

    /* Handle each client once. A close compacts the array (another client
     * moves into slot i), so re-check the slot by descriptor afterwards. */
    size_t i = 0;
    while (i < s->client_count) {
        const int fd = s->clients[i].fd;
        short rev = 0;

        for (nfds_t j = 1; j < nfds; j++) {
            if (fds[j].fd == fd) {
                rev = fds[j].revents;
                break;
            }
        }
        if ((rev & (POLLOUT | POLLERR | POLLHUP)) != 0 &&
            kbdd_ipc_tx_pending(&s->clients[i]) > 0) {
            (void)kbdd_ipc_client_flush(s, i);
        }
        if (i < s->client_count && s->clients[i].fd == fd) {
            int want_read = (rev & (POLLIN | POLLHUP | POLLERR)) != 0;

            /* POLLOUT without POLLIN: the backlog drained, so requests held
             * back by back-pressure can now be processed. */
            if (want_read || (rev & POLLOUT) != 0) {
                (void)kbdd_ipc_handle_client(s, i, handler, ctx, want_read);
            }
        }
        if (i < s->client_count && s->clients[i].fd == fd) {
            i++;
        }
    }

    return 0;
}
