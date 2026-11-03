/* Fuzz harness for the parsers exposed to untrusted input.
 *
 * Input layout: byte 0 selects the target, the rest is the payload.
 *   0 feature/input report parse + re-encode     4 CLI argument parsers
 *   1 profile decoders                           5 daemon config file loader
 *   2 macro decode/encode                        6 IPC wire framing (real socket)
 *   3 profile-block builders
 *
 * Built two ways:
 *   - libFuzzer (clang, -fsanitize=fuzzer): `make fuzz`
 *   - standalone replay driver (any compiler): replays files/directories given
 *     on the command line, then optionally `-m N` seeded mutations of them.
 *     This is what `make test` / `make asan` run as a regression suite.
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "../../src/daemon/kbdd.h"
#include "../../src/cli/kbctl_parse.h"
#include "kbcore.h"
#include "kbipc.h"

static char rt_dir[64];
static kbdd_ipc_server_t *ipc;

static void target_reports(const uint8_t *d, size_t n)
{
    kbc_feature_report fr;
    kbc_input_report ir;
    uint8_t out[KBC_FEATURE_REPORT_SIZE];

    (void)kbc_validate_opcode(n ? d[0] : 0);
    if (kbc_parse_feature_report(d, n, &fr) == KBC_OK) {
        (void)kbc_encode_feature_report(&fr, out, sizeof out);
    }
    (void)kbc_parse_input_report(d, n, &ir);
}

static void target_profile(const uint8_t *d, size_t n)
{
    kbc_profile *p = kbc_profile_create(KBC_KEY_POSITIONS, 61);
    uint8_t hw = 0;

    if (p == NULL) {
        return;
    }
    (void)kbc_profile_decode_key_matrix(p, d, n, &hw);
    (void)kbc_profile_decode_params_b(p, d, n, &hw);
    (void)kbc_profile_decode_block_shifted(p, d, n, 1, &hw);
    (void)kbc_profile_decode_block_shifted(p, d, n, 0, &hw);
    (void)kbc_profile_decode_extra(p, d, n, &hw);
    (void)kbc_read_active_profile_decode(d, n, &hw);
    (void)kbc_profile_validate(p);
    kbc_profile_destroy(p);
}

static void target_macro(const uint8_t *d, size_t n)
{
    kbc_macro *m = kbc_macro_create();
    uint8_t out[KBC_MACRO_SIZE];

    if (m == NULL) {
        return;
    }
    if (kbc_macro_decode(m, d, n) == KBC_OK) {
        (void)kbc_macro_encode(m, out, sizeof out);
    }
    kbc_macro_destroy(m);
}

static void target_block(const uint8_t *d, size_t n)
{
    uint8_t out[KBC_FEATURE_REPORT_SIZE];
    uint8_t rgb[2][3] = {{1, 2, 3}, {4, 5, 6}};
    uint8_t sel = n ? d[0] : 0;
    uint8_t prof = (uint8_t)(1 + sel % 4);

    (void)kbc_profile_block_to_write(d, n, prof, out, sizeof out);
    (void)kbc_profile_block_commit_report(d, n, prof, KBC_COMMIT_ARM, out, sizeof out);
    (void)kbc_profile_block_set_mode(d, n, prof, sel, out, sizeof out);
    (void)kbc_profile_block_set_field(d, n, prof, (kbc_profile_field)(1 + sel % 5),
                                      n > 1 ? d[1] : 0, out, sizeof out);
    (void)kbc_profile_block_set_colors(d, n, prof, sel % 13,
                                       (const uint8_t (*)[3])rgb, 2, out, sizeof out);
}

static void target_cli(const uint8_t *d, size_t n)
{
    char *s = malloc(n + 1);
    long idx;
    unsigned long u;
    uint8_t r, g, b;

    if (s == NULL) {
        return;
    }
    memcpy(s, d, n);
    s[n] = '\0';
    (void)kbctl_parse_index(s, &idx);
    (void)kbctl_parse_rgb(s, &r, &g, &b);
    (void)kbctl_parse_uint(s, 1024, &u);
    (void)kbctl_parse_uint(s, ~0UL, &u);
    free(s);
}

static void target_config(const uint8_t *d, size_t n)
{
    char path[96];
    kbdd_ctx_t k;
    FILE *f;

    snprintf(path, sizeof path, "%s/config", rt_dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    if (n > 0) {
        (void)fwrite(d, 1, n, f);
    }
    fclose(f);
    kbdd_config_defaults(&k);
    (void)kbdd_load_config_file(&k, path);
    unlink(path);
    /* A rejected file is moved aside; remove that too. */
    snprintf(path, sizeof path, "%s/config.invalid", rt_dir);
    unlink(path);
}

static int echo_handler(const kbdd_ipc_message_t *req, kbdd_ipc_message_t *resp,
                        void *ctx)
{
    (void)ctx;
    resp->payload = NULL;
    resp->payload_len = 0;
    if (req->payload_len > 0) {
        resp->payload = malloc(req->payload_len);
        if (resp->payload == NULL) {
            return -1;
        }
        memcpy(resp->payload, req->payload, req->payload_len);
        resp->payload_len = req->payload_len;
    }
    return 0;
}

