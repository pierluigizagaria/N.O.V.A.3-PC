/* Synthetic ELF32 fixture: Android layouts and calls, no game content. */
typedef unsigned int u32;
typedef long long i64;
extern int *__errno(void);
extern int socket(int, int, int);
extern int bind(int, const void *, u32);
extern int listen(int, int);
extern int accept(int, void *, u32 *);
extern int connect(int, const void *, u32);
extern int send(int, const void *, u32, int);
extern int recv(int, void *, u32, int);
extern int sendto(int, const void *, u32, int, const void *, u32);
extern int recvfrom(int, void *, u32, int, void *, u32 *);
extern int shutdown(int, int);
extern int setsockopt(int, int, int, const void *, u32);
extern int getsockopt(int, int, int, void *, u32 *);
extern int getsockname(int, void *, u32 *);
extern int getpeername(int, void *, u32 *);
extern int close(int);
extern int read(int, void *, u32);
extern int write(int, const void *, u32);
extern int fcntl(int, int, ...);
extern int ioctl(int, u32, ...);
extern int open(const char *, int, ...);
extern int lseek(int, int, int);
extern int strcmp(const char *, const char *);
extern u32 strlen(const char *);
extern void *memset(void *, int, u32);
extern int printf(const char *, ...);
extern int usleep(u32);
extern u32 inet_addr(const char *);
extern char *inet_ntoa(u32);
extern int inet_pton(int, const char *, void *);
extern const char *inet_ntop(int, const void *, char *, u32);
extern int gethostname(char *, u32);
extern int *__get_h_errno(void);
struct address {
    unsigned short family, port;
    u32 ip;
    char zero[8];
};
struct hostent {
    char *name;
    char **aliases;
    int family, length;
    char **addresses;
};
struct addrinfo {
    int flags, family, type, protocol;
    u32 length;
    char *canonical;
    void *address;
    struct addrinfo *next;
};
extern struct hostent *gethostbyname(const char *);
extern struct hostent *gethostbyaddr(const void *, u32, int);
extern int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
extern void freeaddrinfo(struct addrinfo *);
struct timeval {
    int seconds, micros;
};
struct pollfd {
    int fd;
    short events, returned;
};
extern int poll(struct pollfd *, u32, int);
extern int select(int, u32 *, u32 *, u32 *, struct timeval *);
#define CHECK(e)                                                                                           \
    do {                                                                                                   \
        if (!(e)) {                                                                                        \
            printf("FAIL platform.c:%d errno=%d\n", __LINE__, *__errno());                                 \
            return __LINE__;                                                                               \
        }                                                                                                  \
    } while (0)
