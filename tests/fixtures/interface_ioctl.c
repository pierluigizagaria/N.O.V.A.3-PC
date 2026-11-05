/* Synthetic Android i386 ELF consumer; contains no original game code. */
typedef unsigned int u32;
typedef unsigned char u8;
struct ifconf32 { int length; u32 buffer; };
struct ifreq32 { char name[16]; u8 value[16]; };
_Static_assert(sizeof(void *) == 4, "guest pointer width");
_Static_assert(sizeof(struct ifconf32) == 8, "ifconf8");
_Static_assert(sizeof(struct ifreq32) == 32, "ifreq32");
extern int ioctl(int, u32, ...);
extern int close(int);
extern int *__errno(void);
extern void fixture_provider(int);
extern void ioctl_failure(const char *, int, int);
#define REQUIRE(x, tag) do { if (!(x)) { ioctl_failure(tag, __LINE__, *__errno()); return 1; } } while (0)
#define ERROR(call, err, tag) do { *__errno() = 0; REQUIRE((call) == -1 && *__errno() == (err), tag); } while (0)
static void fill(void *p, int n, u8 value) { u8 *b = p; for (int i = 0; i < n; ++i) b[i] = value; }
static int equal(const void *a, const void *b, int n) {
    const u8 *x = a, *y = b; for (int i = 0; i < n; ++i) if (x[i] != y[i]) return 0; return 1;
}
static void name(struct ifreq32 *r, const char *s) {
    fill(r->name, 16, 0); for (int i = 0; s[i] && i < 15; ++i) r->name[i] = s[i];
}
static int getflags(int fd, const char *n, unsigned expected) {
    u8 guarded[64]; struct ifreq32 *r = (struct ifreq32 *)(guarded + 16);
    fill(guarded, 64, 0xa7); name(r, n);
    REQUIRE(ioctl(fd, 0x8913u, r) == 0, "flags-call");
    REQUIRE((unsigned)(r->value[0] | (r->value[1] << 8)) == expected, "flags-value");
    for (int i = 2; i < 16; ++i) REQUIRE(r->value[i] == 0xa7, "flags-short-only");
    for (int i = 0; n[i]; ++i) REQUIRE(r->name[i] == n[i], "flags-name-retained");
    REQUIRE(r->name[15] == 0, "flags-name-bound");
    for (int i = 0; i < 16; ++i)
        REQUIRE(guarded[i] == 0xa7 && guarded[48 + i] == 0xa7, "ifreq32-guards");
    return 0;
}
static int records(int fd) {
    static const int capacities[] = {0, 1, 31, 32, 33, 63, 64, 159, 160, 191, 4000};
    static const u8 ips[5][4] = {{10,20,30,1},{10,20,30,2},{192,0,2,4},{127,0,0,1},{198,51,100,9}};
    static const char ids[5] = {'1','1','2','3','5'};
    u8 guarded[4032];
    struct ifconf32 c;
    for (unsigned trial = 0; trial < sizeof capacities / sizeof capacities[0]; ++trial) {
        int cap = capacities[trial], count = cap / 32;
        if (count > 5) count = 5;
        fill(guarded, sizeof guarded, 0xa5);
        c.length = cap; c.buffer = (u32)(guarded + 16);
        REQUIRE(ioctl(fd, 0x8912u, &c) == 0, "enumerate-call");
        REQUIRE(c.length == count * 32 && c.buffer == (u32)(guarded + 16), "enumerate-length-pointer");
        for (int i = 0; i < 16; ++i) REQUIRE(guarded[i] == 0xa5, "prefix-guard");
        for (int i = 16 + count * 32; i < (int)sizeof guarded; ++i)
            REQUIRE(guarded[i] == 0xa5, "suffix-and-unused-capacity");
        for (int j = 0; j < count; ++j) {
            const u8 *r = guarded + 16 + j * 32;
            REQUIRE(r[0] == 'n' && r[1] == ids[j], "record-order-name");
            for (int i = 2; i < 16; ++i) REQUIRE(r[i] == 0, "name-padding");
            REQUIRE(r[16] == 2 && r[17] == 0 && r[18] == 0 && r[19] == 0, "sockaddr-family-port");
            REQUIRE(equal(r + 20, ips[j], 4), "sockaddr-network-bytes");
            for (int i = 24; i < 32; ++i) REQUIRE(r[i] == 0, "sockaddr-padding");
        }
    }
    return 0;
}
/* Reproduces only the documented consumer rule, not original GetLocalIP. */
static int consumer(int fd) {
    struct ifreq32 rows[125]; struct ifconf32 c = {4000, (u32)rows};
    u8 selected[4] = {0,0,0,0};
    REQUIRE(ioctl(fd, 0x8912u, &c) == 0 && c.length == 160, "consumer-enumerate");
    for (int pos = 0; pos < c.length; pos += 32) {
        struct ifreq32 *r = (struct ifreq32 *)((u8 *)rows + pos), f;
        fill(&f, 32, 0); name(&f, r->name);
        for (int i = 0; i < 16 && f.name[i]; ++i) if (f.name[i] == ':') { f.name[i] = 0; break; }
        REQUIRE(ioctl(fd, 0x8913u, &f) == 0, "consumer-flags");
        static const u8 loopback[4] = {127,0,0,1};
        if (r->value[0] == 2 && r->value[1] == 0 && (f.value[0] & 1) && !equal(r->value + 4, loopback, 4))
            for (int i = 0; i < 4; ++i) selected[i] = r->value[4 + i];
    }
    static const u8 last[4] = {198,51,100,9};
    REQUIRE(equal(selected, last, 4), "consumer-last-qualifying");
    return 0;
}
int interface_ioctl_test(int fd, int regular, int stale) {
    struct ifconf32 c; struct ifreq32 r;
    struct { u32 before; struct ifconf32 query; u32 after; } boxed = {0x1234abcd, {0, 0}, 0x7890abcd};
    fixture_provider(0);
    REQUIRE(ioctl(fd, 0x8912u, &boxed.query) == 0, "count-only");
    REQUIRE(boxed.query.length == 160 && boxed.query.buffer == 0, "count-length");
    REQUIRE(boxed.before == 0x1234abcd && boxed.after == 0x7890abcd, "ifconf8-guards");
    c = boxed.query;
    c.length = -1;
    REQUIRE(ioctl(fd, 0x8912u, &c) == 0 && c.length == 160, "count-ignores-input-length");
    REQUIRE(records(fd) == 0, "records");
    REQUIRE(getflags(fd, "n1", 0x1043) == 0, "up-broadcast-multicast");
    REQUIRE(getflags(fd, "n1:99", 0x1043) == 0, "alias-base-lookup");
    REQUIRE(getflags(fd, "n2", 0x0010) == 0, "down-point-to-point");
    REQUIRE(getflags(fd, "n3", 0x0049) == 0, "up-loopback");
    REQUIRE(getflags(fd, "n4", 0x1003) == 0, "admin-up-link-down-without-ip");
    REQUIRE(getflags(fd, "n5", 0x0043) == 0, "up-no-multicast");
    REQUIRE(consumer(fd) == 0, "documented-consumer");
    fill(&r, 32, 0x5a); name(&r, "missing");
    ERROR(ioctl(fd, 0x8913u, &r), 19, "unknown-interface");
    REQUIRE(r.value[0] == 0x5a && r.name[15] == 0, "flags-error-no-write");
    fill(&r, 32, 'x');
    ERROR(ioctl(fd, 0x8913u, &r), 19, "unterminated-name");
    REQUIRE(r.name[15] == 'x', "unknown-name-no-write");
    ERROR(ioctl(-1, 0x8912u, &c), 9, "invalid-descriptor");
    ERROR(ioctl(stale, 0x8913u, &r), 9, "closed-descriptor");
    ERROR(ioctl(regular, 0x8912u, &c), 25, "non-socket");
    ERROR(ioctl(regular, 0x8913u, (void *)0), 25, "non-socket-before-pointer");
    ERROR(ioctl(fd, 0x8912u, (void *)0), 14, "null-ifconf");
    ERROR(ioctl(fd, 0x8913u, (void *)0), 14, "null-ifreq");
    ERROR(ioctl(fd, 0x8912u, (void *)1), 14, "unmapped-ifconf");
    ERROR(ioctl(fd, 0x8913u, (void *)1), 14, "unmapped-ifreq");
    c.length = 32; c.buffer = 1;
    ERROR(ioctl(fd, 0x8912u, &c), 14, "unmapped-record-buffer");
    REQUIRE(c.length == 32 && c.buffer == 1, "fault-length-unchanged");
    c.length = 31; c.buffer = 1;
    REQUIRE(ioctl(fd, 0x8912u, &c) == 0 && c.length == 0, "short-capacity-does-not-dereference");
    c.length = -1; c.buffer = (u32)&r;
    ERROR(ioctl(fd, 0x8912u, &c), 22, "negative-capacity");
    ERROR(ioctl(fd, 0xf00du), 25, "unsupported-no-vararg");
    fixture_provider(1);
    fill(&r, 32, 0x5a); c.length = 32; c.buffer = (u32)&r;
    REQUIRE(ioctl(fd, 0x8912u, &c) == 0 && c.length == 0, "empty-snapshot");
    for (int i = 0; i < 32; ++i) REQUIRE(((u8 *)&r)[i] == 0x5a, "empty-no-record-write");
    name(&r, "n1"); ERROR(ioctl(fd, 0x8913u, &r), 19, "empty-unknown-name");
    static const int modes[] = {2,3,4,5,7,8,10,11,12,14};
    static const int errors[] = {12,13,95,5,11,11,5,12,105,22};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        fixture_provider(modes[i]); c.length = 0x1234; c.buffer = 0;
        ERROR(ioctl(fd, 0x8912u, &c), errors[i], "provider-error");
        REQUIRE(c.length == 0x1234 && c.buffer == 0, "provider-error-no-length-write");
        fixture_provider(modes[i]); fill(&r, 32, 0x5a); name(&r, "n1");
        ERROR(ioctl(fd, 0x8913u, &r), errors[i], "flags-provider-error");
        REQUIRE(r.value[0] == 0x5a && r.value[1] == 0x5a, "provider-error-no-flags-write");
    }
    fixture_provider(6); c.length = 0; c.buffer = 0;
    REQUIRE(ioctl(fd, 0x8912u, &c) == 0 && c.length == 160, "provider-resize-retry");
    fixture_provider(9);
    REQUIRE(getflags(fd, "n1", 0x1002) == 0, "fresh-flags-after-change");
    fixture_provider(15);
    REQUIRE(getflags(fd, "n1", 0x1002) == 0, "admin-down-oper-up-not-running");
    fixture_provider(0);
    REQUIRE(getflags(fd, "n1", 0x1043) == 0, "stable-name-after-change");
    fixture_provider(13);
    REQUIRE(getflags(fd, "n3w5e11264sgsf", 0x0043) == 0, "full-luid-name");
    REQUIRE(getflags(fd, "n3w5e11264sgsf:", 0x0043) == 0, "full-luid-alias");
    REQUIRE(close(fd) == 0 && close(regular) == 0, "descriptor-close");
    ERROR(ioctl(fd, 0x8912u, &c), 9, "closed-after-success");
    return 0;
}