static void target_ipc(const uint8_t *d, size_t n)
{
    struct sockaddr_un addr;
    uint8_t sink[512];
    int fd, i;

    if (ipc == NULL) {
        return;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s/kbdd.sock", rt_dir);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
        size_t off = 0;
        while (off < n) {
            ssize_t w = send(fd, d + off, n - off, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (w <= 0) {
                break;
            }
            off += (size_t)w;
        }
        for (i = 0; i < 4; i++) {
            (void)kbdd_ipc_server_poll(ipc, 0, echo_handler, NULL);
            while (recv(fd, sink, sizeof sink, MSG_DONTWAIT) > 0) {
            }
        }
    }
    close(fd);
    (void)kbdd_ipc_server_poll(ipc, 0, echo_handler, NULL); /* reap */
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    char tmpl[] = "/tmp/kbdd-fuzz-XXXXXX";

    (void)argc; (void)argv;
    signal(SIGPIPE, SIG_IGN);
    if (mkdtemp(tmpl) == NULL) {
        return 0;
    }
    snprintf(rt_dir, sizeof rt_dir, "%s", tmpl);
    setenv("XDG_RUNTIME_DIR", rt_dir, 1);
    ipc = kbdd_ipc_server_create();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t empty[1] = {0};
    const uint8_t *p = size > 1 ? data + 1 : empty;
    size_t n = size > 1 ? size - 1 : 0;

    if (size == 0) {
        return 0;
    }
    switch (data[0] % 7) {
    case 0: target_reports(p, n); break;
    case 1: target_profile(p, n); break;
    case 2: target_macro(p, n); break;
    case 3: target_block(p, n); break;
    case 4: target_cli(p, n); break;
    case 5: target_config(p, n); break;
    default: target_ipc(p, n); break;
    }
    return 0;
}

#ifndef FUZZ_LIBFUZZER
/* ---- standalone replay driver ---- */
#include <dirent.h>

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next_rand(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

typedef struct { uint8_t *buf; size_t len; } seed_t;
static seed_t *seeds;
static size_t nseeds;

/* Run on an exact-size heap copy so ASan sees any read past the input end. */
static void run_exact(const uint8_t *b, size_t len)
{
    uint8_t *c = malloc(len > 0 ? len : 1);

    if (c == NULL) {
        return;
    }
    if (len > 0) {
        memcpy(c, b, len);
    }
    LLVMFuzzerTestOneInput(c, len);
    free(c);
}

static int load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    seed_t *tmp;

    if (f == NULL) {
        fprintf(stderr, "fuzz: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (1 << 20)) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)sz + 1);
    if (buf == NULL || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    tmp = realloc(seeds, (nseeds + 1) * sizeof *seeds);
    if (tmp == NULL) {
        free(buf);
        return -1;
    }
    seeds = tmp;
    seeds[nseeds].buf = buf;
    seeds[nseeds].len = (size_t)sz;
    nseeds++;
    run_exact(buf, (size_t)sz);
    return 0;
}

static int load_path(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;
    char sub[512];

    if (stat(path, &st) != 0) {
        fprintf(stderr, "fuzz: cannot stat %s\n", path);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return load_file(path);
    }
    d = opendir(path);
    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        if (load_path(sub) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

static void mutate_and_run(const seed_t *s)
{
    size_t cap = s->len + 64, len = s->len, i, flips;
    uint8_t *b = malloc(cap);

    if (b == NULL) {
        return;
    }
    memcpy(b, s->buf, s->len);
    flips = 1 + next_rand() % 8;
    for (i = 0; i < flips; i++) {
        switch (next_rand() % 4) {
        case 0: if (len) b[next_rand() % len] ^= (uint8_t)(1u << (next_rand() % 8)); break;
        case 1: if (len) b[next_rand() % len] = (uint8_t)next_rand(); break;
        case 2: if (len > 1) len -= 1 + next_rand() % (len > 8 ? 8 : len - 1); break;
        default: if (len + 8 < cap) { b[len++] = (uint8_t)next_rand(); } break;
        }
    }
    run_exact(b, len);
    free(b);
}

int main(int argc, char **argv)
{
    unsigned long mutations = 0, i;
    int a;

    LLVMFuzzerInitialize(&argc, &argv);
    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-m") == 0 && a + 1 < argc) {
            mutations = strtoul(argv[++a], NULL, 10);
        } else if (load_path(argv[a]) != 0) {
            return 1;
        }
    }
    if (nseeds == 0) {
        fprintf(stderr, "usage: fuzz-replay [-m N] <corpus file|dir>...\n");
        return 1;
    }
    for (i = 0; i < mutations; i++) {
        mutate_and_run(&seeds[next_rand() % nseeds]);
    }
    if (ipc != NULL) {
        kbdd_ipc_server_destroy(ipc);
    }
    rmdir(rt_dir);
    printf("ok: fuzz replay (%zu seeds, %lu mutations)\n", nseeds, mutations);
    return 0;
}
#endif