#define BASE "/data/data/com.gameloft.android.ANMP.GloftN3HM/files/"
static unsigned short swap(unsigned short n) { return (unsigned short)((n << 8) | (n >> 8)); }
static struct address loopback(unsigned short port) {
    struct address a = {2, 0, 0, {0}};
    a.port = swap(port);
    a.ip = inet_addr("127.0.0.1");
    return a;
}
static int wait_fd(int fd, short events) {
    struct pollfd p = {fd, events, 0};
    return poll(&p, 1, 3000) == 1 && (p.returned & events);
}
static int timeout_socket(int fd) {
    struct timeval t = {2, 0};
    return setsockopt(fd, 1, 20, &t, 8);
}
static int listener(struct address *a) {
    int s = socket(2, 1, 0);
    if (s < 0)
        return -1;
    int one = 1;
    if (setsockopt(s, 1, 2, &one, 4) || bind(s, a, 16) || listen(s, 4))
        return -1;
    u32 length = 16;
    if (getsockname(s, a, &length) || length != 16)
        return -1;
    return s;
}
int fixture_tcp(void) {
    for (int asynchronous = 0; asynchronous < 2; ++asynchronous) {
        struct address address = loopback(0);
        int server = listener(&address);
        CHECK(server >= 3 && address.port);
        int client = socket(2, 1, 0);
        CHECK(client >= 3 && client != server);
        if (asynchronous)
            CHECK(fcntl(client, 4, 2048) == 0 && (fcntl(client, 3) & 2048));
        int rc = connect(client, &address, 16);
        CHECK(rc == 0 || (asynchronous && rc == -1 && *__errno() == 115));
        CHECK(wait_fd(client, 4));
        int error = -1;
        u32 size = 4;
        CHECK(getsockopt(client, 1, 4, &error, &size) == 0 && error == 0 && size == 4);
        CHECK(wait_fd(server, 1));
        struct address peer = {0};
        size = 16;
        CHECK(fcntl(server, 4, 2048) == 0);
        int accepted = accept(server, &peer, &size);
        CHECK(accepted >= 3 && size == 16 && peer.family == 2 && peer.ip == address.ip);
        CHECK(!(fcntl(accepted, 3) & 2048));
        CHECK(timeout_socket(accepted) == 0);
        size = 16;
        CHECK(getpeername(client, &peer, &size) == 0 && peer.port == address.port);
        char data[32] = {0};
        CHECK(send(client, "hello", 5, 0) == 5);
        CHECK(wait_fd(accepted, 1));
        int pending = 0;
        CHECK(ioctl(accepted, 0x541b, &pending) == 0 && pending == 5);
        CHECK(read(accepted, data, 5) == 5 && !strcmp(data, "hello"));
        CHECK(write(accepted, "world", 5) == 5 && wait_fd(client, 1));
        memset(data, 0, sizeof data);
        CHECK(recv(client, data, 5, 2) == 5 && !strcmp(data, "world"));
        CHECK(recv(client, data, 5, 0) == 5);
        CHECK(send(client, "!", 1, 1) == 1);
        struct pollfd urgent = {accepted, 2, 0};
        CHECK(poll(&urgent, 1, 3000) == 1 && (urgent.returned & 2));
        u32 exceptional[32] = {0};
        exceptional[accepted / 32] |= 1u << (accepted % 32);
        struct timeval instant = {0, 0};
        CHECK(select(accepted + 1, 0, 0, exceptional, &instant) == 1);
        CHECK(recv(accepted, data, 1, 1) == 1 && data[0] == '!');
        int nonblock = 1;
        CHECK(ioctl(accepted, 0x5421, &nonblock) == 0 && (fcntl(accepted, 3) & 2048));
        CHECK(recv(accepted, data, 1, 0) == -1 && *__errno() == 11);
        CHECK(accept(server, 0, 0) == -1 && *__errno() == 11);
        CHECK(shutdown(client, 1) == 0 && wait_fd(accepted, 1));
        CHECK(recv(accepted, data, 1, 0) == 0);
        CHECK(close(accepted) == 0 && close(client) == 0 && close(server) == 0);
    }
    return 0;
}
int fixture_udp(void) {
    struct address address = loopback(0);
    int receiver = socket(2, 2, 0), sender = socket(2, 2 | 2048, 0);
    CHECK(receiver >= 3 && sender >= 3);
    CHECK(bind(receiver, &address, 16) == 0 && timeout_socket(receiver) == 0);
    u32 size = 16;
    CHECK(getsockname(receiver, &address, &size) == 0);
    int one = 1, got = 0;
    CHECK(setsockopt(sender, 1, 6, &one, 4) == 0);
    size = 4;
    CHECK(getsockopt(sender, 1, 6, &got, &size) == 0 && got);
    CHECK(sendto(sender, "udp-payload", 11, 0, &address, 16) == 11 && wait_fd(receiver, 1));
    char bytes[32] = {0};
    struct address source;
    size = 16;
    CHECK(recvfrom(receiver, bytes, 32, 0, &source, &size) == 11 && !strcmp(bytes, "udp-payload"));
    CHECK(size == 16 && source.ip == inet_addr("127.0.0.1") && source.port);
    CHECK(sendto(receiver, "reply", 5, 0, &source, 16) == 5 && wait_fd(sender, 1));
    CHECK(recvfrom(sender, bytes, 32, 0, 0, 0) == 5);
    CHECK(connect(sender, &address, 16) == 0 && write(sender, "connected", 9) == 9 && wait_fd(receiver, 1));
    CHECK(read(receiver, bytes, 3) == 3 && bytes[0] == 'c' && bytes[2] == 'n');
    CHECK(send(sender, "", 0, 0) == 0 && wait_fd(receiver, 1));
    CHECK(recv(receiver, bytes, 32, 0) == 0);
    CHECK(recv(sender, bytes, 32, 0) == -1 && *__errno() == 11);
    struct timeval short_timeout = {0, 25000};
    CHECK(setsockopt(receiver, 1, 20, &short_timeout, 8) == 0);
    CHECK(recv(receiver, bytes, 32, 0) == -1 && *__errno() == 11);
    CHECK(close(sender) == 0 && close(receiver) == 0);
    return 0;
}
int fixture_waits(void) {
    int file = open(BASE "ready", 2 | 64 | 512, 0600);
    CHECK(file >= 3);
    struct address a = loopback(0);
    int s = socket(2, 2, 0);
    CHECK(s >= 3 && s != file && bind(s, &a, 16) == 0);
    u32 length = 16;
    CHECK(getsockname(s, &a, &length) == 0);
    int tx = socket(2, 2, 0);
    CHECK(tx >= 3);
    struct pollfd fds[4] = {{file, 1 | 4, 0}, {s, 1, 0}, {-1, 1, 0}, {31000, 1, 0}};
    CHECK(poll(fds, 4, 0) == 2 && fds[0].returned == 5 && fds[1].returned == 0 && fds[2].returned == 0 &&
          fds[3].returned == 32);
    CHECK(sendto(tx, "x", 1, 0, &a, 16) == 1 && wait_fd(s, 1));
    u32 rd[32] = {0}, wr[32] = {0};
    rd[file / 32] |= 1u << (file % 32);
    rd[s / 32] |= 1u << (s % 32);
    wr[file / 32] |= 1u << (file % 32);
    struct timeval t = {1, 0};
    int n = s > file ? s : file;
    CHECK(select(n + 1, rd, wr, 0, &t) == 3);
    CHECK((rd[s / 32] & (1u << (s % 32))) && (rd[file / 32] & (1u << (file % 32))) &&
          (wr[file / 32] & (1u << (file % 32))));
    CHECK(poll(fds, 2, 0) == 2 && (fds[1].returned & 1));
    char c;
    CHECK(read(s, &c, 1) == 1 && c == 'x');
    rd[0] = 1u << 31;
    t.seconds = 0;
    t.micros = 0;
    CHECK(select(32, rd, 0, 0, &t) == -1 && *__errno() == 9);
    CHECK(poll(0, 0, 5) == 0);
    CHECK(select(0, 0, 0, 0, &t) == 0);
    CHECK(write(file, "abc", 3) == 3 && lseek(file, 1, 0) == 1);
    int pending = 0;
    CHECK(ioctl(file, 0x541b, &pending) == 0 && pending == 2);
    CHECK(lseek(s, 0, 0) == -1 && *__errno() == 29);
    CHECK(send(file, "x", 1, 0) == -1 && *__errno() == 88);
    CHECK(close(s) == 0);
    int replacement = open(BASE "reuse", 2 | 64, 0600);
    CHECK(replacement == s);
    CHECK(close(file) == 0 && close(replacement) == 0 && close(tx) == 0);
    return 0;
}
int fixture_errors(void) {
    CHECK(socket(1, 1, 0) == -1 && *__errno() == 97);
    CHECK(socket(2, 3, 0) == -1 && *__errno() == 94);
    CHECK(socket(2, 1, 17) == -1 && *__errno() == 93);
    int s = socket(2, 1, 0);
    CHECK(s >= 3);
    int value = 1;
    CHECK(setsockopt(s, 1, 12345, &value, 4) == -1 && *__errno() == 92);
    CHECK(bind(s, 0, 0) == -1 && *__errno() == 14);
    struct address a = loopback(0);
    CHECK(bind(s, &a, 1) == -1 && *__errno() == 22);
    CHECK(shutdown(s, 3) == -1 && *__errno() == 22);
    CHECK(fcntl(s, 4, 2048) == 0);
    CHECK(recv(s, &value, 4, 0) == -1 && *__errno() == 107);
    int one = 1;
    CHECK(setsockopt(s, 6, 1, &one, 4) == 0);
    u32 length = 4;
    value = 0;
    CHECK(getsockopt(s, 6, 1, &value, &length) == 0 && value == 1);
    struct timeval timeout = {0, 25000}, back = {0};
    CHECK(setsockopt(s, 1, 20, &timeout, 8) == 0);
    length = 8;
    CHECK(getsockopt(s, 1, 20, &back, &length) == 0 && length == 8 && back.micros == 25000);
    int linger[2] = {1, 2}, linger_back[2] = {0};
    CHECK(setsockopt(s, 1, 13, linger, 8) == 0);
    length = 8;
    CHECK(getsockopt(s, 1, 13, linger_back, &length) == 0 && linger_back[0] && linger_back[1] == 2);
    CHECK(close(s) == 0 && close(s) == -1 && *__errno() == 9);
    // A bound, non-listening local TCP port deterministically refuses connections.
    a = loopback(0);
    int reserved = socket(2, 1, 0);
    CHECK(reserved >= 3 && bind(reserved, &a, 16) == 0);
    length = 16;
    CHECK(getsockname(reserved, &a, &length) == 0);
    s = socket(2, 1, 0);
    CHECK(s >= 3);
    CHECK(connect(s, &a, 16) == -1 && *__errno() == 111);
    CHECK(close(s) == 0);
    s = socket(2, 1 | 2048, 0);
    CHECK(s >= 3);
    CHECK(connect(s, &a, 16) == -1 && *__errno() == 115);
    struct pollfd p = {s, 4, 0};
    CHECK(poll(&p, 1, 3000) == 1 && (p.returned & (4 | 8 | 16)));
    length = 4;
    value = 0;
    CHECK(getsockopt(s, 1, 4, &value, &length) == 0 && value == 111);
    CHECK(close(s) == 0 && close(reserved) == 0);
    return 0;
}
int fixture_resolver(void) {
    CHECK(sizeof(struct addrinfo) == 32 && __builtin_offsetof(struct addrinfo, canonical) == 20 &&
          __builtin_offsetof(struct addrinfo, address) == 24);
    CHECK(sizeof(struct hostent) == 20);
    const char *blocked[] = {"gllive.gameloft.com", "vgold.gameloft.com",     "valpha.gameloft.com",
                             "ets.gameloft.com",    "ingameads.gameloft.com", "confirmation.gameloft.com",
                             "unknown.invalid"};
    for (int i = 0; i < 7; ++i) {
        CHECK(!gethostbyname(blocked[i]) && *__get_h_errno() == 1);
        struct addrinfo *p = (void *)1;
        CHECK(getaddrinfo(blocked[i], "80", 0, &p) == 8 && !p);
    }
    struct hostent *h = gethostbyname("LoCaLhOsT.");
    CHECK(h && h->family == 2 && h->length == 4 && !h->aliases[0] && !h->addresses[1]);
    CHECK(*(u32 *)h->addresses[0] == inet_addr("127.0.0.1"));
    h = gethostbyname("community.test");
    CHECK(h && *(u32 *)h->addresses[0] == inet_addr("127.0.0.1"));
    struct addrinfo hint = {2, 2, 1, 6, 0, 0, 0, 0}, *p = 0;
    CHECK(getaddrinfo("community.test", "43210", &hint, &p) == 0 && p && !p->next && p->family == 2 &&
          p->type == 1 && p->protocol == 6);
    CHECK(p->length == 16 && !strcmp(p->canonical, "community.test"));
    struct address *a = p->address;
    CHECK(a->family == 2 && a->ip == inet_addr("127.0.0.1") && a->port == swap(43210));
    freeaddrinfo(p);
    CHECK(getaddrinfo("127.0.0.1", "80", 0, &p) == 0 && p && p->next && !p->next->next);
    freeaddrinfo(p);
    hint.flags = 4;
    CHECK(getaddrinfo("community.test", "80", &hint, &p) == 8 && !p);
    hint.flags = 1;
    CHECK(getaddrinfo(0, "0", &hint, &p) == 0 && ((struct address *)p->address)->ip == 0);
    freeaddrinfo(p);
    hint.family = 10;
    hint.flags = 0;
    CHECK(getaddrinfo("::1", "7", &hint, &p) == 0 && p->length == 28 && p->family == 10);
    freeaddrinfo(p);
    hint.family = 123;
    CHECK(getaddrinfo("localhost", "80", &hint, &p) == 5);
    CHECK(inet_addr("bad") == 0xffffffffu && !strcmp(inet_ntoa(inet_addr("127.0.0.1")), "127.0.0.1"));
    u32 ip = 0;
    char text[64];
    CHECK(inet_pton(2, "127.0.0.1", &ip) == 1 && inet_ntop(2, &ip, text, 64) && !strcmp(text, "127.0.0.1"));
    h = gethostbyaddr(&ip, 4, 2);
    CHECK(h && !strcmp(h->name, "127.0.0.1"));
    CHECK(gethostname(text, 64) == 0 && strlen(text) > 8);
    CHECK(gethostname(text, 2) == -1 && *__errno() == 36);
    return 0;
}
int fixture_broadcast(void) {
    // Only loopback broadcast: this test must never emit onto a physical LAN.
    int receiver = socket(2, 2, 0), sender = socket(2, 2, 0);
    CHECK(receiver >= 3 && sender >= 3);
    struct address a = loopback(0), b = loopback(0);
    a.ip = 0;
    CHECK(bind(receiver, &a, 16) == 0 && bind(sender, &b, 16) == 0);
    u32 length = 16;
    CHECK(getsockname(receiver, &a, &length) == 0);
    int one = 1;
    CHECK(setsockopt(sender, 1, 6, &one, 4) == 0);
    a.ip = inet_addr("127.255.255.255");
    int sent = sendto(sender, "loopback-broadcast", 18, 0, &a, 16);
    struct pollfd p = {receiver, 1, 0};
    int ready = sent == 18 ? poll(&p, 1, 300) : 0;
    if ((sent == 18 && ready == 0) ||
        (sent < 0 && (*__errno() == 99 || *__errno() == 100 || *__errno() == 101 || *__errno() == 95))) {
        printf("SKIP: host has no usable loopback broadcast interface (errno=%d)\n", *__errno());
        close(receiver);
        close(sender);
        return 77;
    }
    CHECK(sent == 18 && ready == 1);
    char data[32] = {0};
    CHECK(recv(receiver, data, 32, 0) == 18 && !strcmp(data, "loopback-broadcast"));
    CHECK(close(receiver) == 0 && close(sender) == 0);
    return 0;
}
u32 fixture_port;
int fixture_server_start(void) {
    struct address a = loopback(0);
    int s = listener(&a);
    fixture_port = swap(a.port);
    return s;
}
int fixture_server_exchange(int s) {
    CHECK(wait_fd(s, 1));
    int peer = accept(s, 0, 0);
    CHECK(peer >= 3 && timeout_socket(peer) == 0);
    char bytes[64] = {0};
    CHECK(recv(peer, bytes, 64, 0) == 15 && !strcmp(bytes, "second-profile!"));
    CHECK(send(peer, "first-profile!", 14, 0) == 14);
    CHECK(close(peer) == 0 && close(s) == 0);
    return 0;
}
int fixture_client_exchange(u32 port) {
    struct address a = loopback((unsigned short)port);
    int s = socket(2, 1, 0);
    CHECK(s >= 3 && timeout_socket(s) == 0 && connect(s, &a, 16) == 0);
    CHECK(write(s, "second-profile!", 15) == 15);
    char bytes[64] = {0};
    CHECK(read(s, bytes, 64) == 14 && !strcmp(bytes, "first-profile!"));
    CHECK(close(s) == 0);
    return 0;
}

