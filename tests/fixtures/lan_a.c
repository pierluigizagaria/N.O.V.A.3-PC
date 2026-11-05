/* Invented ELF32 socket-shape fixture. Contains no game code or packets. */
typedef unsigned int u32;
struct address { unsigned short family, port; u32 ip; unsigned char zero[8]; };
struct addrinfo {
    int flags, family, type, protocol;
    u32 length;
    char *canonical;
    struct address *address;
    struct addrinfo *next;
};
struct timeval { int seconds, micros; };
extern int *__errno(void);
extern int socket(int, int, int);
extern int bind(int, const void *, u32);
extern int setsockopt(int, int, int, const void *, u32);
extern int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
extern void freeaddrinfo(struct addrinfo *);
extern int getsockname(int, void *, u32 *);
extern int select(int, u32 *, u32 *, u32 *, struct timeval *);
extern int sendto(int, const void *, u32, int, const void *, u32);
extern int recvfrom(int, void *, u32, int, void *, u32 *);
extern int close(int);
extern void lan_event(const char *, int, int);
extern void lan_failure(const char *, int, int);
_Static_assert(sizeof(void *) == 4 && sizeof(struct address) == 16, "ELF32 sockaddr");
_Static_assert(sizeof(struct addrinfo) == 32 &&
               __builtin_offsetof(struct addrinfo, address) == 24, "bionic addrinfo");
_Static_assert(sizeof(struct timeval) == 8, "bionic timeval");
#define CHECK(tag, expression) do { if (!(expression)) { \
    lan_failure(tag, __LINE__, *__errno()); return __LINE__; } } while (0)
