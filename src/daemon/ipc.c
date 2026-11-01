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
};

struct kbdd_ipc_server {
    int fd;
    char path[256];
    struct kbdd_ipc_client clients[KBDD_MAX_CLIENTS];
    size_t client_count;
};

static void kbdd_ipc_client_reset(struct kbdd_ipc_client *c)
{
    if (c == NULL) {
        return;
    }
    free(c->rx);
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
        close(c->fd);
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

static int kbdd_ipc_handle_client(struct kbdd_ipc_server *s, size_t idx,
                                  kbdd_ipc_handler_t handler, void *ctx)
{
    struct kbdd_ipc_client *c = &s->clients[idx];
    /* read available data */
    for (;;) {
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
        if (!c->has_header) {
            if (c->rx_len < KBDD_IPC_HEADER_SIZE) {
                break;
            }
            uint32_t magic = rd32(c->rx);
            if (magic != KBDD_IPC_MAGIC) {
                kbdd_ipc_client_close(s, idx);
                return -1;
            }
            uint16_t version = rd16(c->rx + 4);
            if (version != KBDD_IPC_VERSION) {
                kbdd_ipc_client_close(s, idx);
                return -1;
            }
            /* flags at +6 ignored */
            uint32_t plen = rd32(c->rx + 8);
            if (plen > KBDD_IPC_MAX_PAYLOAD) {
                kbdd_ipc_client_close(s, idx);
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
        req.magic = KBDD_IPC_MAGIC;
        req.version = KBDD_IPC_VERSION;
        req.flags = rd16(c->rx + 6);
        req.payload_len = c->expected_payload;
        req.opcode = rd16(c->rx + 12);
        req.request_id = rd16(c->rx + 14);
        if (req.payload_len > 0) {
            req.payload = malloc(req.payload_len);
            if (req.payload == NULL) {
                kbdd_ipc_client_close(s, idx);
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
            kbdd_ipc_client_close(s, idx);
            free(req.payload);
            return -1;
        }
        c->rl_count++;

        int rc = -1;
        if (handler != NULL) {
            rc = handler(&req, &resp, ctx);
        }
        free(req.payload);

        /* send response */
        /* build header */
        uint8_t header[KBDD_IPC_HEADER_SIZE];
        uint32_t rmagic = KBDD_IPC_MAGIC;
        header[0] = (uint8_t)(rmagic);
        header[1] = (uint8_t)(rmagic >> 8);
        header[2] = (uint8_t)(rmagic >> 16);
        header[3] = (uint8_t)(rmagic >> 24);
        header[4] = (uint8_t)(resp.version);
        header[5] = (uint8_t)(resp.version >> 8);
        header[6] = (uint8_t)(resp.flags);
        header[7] = (uint8_t)(resp.flags >> 8);
        uint32_t rplen = resp.payload_len;
        header[8] = (uint8_t)(rplen);
        header[9] = (uint8_t)(rplen >> 8);
        header[10] = (uint8_t)(rplen >> 16);
        header[11] = (uint8_t)(rplen >> 24);
        header[12] = (uint8_t)(resp.opcode);
        header[13] = (uint8_t)(resp.opcode >> 8);
        header[14] = (uint8_t)(resp.request_id);
        header[15] = (uint8_t)(resp.request_id >> 8);

        /* send header */
        size_t to_send = KBDD_IPC_HEADER_SIZE;
        const uint8_t *p = header;
        while (to_send > 0) {
            ssize_t w = send(c->fd, p, to_send, MSG_NOSIGNAL);
            if (w <= 0) {
                if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    /* best effort; close if cannot send fully */
                }
                kbdd_ipc_client_close(s, idx);
                free(resp.payload);
                return -1;
            }
            p += w;
            to_send -= (size_t)w;
        }
        if (resp.payload_len > 0 && resp.payload != NULL) {
            size_t rem = resp.payload_len;
            const uint8_t *pp = resp.payload;
            while (rem > 0) {
                ssize_t w = send(c->fd, pp, rem, MSG_NOSIGNAL);
                if (w <= 0) {
                    kbdd_ipc_client_close(s, idx);
                    free(resp.payload);
                    return -1;
                }
                pp += w;
                rem -= (size_t)w;
            }
        }
        free(resp.payload);

        /* consume processed bytes */
        size_t consumed = total;
        if (c->rx_len > consumed) {
            memmove(c->rx, c->rx + consumed, c->rx_len - consumed);
        }
        c->rx_len -= consumed;
        c->has_header = 0;
        c->expected_payload = 0;

        if (rc != 0) {
            /* error already signaled by closing? keep connection */
        }
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
        uint8_t header[KBDD_IPC_HEADER_SIZE];
        uint32_t magic = KBDD_IPC_MAGIC;
        uint16_t opcode = KBDD_IPC_OP_EVENT;
        uint16_t request_id = 0;
        size_t sent, payload_sent;

        if (!c->watching) {
            i++;
            continue;
        }
        header[0] = (uint8_t)magic; header[1] = (uint8_t)(magic >> 8);
        header[2] = (uint8_t)(magic >> 16); header[3] = (uint8_t)(magic >> 24);
        header[4] = KBDD_IPC_VERSION; header[5] = 0;
        header[6] = 0; header[7] = 0;
        header[8] = (uint8_t)len; header[9] = (uint8_t)(len >> 8);
        header[10] = (uint8_t)(len >> 16); header[11] = (uint8_t)(len >> 24);
        header[12] = (uint8_t)opcode; header[13] = (uint8_t)(opcode >> 8);
        header[14] = (uint8_t)request_id; header[15] = (uint8_t)(request_id >> 8);
        sent = 0;
        while (sent < sizeof(header)) {
            ssize_t n = send(c->fd, header + sent, sizeof(header) - sent,
                             MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += (size_t)n;
        }
        payload_sent = 0;
        while (sent == sizeof(header) && payload_sent < len) {
            ssize_t n = send(c->fd, event + payload_sent, len - payload_sent,
                             MSG_NOSIGNAL);
            if (n <= 0) break;
            payload_sent += (size_t)n;
        }
        if (sent != sizeof(header) || payload_sent != len) {
            kbdd_ipc_client_close(s, i);
            continue;
        }
        i++;
    }
    return 0;
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
        fds[nfds].fd = s->clients[i].fd;
        fds[nfds].events = POLLIN;
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

    /* process clients; iterate by index, but client_count may change */
    size_t i = 0;
    while (i < s->client_count) {
        /* find matching fd? We'll just process each current client once. */
        int ready = 0;
        /* map client index to fds index: 1 + original offset; but after accept compaction, simpler process all clients with POLLIN by checking recv readiness via poll per fd */
        /* We'll call handle for each client whose fd had revents; but indices may shift after close. Handle one at a time and restart loop. */
        /* Determine if this client's fd is in fds and ready */
        /* Build a quick check: */
        ready = 0;
        for (nfds_t j = 1; j < nfds; j++) {
            if (fds[j].fd == s->clients[i].fd && (fds[j].revents & (POLLIN | POLLHUP | POLLERR))) {
                ready = 1;
                break;
            }
        }
        if (ready) {
            if (kbdd_ipc_handle_client(s, i, handler, ctx) != 0) {
                /* client closed; do not increment */
                continue;
            }
        }
        i++;
    }

    return 0;
}