union value {
    unsigned char z;
    int i;
    i64 j;
    float f;
    double d;
    void *l;
};
struct env {
    void **table;
};
#define J(e, index, type) ((type)((e)->table[index]))
static void *text(struct env *e, const char *s) {
    return J(e, 167, void *(*)(struct env *, const char *))(e, s);
}
static void *method(struct env *e, void *c, const char *name, const char *signature) {
    return J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(e, c, name, signature);
}
int fixture_profile(struct env *e, int phase) {
    void *c =
        J(e, 6, void *(*)(struct env *, const char *))(e, "com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
    void *get = method(e, c, "GetInt", "(Ljava/lang/String;I)I"),
         *put = method(e, c, "PutInt", "(Ljava/lang/String;I)V");
    void *key = text(e, "fixture-key");
    CHECK(J(e, 129, int (*)(struct env *, void *, void *, ...))(e, c, get, key, -9) == (phase ? 42 : -9));
    if (!phase)
        J(e, 141, void (*)(struct env *, void *, void *, ...))(e, c, put, key, 42);
    void *first = method(e, c, "IsFirstTimeLaunch", "(Ljava/lang/String;)Z"),
         *launch = text(e, "fixture-first");
    CHECK(J(e, 117, unsigned char (*)(struct env *, void *, void *, ...))(e, c, first, launch) ==
          (phase ? 0 : 1));
    CHECK(!J(e, 117, unsigned char (*)(struct env *, void *, void *, ...))(e, c, first, launch));
    void *day = method(e, c, "IsNewDay", "()Z");
    CHECK(J(e, 117, unsigned char (*)(struct env *, void *, void *, ...))(e, c, day) == (phase ? 0 : 1));
    void *lang = method(e, c, "GetPhoneLanguage", "()Ljava/lang/String;");
    void *language = J(e, 114, void *(*)(struct env *, void *, void *, ...))(e, c, lang);
    const char *utf = J(e, 169, const char *(*)(struct env *, void *, void *))(e, language, 0);
    CHECK(!strcmp(utf, "it"));
    void *setlang = method(e, c, "setGameLang", "(I)V"),
         *getlang = method(e, c, "getGameLang", "()Ljava/lang/String;");
    J(e, 141, void (*)(struct env *, void *, void *, ...))(e, c, setlang, 5);
    language = J(e, 114, void *(*)(struct env *, void *, void *, ...))(e, c, getlang);
    utf = J(e, 169, const char *(*)(struct env *, void *, void *))(e, language, 0);
    CHECK(!strcmp(utf, "jp"));
    void *ids[] = {method(e, c, "GetUDID", "(Z)Ljava/lang/String;"),
                   method(e, c, "getAndroidID", "()Ljava/lang/String;"),
                   method(e, c, "getIMEI", "()Ljava/lang/String;"),
                   method(e, c, "getSerial", "()Ljava/lang/String;")};
    for (int i = 0; i < 4; ++i) {
        void *id = J(e, 114, void *(*)(struct env *, void *, void *, ...))(e, c, ids[i], 0);
        CHECK(id && J(e, 168, int (*)(struct env *, void *))(e, id) >= 15);
    }
    void *policy = J(e, 6, void *(*)(struct env *, const char *))(
        e, "com/gameloft/android/ANMP/GloftN3HM/installer/GDRMPolicy");
    if (!phase) {
        void *update = method(e, policy, "UpdatePreferences", "(Ljava/lang/String;Ljava/lang/String;I)V");
        void *update2 = method(e, policy, "UpdatePreferences2", "(Ljava/lang/String;JI)V");
        J(e, 141, void (*)(struct env *, void *, void *, ...))(e, policy, update, text(e, "licensed"),
                                                               text(e, "123"), 1);
        J(e, 141, void (*)(struct env *, void *, void *, ...))(e, policy, update, text(e, "boolean"),
                                                               text(e, "0"), 6);
        J(e, 141, void (*)(struct env *, void *, void *, ...))(e, policy, update2, text(e, "long"),
                                                               0x123456789LL, 2);
    }
    return 0;
}