static void fill(void *p, unsigned char c, u32 n) {
    unsigned char *b = p; for (u32 i = 0; i < n; ++i) b[i] = c;
}
static int equal(const void *a, const void *b, u32 n) {
    const unsigned char *x = a, *y = b;
    for (u32 i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}
static int uniform(const void *p, unsigned char c, u32 n) {
    const unsigned char *b = p;
    for (u32 i = 0; i < n; ++i) if (b[i] != c) return 0;
    return 1;
}
static unsigned short swap(unsigned short p) { return (unsigned short)((p << 8) | (p >> 8)); }
static struct address endpoint(unsigned short port) {
    struct address a = {2, 0, 0x0100007fu, {0}}; a.port = swap(port); return a;
}
static int clean_address(const struct address *a) {
    return a->family == 2 && a->ip == 0x0100007fu && a->port && uniform(a->zero, 0, 8);
}
/* The original uses a 128-byte bionic bitset and zero timeout. Preserve that
 * immediate poll; later bounded waits are fixture scheduling, not game timing. */
static int ready(int fd, int wait) {
    struct { u32 before, bits[32], after; } set;
    struct timeval t = {0, 0};
    fill(&set, 0, sizeof set); set.before = set.after = 0xa5c3e789u;
    if (fd < 3 || fd >= 1024) return -2;
    set.bits[fd / 32] = 1u << (fd % 32);
    if (wait) t.seconds = 2; /* bounded harness wait after the immediate poll */
    int rc = select(fd + 1, set.bits, 0, 0, &t);
    if (set.before != 0xa5c3e789u || set.after != 0xa5c3e789u) return -3;
    if (rc >= 0 && ((set.bits[fd / 32] >> (fd % 32)) & 1u) != (u32)(rc == 1)) return -4;
    return rc;
}
static int close_exact(int fd) {
    CHECK("close-once", close(fd) == 0);
    *__errno() = 0;
    CHECK("double-close", close(fd) == -1 && *__errno() == 9);
    *__errno() = 0;
    CHECK("closed-select", ready(fd, 0) == -1 && *__errno() == 9);
    unsigned char byte = 0;
    struct address to = endpoint(7891);
    CHECK("closed-send", sendto(fd, &byte, 1, 0, &to, 16) == -1 && *__errno() == 9);
    CHECK("closed-recv", recvfrom(fd, &byte, 1, 0, 0, 0) == -1 && *__errno() == 9);
    return 0;
}
static int owned_fd = -1;
static int is_server;
static int fault;
int lan_start(int server, int mutation) {
    is_server = server; fault = mutation;
    struct addrinfo hint = {1, 2, 2, 17, 0, 0, 0, 0}, *answer = 0;
    CHECK("passive-resolve", getaddrinfo(0, server ? "7891" : "7890", &hint, &answer) == 0);
    CHECK("resolver-layout", answer && !answer->next && answer->flags == 1 &&
          answer->family == 2 && answer->type == 2 && answer->protocol == 17 && answer->length == 16);
    struct address expected = {2, 0, 0, {0}};
    expected.port = swap(server ? 7891 : 7890);
    CHECK("passive-wildcard", equal(answer->address, &expected, 16));
    owned_fd = socket(answer->family, answer->type, answer->protocol);
    CHECK("socket-created", owned_fd >= 3 && owned_fd < 1024);
    /* Deliberate isolation narrowing: verify AI_PASSIVE wildcard first, then
     * bind only 127.0.0.1:7891. Never bind wildcard or broadcast in this fixture. */
    struct address local = *answer->address;
    freeaddrinfo(answer);
    local.ip = 0x0100007fu;
    if (server) {
        int rc = bind(owned_fd, &local, 16);
        if (rc == -1 && (*__errno() == 98 || *__errno() == 13)) {
            lan_event("BLOCKED_PORT", 7891, *__errno());
            close(owned_fd); owned_fd = -1; return 77;
        }
        CHECK("server-bind", rc == 0);
    } else {
        if (fault == 1) { /* mutation: host guard must stop explicit client bind before OS I/O */
            local.port = 0; bind(owned_fd, &local, 16);
        }
        int one = 1;
        CHECK("broadcast-option", setsockopt(owned_fd, 1, 6, &one, 4) == 0);
    }
    CHECK("immediate-select", ready(owned_fd, 0) == 0);
    lan_event("IMMEDIATE", owned_fd, 0);
    if (server) {
        int conflict = socket(2, 2, 17);
        CHECK("conflict-owned", conflict >= 3 && conflict != owned_fd);
        *__errno() = 0;
        CHECK("conflict-errno98", bind(conflict, &local, 16) == -1 && *__errno() == 98);
        CHECK("conflict-close", close_exact(conflict) == 0);
        lan_event("CONFLICT", 98, 0);
    }
    return 0;
}
static unsigned char pattern(int reply, int round, u32 i) {
    return (unsigned char)(0xa3u + (u32)reply * 61u + (u32)round * 19u + i * 7u);
}
static int receive(int fd, int reply, int round, struct address *source) {
    struct { u32 before; unsigned char bytes[10240]; u32 after; } data;
    struct { u32 before; struct address value; u32 after; } address;
    const u32 capacity = !reply && round == 1 ? 7u : 10240u;
    const int expected = !reply ? (round == 0 ? 32 : round == 1 ? 7 : 0) : 24;
    unsigned char short_prefix[4] = {0};
    fill(&data, 0xa5, sizeof data); fill(&address, 0xa5, sizeof address);
    u32 length = 16;
    CHECK("receive-ready", ready(fd, 1) == 1);
    if (!reply && round == 0) {
        /* Peek preserves the datagram while testing a short address output. */
        length = 4;
        CHECK("peek-prefix", recvfrom(fd, data.bytes, 7, 2, &address.value, &length) == 7);
        CHECK("short-address-length", length == 16);
        CHECK("short-address-prefix", address.value.family == 2 && address.value.port);
        for (u32 i = 0; i < 4; ++i) short_prefix[i] = ((unsigned char *)&address.value)[i];
        CHECK("short-address-bounds", uniform((unsigned char *)&address.value + 4, 0xa5, 12) &&
              address.before == 0xa5a5a5a5u && address.after == 0xa5a5a5a5u);
        CHECK("peek-buffer-bounds", data.before == 0xa5a5a5a5u && data.after == 0xa5a5a5a5u &&
              uniform(data.bytes + 7, 0xa5, 10240 - 7));
        for (u32 i = 0; i < 7; ++i) CHECK("peek-payload", data.bytes[i] == pattern(reply, round, i));
        fill(&data, 0xa5, sizeof data); fill(&address, 0xa5, sizeof address); length = 16;
        CHECK("peek-still-ready", ready(fd, 0) == 1);
    }
    int n = recvfrom(fd, data.bytes, capacity, 0, &address.value, &length);
    CHECK("receive-length", n == expected);
    CHECK("address-length16", length == 16);
    CHECK("address-clean", clean_address(&address.value));
    if (!reply && round == 0) CHECK("address-prefix-stable", equal(short_prefix, &address.value, 4));
    CHECK("address-bounds", address.before == 0xa5a5a5a5u && address.after == 0xa5a5a5a5u);
    CHECK("buffer-bounds", data.before == 0xa5a5a5a5u && data.after == 0xa5a5a5a5u &&
          uniform(data.bytes + expected, 0xa5, 10240u - (u32)expected));
    for (int i = 0; i < expected; ++i)
        CHECK("synthetic-payload", data.bytes[i] == pattern(reply, round, (u32)i));
    *source = address.value;
    CHECK("receive-drained", ready(fd, 0) == 0);
    return 0;
}
int lan_exchange(void) {
    struct address server = endpoint(7891), first_peer = {0};
    unsigned short client_port = 0;
    for (int round = 0; round < 3; ++round) {
        unsigned char bytes[33];
        int length = is_server ? 24 : round == 0 ? 32 : round == 1 ? 33 : 0;
        for (u32 i = 0; i < sizeof bytes; ++i) bytes[i] = pattern(is_server, round, i);
        if (!is_server) {
            CHECK("client-send", sendto(owned_fd, bytes, (u32)length, 0, &server, 16) == length);
            struct address local; u32 size = 16; fill(&local, 0xa5, 16);
            CHECK("implicit-port", getsockname(owned_fd, &local, &size) == 0 && size == 16 &&
                  local.family == 2 && local.port && local.port != server.port &&
                  (local.ip == 0 || local.ip == server.ip) && uniform(local.zero, 0, 8));
            if (!round) { client_port = local.port; lan_event("CLIENT_PORT", swap(client_port), 0); }
            CHECK("stable-client-port", client_port == local.port);
            /* Explicit missing-reply assertion, separate from recv timeout. */
            CHECK("reply-ready", ready(owned_fd, 1) == 1);
        }
        struct address source;
        CHECK("receive-contract", receive(owned_fd, !is_server, round, &source) == 0);
        if (is_server) {
            CHECK("independent-endpoints", source.port != server.port);
            if (!round) { first_peer = source; lan_event("PEER_PORT", swap(source.port), 0); }
            CHECK("stable-peer16", equal(&first_peer, &source, 16));
            if (fault == 2) source.port = 0; /* stopped by host endpoint guard */
            CHECK("server-reply", sendto(owned_fd, bytes, (u32)length, 0, &source, 16) == length);
            if (fault == 3) break; /* omitted reply lane exits without waiting for another request */
        } else CHECK("reply-endpoint16", equal(&source, &server, 16));
        lan_event("ROUND", round, length);
    }
    /* These invalid arguments fail before touching OS receive/send storage. */
    unsigned char byte = 0;
    CHECK("null-send", sendto(owned_fd, 0, 1, 0, &server, 16) == -1 && *__errno() == 14);
    CHECK("oversize-send", sendto(owned_fd, &byte, 0x80000000u, 0, &server, 16) == -1 && *__errno() == 22);
    CHECK("null-recv", recvfrom(owned_fd, 0, 1, 0, 0, 0) == -1 && *__errno() == 14);
    CHECK("oversize-recv", recvfrom(owned_fd, &byte, 0x80000000u, 0, 0, 0) == -1 && *__errno() == 22);
    CHECK("missing-addrlen", recvfrom(owned_fd, &byte, 1, 0, &server, 0) == -1 && *__errno() == 14);
    int previous = owned_fd;
    CHECK("final-close", close_exact(owned_fd) == 0); owned_fd = -1;
    /* No stale endpoint/serial may survive descriptor-slot reuse. */
    int replacement = socket(2, 2, 17);
    CHECK("replacement-socket", replacement == previous);
    CHECK("replacement-close", close_exact(replacement) == 0);
    lan_event("CLOSED", 0, 0);
    return 0;
}
