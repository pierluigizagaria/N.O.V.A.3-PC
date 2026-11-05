typedef unsigned int u32;
typedef unsigned int size_t;
typedef long long i64;
typedef unsigned long long u64;
struct buffer {
    unsigned char *base;
    int size;
};
struct file {
    unsigned char *p;
    int r, w;
    short flags, fd;
    struct buffer bf;
    int lbfsize;
    void *cookie;
    void *close;
    void *read;
    void *seek;
    void *write;
    struct buffer ext;
    unsigned char *up;
    int ur;
    char ubuf[3], nbuf[1];
    struct buffer lb;
    int blksize, offset;
};
struct stat {
    u64 dev;
    char pad0[4];
    u32 ino32, mode, nlink, uid, gid;
    u64 rdev;
    char pad3[4];
    i64 size;
    u32 blksize;
    u64 blocks;
    u32 atime, an, mtime, mn, ctime, cn;
    u64 ino;
};
struct dirent {
    u64 ino;
    i64 off;
    unsigned short reclen;
    unsigned char type;
    char name[256];
};
struct attr {
    u32 flags, base, size, guard;
    int policy, priority;
};
struct timespec {
    int seconds, nanoseconds;
};
struct timeval {
    int seconds, micros;
};
struct tm {
    int sec, min, hour, mday, mon, year, wday, yday, isdst, gmtoff;
    const char *zone;
};
extern struct file __sF[];
extern const unsigned char *_ctype_;
extern const short *_tolower_tab_;
extern int *__errno(void);
extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern struct file *fopen(const char *, const char *);
extern struct file *fdopen(int, const char *);
extern struct file *freopen(const char *, const char *, struct file *);
extern struct file *tmpfile(void);
extern char *tmpnam(char *);
extern int fclose(struct file *);
extern int fflush(struct file *);
extern int fseek(struct file *, int, int);
extern int ftell(struct file *);
extern void rewind(struct file *);
extern int setvbuf(struct file *, char *, int, size_t);
extern int fputc(int, struct file *);
extern int fputs(const char *, struct file *);
extern int getc(struct file *);
extern int __srget(struct file *);
extern int __swbuf(int, struct file *);
extern int ungetc(int, struct file *);
extern char *fgets(char *, int, struct file *);
extern size_t fread(void *, size_t, size_t, struct file *);
extern size_t fwrite(const void *, size_t, size_t, struct file *);
extern int snprintf(char *, size_t, const char *, ...);
extern int sprintf(char *, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, __builtin_va_list);
extern int sscanf(const char *, const char *, ...);
extern int fscanf(struct file *, const char *, ...);
extern int fprintf(struct file *, const char *, ...);
extern int open(const char *, int, ...);
extern int close(int);
extern int read(int, void *, size_t);
extern int write(int, const void *, size_t);
extern int lseek(int, int, int);
extern int pread(int, void *, size_t, int);
extern int stat(const char *, struct stat *);
extern int fstat(int, struct stat *);
extern int mkdir(const char *, u32);
extern int unlink(const char *);
extern int rename(const char *, const char *);
extern void *opendir(const char *);
extern struct dirent *readdir(void *);
extern int closedir(void *);
extern int fcntl(int, int, ...);
extern int ioctl(int, int, ...);
extern char *basename(char *);
extern int pthread_create(u32 *, const struct attr *, void *(*)(void *), void *);
extern int pthread_join(u32, void **);
extern int pthread_detach(u32);
extern void pthread_exit(void *) __attribute__((noreturn));
extern u32 pthread_self(void);
extern int pthread_equal(u32, u32);
extern int pthread_kill(u32, int);
extern int pthread_attr_init(struct attr *);
extern int pthread_attr_setstacksize(struct attr *, u32);
extern int pthread_attr_setdetachstate(struct attr *, int);
extern int pthread_attr_destroy(struct attr *);
extern int pthread_key_create(u32 *, void (*)(void *));
extern int pthread_setspecific(u32, const void *);
extern void *pthread_getspecific(u32);
extern int pthread_key_delete(u32);
extern int pthread_mutex_init(u32 *, const u32 *);
extern int pthread_mutex_destroy(u32 *);
extern int pthread_mutex_lock(u32 *);
extern int pthread_mutex_unlock(u32 *);
extern int pthread_cond_init(u32 *, const u32 *);
extern int pthread_cond_wait(u32 *, u32 *);
extern int pthread_cond_timedwait(u32 *, u32 *, const struct timespec *);
extern int pthread_cond_signal(u32 *);
extern int pthread_cond_destroy(u32 *);
extern int sem_init(u32 *, int, u32);
extern int sem_wait(u32 *);
extern int sem_trywait(u32 *);
extern int sem_post(u32 *);
extern int sem_destroy(u32 *);
extern int usleep(u32);
extern int gettimeofday(struct timeval *, void *);
extern int setjmp(u32 *) __attribute__((returns_twice));
extern void longjmp(u32 *, int) __attribute__((noreturn));
extern void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
extern int aligned_compare(const void *, const void *);
extern void *aligned_thread(void *);
extern void aligned_destructor(void *);
extern void *mmap(void *, size_t, int, int, int, int);
extern int munmap(void *, size_t);
extern void *mremap(void *, size_t, size_t, int, ...);
extern int lrand48(void);
extern void srand48(int);
extern char *getenv(const char *);
extern int system(const char *);
extern int sysconf(int);
extern int getuid(void);
extern double sqrt(double);
extern double sin(double);
extern double pow(double, double);
extern struct tm *gmtime_r(const int *, struct tm *);
extern struct tm *localtime_r(const int *, struct tm *);
extern int mktime(struct tm *);
extern size_t strftime(char *, size_t, const char *, const struct tm *);
extern double difftime(int, int);
extern size_t mbrtowc(u32 *, const char *, size_t, void *);
extern size_t wcrtomb(char *, u32, void *);
extern size_t wcslen(const u32 *);
extern u32 *wmemmove(u32 *, const u32 *, size_t);
extern u32 wctype(const char *);
extern int iswctype(u32, u32);
struct signal_action {
    u32 handler, mask, flags, restorer;
};
extern int sigaction(int, const struct signal_action *, struct signal_action *);
extern u32 bsd_signal(int, u32);
extern int raise(int);
extern int __android_log_print(int, const char *, const char *, ...);
static volatile int signal_called;
static void recorded_signal(int sig) {
    (void)sig;
    ++signal_called;
}
#define CHECK(e)                                                                                             \
    do {                                                                                                     \
        if (!(e))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
#define BASE "/data/data/com.gameloft.android.ANMP.GloftN3HM/files/"
int fixture_tmpnam(void) {
    char first[1024], second[1024];
    CHECK(tmpnam(first) == first);
    CHECK(tmpnam(second) == second && strcmp(first, second) != 0);
    struct stat s;
    CHECK(stat(first, &s) == -1 && *__errno() == 2);
    struct file *f = fopen(first, "w+");
    CHECK(f && fputs("temporary name", f) >= 0 && fclose(f) == 0);
    CHECK(stat(first, &s) == 0 && s.size == 14);
    CHECK(unlink(first) == 0);
    CHECK(tmpnam((void *)0) != (void *)0);
    return 0;
}

int fixture_stdio(void) {
    CHECK(sizeof(struct file) == 84);
    CHECK(__sF[1].fd == 1 && __sF[2].fd == 2);
    CHECK((_ctype_[1 + 'A'] & 65) == 65);
    CHECK(_ctype_[1 + '0'] == 4);
    CHECK(_ctype_[1 + 128] == 32 && _ctype_[1 + 160] == 16);
    CHECK(_tolower_tab_[0] == -1 && _tolower_tab_[1 + 'A'] == 'a');
    struct file *f = tmpfile();
    CHECK(f);
    CHECK(fwrite("abcdef\n", 1, 7, f) == 7);
    CHECK(ftell(f) == 7);
    CHECK(fseek(f, 0, 0) == 0);
    CHECK((--f->r < 0 ? __srget(f) : *f->p++) == 'a');
    CHECK(f->r == 6);
    CHECK((--f->r < 0 ? __srget(f) : *f->p++) == 'b');
    CHECK(ftell(f) == 2);
    CHECK(ungetc('X', f) == 'X');
    CHECK(getc(f) == 'X');
    CHECK(getc(f) == 'c');
    CHECK(fseek(f, -1, 1) == 0);
    CHECK(getc(f) == 'c');
    char buf[32];
    CHECK(fgets(buf, sizeof buf, f) && strcmp(buf, "def\n") == 0);
    CHECK(getc(f) == -1 && (f->flags & 32));
    CHECK(ungetc('Q', f) == 'Q' && !(f->flags & 32));
    CHECK(getc(f) == 'Q');
    rewind(f);
    CHECK(getc(f) == 'a');
    CHECK(setvbuf(f, 0, 2, 0) == 0);
    CHECK(getc(f) == 'b');
    CHECK(fclose(f) == 0);
    f = fopen(BASE "stream", "w+");
    CHECK(f);
    char custom[4];
    CHECK(setvbuf(f, custom, 0, 4) == 0);
    CHECK(fputc('1', f) == '1');
    CHECK(--f->w >= 0);
    *f->p++ = '2';
    CHECK(fflush(f) == 0);
    CHECK(fseek(f, 0, 0) == 0);
    CHECK(fread(buf, 1, 2, f) == 2 && buf[0] == '1' && buf[1] == '2');
    CHECK(freopen(BASE "stream2", "w+", f) == f);
    CHECK(fputs("hello", f) >= 0);
    CHECK(fclose(f) == 0);
    int fd = open(BASE "stream2", 0);
    CHECK(fd >= 0);
    f = fdopen(fd, "r");
    CHECK(f && f->fd == fd);
    CHECK(fread(buf, 1, 5, f) == 5);
    CHECK(fclose(f) == 0);
    CHECK(read(fd, buf, 1) == -1 && *__errno() == 9);
    fd = open(BASE "stream2", 2);
    CHECK(fd >= 0);
    f = fdopen(fd, "a");
    CHECK(f && fputc('!', f) == '!' && fclose(f) == 0);
    fd = open(BASE "stream2", 0);
    CHECK(fd >= 0 && read(fd, buf, 6) == 6 && buf[5] == '!');
    CHECK(close(fd) == 0);
    CHECK(close(0) == 0 && read(0, buf, 1) == -1 && *__errno() == 9);
    fd = open(BASE "stream2", 0);
    CHECK(fd == 0 && close(fd) == 0);
    return 0;
}
static int vfmt(char *p, size_t n, const char *fmt, ...) {
    __builtin_va_list a;
    __builtin_va_start(a, fmt);
    int result = vsnprintf(p, n, fmt, a);
    __builtin_va_end(a);
    return result;
}
int fixture_format(void) {
    char b[256];
    CHECK(sizeof(long double) == 8);
    long double ld = 0;
    CHECK(snprintf(b, sizeof b, "%.2Lf", 1.25L) == 4 && strcmp(b, "1.25") == 0);
    CHECK(sscanf("2.5", "%Lf", &ld) == 1 && ld == 2.5L);
    CHECK(sscanf("0x1.8p+2", "%La", &ld) == 1 && ld == 6.0L);
    CHECK(snprintf(b, sizeof b, "%2$d %1$s %3$.*4$f", "value", 17, 1.25, 2) == 13 &&
          strcmp(b, "17 value 1.25") == 0);
    int count = -1;
    u32 wide[] = {'a', 'b', 'c', 0};
    CHECK(snprintf(b, sizeof b, "%+06d %#x %.0u %lld", -42, 0x2a, 0, 1234567890123LL) == 26);
    CHECK(strcmp(b, "-00042 0x2a  1234567890123") == 0);
    CHECK(snprintf(b, 5, "abcdef") == 6 && strcmp(b, "abcd") == 0);
    CHECK(snprintf(0, 0, "%s %d", "ab", 12) == 5);
    CHECK(vfmt(b, sizeof b, "%*.*f %n", 8, 2, 1.25, &count) == 9 && count == 9 &&
          strcmp(b, "    1.25 ") == 0);
    CHECK(sprintf(b, "%ls:%lc", wide, (u32)'Z') == 5 && strcmp(b, "abc:Z") == 0);
    CHECK(snprintf(b, sizeof b, "%.2e|%#.3g|%a", 12.5, 1.2, 1.5) > 0);
    CHECK(strcmp(b, "1.25e+01|1.20|0x1.8p+0") == 0);
    int i = 0, n = -1;
    unsigned x = 0;
    long long ll = 0;
    double d = 0;
    char word[16];
    CHECK(sscanf(" -17 0x2a 1234567890123 2.5 word!", "%d %x %lld %lf %4s%n", &i, &x, &ll, &d, word, &n) ==
          5);
    CHECK(i == -17 && x == 42 && ll == 1234567890123LL && d == 2.5 && strcmp(word, "word") == 0 && n == 32);
    CHECK(sscanf("abc123", "%[a-z]%d", word, &i) == 2 && strcmp(word, "abc") == 0 && i == 123);
    CHECK(sscanf("", "%d", &i) == -1);
    CHECK(sscanf("x", "%d", &i) == 0);
    u32 wout[8];
    CHECK(sscanf("hi Q", "%ls %lc", wout, &x) == 2 && wout[0] == 'h' && wout[1] == 'i' && wout[2] == 0 &&
          x == 'Q');
    struct file *f = tmpfile();
    CHECK(f);
    CHECK(fprintf(f, "%d\n%s", 25, "end") == 6);
    rewind(f);
    CHECK(fscanf(f, "%d %3s", &i, word) == 2 && i == 25 && strcmp(word, "end") == 0);
    CHECK(fclose(f) == 0);
    return 0;
}
int fixture_files(void) {
    struct stat s;
    char b[8];
    int fd = open(BASE "bytes", 2 | 64 | 512, 0600);
    CHECK(fd >= 3);
    CHECK(write(fd, "hello", 5) == 5);
    CHECK(lseek(fd, 1, 0) == 1);
    CHECK(pread(fd, b, 2, 3) == 2 && b[0] == 'l' && b[1] == 'o');
    CHECK(lseek(fd, 0, 1) == 1);
    CHECK(fstat(fd, &s) == 0 && sizeof(s) == 96 && s.size == 5 && s.blksize == 4096 &&
          (s.mode & 0170000) == 0100000);
    CHECK(fcntl(fd, 3) == 2);
    CHECK(fcntl(fd, 999) == -1 && *__errno() == 22);
    CHECK(ioctl(fd, 999) == -1 && *__errno() == 25);
    CHECK(close(fd) == 0);
    CHECK(stat(BASE "bytes", &s) == 0 && s.size == 5 && s.ino);
    fd = open("." BASE "bytes", 0);
    CHECK(fd >= 3 && read(fd, b, 5) == 5 && b[0] == 'h' && b[4] == 'o');
    CHECK(close(fd) == 0);
    CHECK(open("/etc/passwd", 0) == -1 && *__errno() == 2);
    CHECK(open(BASE "../escape", 2 | 64) == -1 && *__errno() == 2);
    CHECK(open(BASE "name:stream", 2 | 64) == -1 && *__errno() == 2);
    CHECK(open(BASE "NUL", 0) == -1 && *__errno() == 2);
    CHECK(open("/sdcard/Android/obb/com.gameloft.android.ANMP.GloftN3HM/x", 1 | 64) == -1 &&
          *__errno() == 30);
    CHECK(rename(BASE "bytes", BASE "renamed") == 0);
    void *dir = opendir(BASE);
    CHECK(dir);
    int found = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        CHECK(sizeof(*e) == 276 && e->reclen % 8 == 0);
        if (!strcmp(e->name, "renamed")) {
            found = 1;
            CHECK(e->type == 8);
        }
    }
    CHECK(found && closedir(dir) == 0);
    CHECK(unlink(BASE "renamed") == 0);
    char path[] = "/a/b///";
    CHECK(strcmp(basename(path), "b") == 0);
    return 0;
}
u32 fixture_key, fixture_mutex, fixture_cond, fixture_sem;
volatile int fixture_ready, fixture_destructors;
u32 fixture_worker_errno;
u32 fixture_worker_id;
void destructor_body(void *value) {
    ++fixture_destructors;
    if ((u32)value == 1)
        pthread_setspecific(fixture_key, (void *)2);
}
void *thread_body(void *arg) {
    fixture_worker_id = pthread_self();
    fixture_worker_errno = (u32)__errno();
    *__errno() = 84;
    pthread_setspecific(fixture_key, (void *)1);
    if (arg == (void *)7)
        pthread_exit((void *)77);
    pthread_mutex_lock(&fixture_mutex);
    fixture_ready = 1;
    pthread_cond_signal(&fixture_cond);
    pthread_mutex_unlock(&fixture_mutex);
    sem_wait(&fixture_sem);
    return (void *)42;
}
int fixture_threads(void) {
    struct attr a;
    CHECK(sizeof(a) == 24 && pthread_attr_init(&a) == 0);
    CHECK(pthread_attr_setstacksize(&a, 4096) == 22);
    CHECK(pthread_attr_setstacksize(&a, 1024 * 1024) == 0);
    CHECK(pthread_key_create(&fixture_key, aligned_destructor) == 0);
    CHECK(pthread_mutex_init(&fixture_mutex, 0) == 0);
    CHECK(pthread_cond_init(&fixture_cond, 0) == 0);
    CHECK(sem_init(&fixture_sem, 0, 0) == 0);
    CHECK(sem_trywait(&fixture_sem) == -1 && *__errno() == 11);
    *__errno() = 123;
    u32 id;
    CHECK(pthread_mutex_lock(&fixture_mutex) == 0);
    CHECK(pthread_create(&id, &a, aligned_thread, 0) == 0);
    while (!fixture_ready)
        CHECK(pthread_cond_wait(&fixture_cond, &fixture_mutex) == 0);
    CHECK(pthread_mutex_unlock(&fixture_mutex) == 0);
    CHECK(*__errno() == 123 && fixture_worker_errno != (u32)__errno());
    CHECK(pthread_equal(id, fixture_worker_id) && !pthread_equal(id, pthread_self()));
    CHECK(pthread_kill(id, 0) == 0);
    CHECK(sem_post(&fixture_sem) == 0);
    void *result = 0;
    CHECK(pthread_join(id, &result) == 0 && result == (void *)42);
    CHECK(fixture_destructors == 2);
    CHECK(pthread_create(&id, &a, aligned_thread, (void *)7) == 0);
    CHECK(pthread_join(id, &result) == 0 && result == (void *)77 && fixture_destructors == 4);
    CHECK(pthread_key_delete(fixture_key) == 0);
    CHECK(pthread_mutex_lock(&fixture_mutex) == 0);
    struct timespec t = {0, 0};
    CHECK(pthread_cond_timedwait(&fixture_cond, &fixture_mutex, &t) == 110);
    CHECK(pthread_mutex_unlock(&fixture_mutex) == 0);
    CHECK(pthread_cond_destroy(&fixture_cond) == 0);
    CHECK(pthread_mutex_destroy(&fixture_mutex) == 0);
    CHECK(sem_destroy(&fixture_sem) == 0);
    CHECK(pthread_attr_destroy(&a) == 0);
    return 0;
}
int fixture_misc(void) {
    u32 env[10];
    int value = setjmp(env);
    if (!value)
        longjmp(env, 7);
    CHECK(value == 7);
    value = setjmp(env);
    if (!value)
        longjmp(env, 0);
    CHECK(value == 1);
    int values[] = {5, -2, 17, 0, 5};
    qsort(values, 5, 4, aligned_compare);
    CHECK(values[0] == -2 && values[1] == 0 && values[4] == 17);
    CHECK(sqrt(9.0) == 3.0 && sin(0.0) == 0 && pow(2.0, 10.0) == 1024.0);
    *__errno() = 0;
    double bad = sqrt(-1.0);
    CHECK(bad != bad && *__errno() == 33);
    srand48(1);
    CHECK(lrand48() == 89400484 && lrand48() == 976015093);
    CHECK(strcmp(getenv("ANDROID_ROOT"), "/system") == 0 && getenv("NOVA_PRIVATE_HOST_SECRET") == 0);
    CHECK(sysconf(0x27) == 4096 && getuid() == 10000);
    CHECK(system("echo unsafe") == -1 && *__errno() == 13);
    void *p = mmap(0, 4096, 3, 2 | 0x20, -1, 0);
    CHECK(p != (void *)-1);
    ((u32 *)p)[0] = 123;
    void *q = mremap(p, 4096, 8192, 1);
    CHECK(q != (void *)-1 && ((u32 *)q)[0] == 123 && ((u32 *)q)[1024] == 0);
    CHECK(munmap(q, 8192) == 0);
    int fd = open(BASE "mapped", 2 | 64 | 512, 0600);
    CHECK(fd >= 0 && write(fd, "map", 3) == 3);
    p = mmap(0, 4096, 1, 2, fd, 0);
    CHECK(p != (void *)-1 && ((char *)p)[0] == 'm' && ((char *)p)[3] == 0);
    CHECK(lseek(fd, 0, 1) == 3);
    CHECK(lseek(fd, 0, 0) == 0 && write(fd, "M", 1) == 1 && ((char *)p)[0] == 'M');
    CHECK(close(fd) == 0 && ((char *)p)[1] == 'a');
    CHECK(munmap(p, 4096) == 0);
    fd = open(BASE "offset-map", 2 | 64 | 512, 0600);
    CHECK(fd >= 0 && lseek(fd, 8191, 0) == 8191 && write(fd, "Z", 1) == 1);
    p = mmap(0, 4096, 1, 2, fd, 4096);
    CHECK(p != (void *)-1 && ((char *)p)[4095] == 'Z');
    CHECK(munmap(p, 4096) == 0);
    p = mmap(0, 4096, 1, 2, fd, 0);
    CHECK(p != (void *)-1);
    q = mremap(p, 4096, 8192, 1);
    CHECK(q != (void *)-1 && ((char *)q)[8191] == 'Z');
    CHECK(close(fd) == 0 && munmap(q, 8192) == 0);
    int epoch = 0;
    struct tm tm;
    CHECK(gmtime_r(&epoch, &tm) == &tm && sizeof(tm) == 44 && tm.year == 70 && tm.mon == 0 && tm.mday == 1 &&
          tm.gmtoff == 0);
    char b[64];
    CHECK(strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm) == 19 && strcmp(b, "1970-01-01 00:00:00") == 0);
    epoch = 1700000000;
    CHECK(localtime_r(&epoch, &tm) == &tm && mktime(&tm) == epoch);
    CHECK(difftime(100, 40) == 60.0);
    u32 wc = 0;
    char narrow = 0;
    CHECK(mbrtowc(&wc, "Q", 1, 0) == 1 && wc == 'Q');
    CHECK(mbrtowc(&wc, "", 0, 0) == (size_t)-2);
    CHECK(mbrtowc(&wc, "\xff", 1, 0) == (size_t)-1 && *__errno() == 84);
    CHECK(wcrtomb(&narrow, 'A', 0) == 1 && narrow == 'A');
    CHECK(wcrtomb(&narrow, 0xe9, 0) == (size_t)-1 && *__errno() == 84);
    u32 text[] = {0x10000, 0x20000, 0, 0};
    CHECK(wcslen(text) == 2);
    CHECK(wmemmove(text + 1, text, 2) == text + 1 && text[1] == 0x10000 && text[2] == 0x20000);
    CHECK(iswctype('9', wctype("xdigit")) && !iswctype('z', wctype("xdigit")));
    struct signal_action action = {(u32)recorded_signal, 0x1234, 0x10000000, 0x5678}, previous = {0};
    CHECK(sizeof(action) == 16 && sigaction(10, &action, &previous) == 0 && previous.handler == 0);
    CHECK(sigaction(10, 0, &previous) == 0 && previous.handler == action.handler && previous.mask == 0x1234 &&
          previous.restorer == 0x5678);
    CHECK(raise(10) == 0 && signal_called == 0);
    CHECK(bsd_signal(10, 1) == (u32)recorded_signal);
    CHECK(sigaction(9, &action, 0) == -1 && *__errno() == 22);
    CHECK(__android_log_print(4, "fixture", "value=%d", 7) == 7);
    return 0;
}

union value {
    unsigned char z;
    signed char b;
    unsigned short c;
    short s;
    int i;
    i64 j;
    float f;
    double d;
    void *l;
};
struct env {
    void **table;
};
struct vm {
    void **table;
};
#define J(e, index, type) ((type)((e)->table[index]))
static i64 call_long_v(struct env *e, void *c, void *m, ...) {
    __builtin_va_list a;
    __builtin_va_start(a, m);
    i64 result = J(e, 133, i64 (*)(struct env *, void *, void *, __builtin_va_list))(e, c, m, a);
    __builtin_va_end(a);
    return result;
}
struct native_method {
    const char *name;
    const char *signature;
    void *function;
};
extern i64 aligned_native_long(struct env *, void *, int, i64, float, double, void *);
extern float aligned_native_float(struct env *, void *);
extern double aligned_native_double(struct env *, void *);
extern void *aligned_native_object(struct env *, void *);
i64 native_long_body(struct env *e, void *receiver, int i, i64 j, float f, double d, void *text) {
    (void)receiver;
    if (J(e, 164, int (*)(struct env *, void *))(e, text) != 3)
        return -1;
    return i + j + (i64)(f + d);
}
float native_float_body(struct env *e, void *receiver) {
    (void)e;
    (void)receiver;
    return 1.25f;
}
double native_double_body(struct env *e, void *receiver) {
    (void)e;
    (void)receiver;
    return 3.5;
}
void *native_object_body(struct env *e, void *receiver) {
    (void)receiver;
    J(e, 167, void *(*)(struct env *, const char *))(e, "temporary");
    return J(e, 167, void *(*)(struct env *, const char *))(e, "returned");
}
int fixture_jni(struct env *e) {
    CHECK(J(e, 4, int (*)(struct env *))(e) == 0x10006);
    void *c = J(e, 6, void *(*)(struct env *, const char *))(e, "synthetic/Example");
    CHECK(c);
    void *s = J(e, 167, void *(*)(struct env *, const char *))(e, "a\xc0\x80\xc3\xa9");
    CHECK(s);
    CHECK(J(e, 164, int (*)(struct env *, void *))(e, s) == 3);
    CHECK(J(e, 168, int (*)(struct env *, void *))(e, s) == 5);
    unsigned char copy = 9;
    const unsigned short *chars =
        J(e, 165, const unsigned short *(*)(struct env *, void *, unsigned char *))(e, s, &copy);
    CHECK(chars && chars[0] == 'a' && chars[1] == 0 && chars[2] == 0xe9);
    J(e, 166, void (*)(struct env *, void *, const void *))(e, s, chars);
    void *m = J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(
        e, c, "mixed", "(IJFDLjava/lang/String;)J");
    CHECK(m);
    i64 value = J(e, 132, i64 (*)(struct env *, void *, void *, ...))(e, c, m, 7, 0x123456789LL, 1.5, 2.5, s);
    CHECK(value == 0x123456794LL);
    CHECK(call_long_v(e, c, m, 7, 0x123456789LL, 1.5, 2.5, s) == value);
    union value a[5];
    a[0].i = 7;
    a[1].j = 0x123456789LL;
    a[2].f = 1.5f;
    a[3].d = 2.5;
    a[4].l = s;
    CHECK(J(e, 134, i64 (*)(struct env *, void *, void *, const union value *))(e, c, m, a) == value);
    void *fm =
        J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(e, c, "floatValue", "()F");
    CHECK(J(e, 135, float (*)(struct env *, void *, void *, ...))(e, c, fm) == 1.25f);
    void *dm =
        J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(e, c, "doubleValue", "()D");
    CHECK(J(e, 138, double (*)(struct env *, void *, void *, ...))(e, c, dm) == 3.5);
    void *unknown =
        J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(e, c, "unknown", "()I");
    CHECK(J(e, 129, int (*)(struct env *, void *, void *, ...))(e, c, unknown) == 0);
    // A missing cached ID must reach the runtime's explicit exception path,
    // not dereference it while decoding varargs (T04 push-init regression).
    CHECK(J(e, 129, int (*)(struct env *, void *, void *, ...))(e, c, 0) == 0);
    CHECK(J(e, 228, unsigned char (*)(struct env *))(e));
    J(e, 17, void (*)(struct env *))(e);
    void *global = J(e, 21, void *(*)(struct env *, void *))(e, s);
    CHECK(J(e, 232, int (*)(struct env *, void *))(e, global) == 2);
    J(e, 23, void (*)(struct env *, void *))(e, s);
    CHECK(J(e, 164, int (*)(struct env *, void *))(e, global) == 3);
    CHECK(J(e, 19, int (*)(struct env *, int))(e, 10) == 0);
    void *local = J(e, 25, void *(*)(struct env *, void *))(e, global);
    void *preserved = J(e, 20, void *(*)(struct env *, void *))(e, local);
    CHECK(J(e, 24, unsigned char (*)(struct env *, void *, void *))(e, preserved, global));
    J(e, 22, void (*)(struct env *, void *))(e, global);
    void *arr = J(e, 179, void *(*)(struct env *, int))(e, 3);
    int data[] = {4, 5, 6}, out[3] = {0};
    J(e, 211, void (*)(struct env *, void *, int, int, const void *))(e, arr, 0, 3, data);
    J(e, 203, void (*)(struct env *, void *, int, int, void *))(e, arr, 0, 3, out);
    CHECK(out[0] == 4 && out[2] == 6 && J(e, 171, int (*)(struct env *, void *))(e, arr) == 3);
    J(e, 203, void (*)(struct env *, void *, int, int, void *))(e, arr, 2, 3, out);
    CHECK(J(e, 228, unsigned char (*)(struct env *))(e));
    J(e, 17, void (*)(struct env *))(e);
    CHECK(!J(e, 228, unsigned char (*)(struct env *))(e));
    void *object = J(e, 27, void *(*)(struct env *, void *))(e, c);
    void *field = J(e, 94, void *(*)(struct env *, void *, const char *, const char *))(e, c, "counter", "J");
    J(e, 110, void (*)(struct env *, void *, void *, i64))(e, object, field, 0xabcdef123LL);
    CHECK(J(e, 101, i64 (*)(struct env *, void *, void *))(e, object, field) == 0xabcdef123LL);
    void *array = J(e, 172, void *(*)(struct env *, int, void *, void *))(e, 2, c, object);
    void *other = J(e, 173, void *(*)(struct env *, void *, int))(e, array, 1);
    CHECK(J(e, 24, unsigned char (*)(struct env *, void *, void *))(e, other, object));
    return 0;
}
static struct vm *test_vm;
static void *vm_thread(void *automatic) {
    struct env *e = (void *)1;
    int rc = J(test_vm, 6, int (*)(struct vm *, struct env **, int))(test_vm, &e, 0x10004);
    if (rc != -2 || e)
        return (void *)1;
    if (J(test_vm, 4, int (*)(struct vm *, struct env **, void *))(test_vm, &e, 0))
        return (void *)2;
    if (J(e, 4, int (*)(struct env *))(e) != 0x10006)
        return (void *)3;
    void *s = J(e, 167, void *(*)(struct env *, const char *))(e, "worker");
    void *global = J(e, 21, void *(*)(struct env *, void *))(e, s);
    if (!automatic && J(test_vm, 5, int (*)(struct vm *))(test_vm))
        return (void *)4;
    return global;
}
int fixture_jni_threads(struct env *e) {
    CHECK(J(e, 219, int (*)(struct env *, struct vm **))(e, &test_vm) == 0);
    u32 id;
    CHECK(pthread_create(&id, 0, vm_thread, 0) == 0);
    void *result = 0;
    CHECK(pthread_join(id, &result) == 0 && (u32)result > 4);
    CHECK(J(e, 168, int (*)(struct env *, void *))(e, result) == 6);
    J(e, 22, void (*)(struct env *, void *))(e, result);
    CHECK(pthread_create(&id, 0, vm_thread, (void *)1) == 0);
    CHECK(pthread_join(id, &result) == 0 && (u32)result > 4);
    CHECK(J(e, 168, int (*)(struct env *, void *))(e, result) == 6);
    J(e, 22, void (*)(struct env *, void *))(e, result);
    struct env *nullenv = (void *)1;
    CHECK(J(test_vm, 6, int (*)(struct vm *, struct env **, int))(test_vm, &nullenv, 0x10008) == -3 &&
          !nullenv);
    return 0;
}

int fixture_jni_trap(struct env *e) { return J(e, 5, int (*)(struct env *))(e); }

extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);
extern size_t malloc_usable_size(void *);
extern char *strdup(const char *);
int fixture_allocator(void) {
    for (size_t n = 0; n < 80; ++n) {
        unsigned char *p = malloc(n);
        CHECK(p && !((u32)p & 15));
        size_t usable = malloc_usable_size(p);
        CHECK(usable >= n && usable >= 12);
        for (size_t i = n; i < usable; ++i) CHECK(p[i] == 0xa5);
        memset(p, 0x42, usable);
        p = realloc(p, 100);
        CHECK(p && !((u32)p & 15));
        for (size_t i = 0; i < usable; ++i) CHECK(p[i] == 0x42);
        p = realloc(p, 4);
        CHECK(p && malloc_usable_size(p) >= 12 && ((u32 *)p)[0] == 0x42424242);
        CHECK(((u32 *)p)[1] == 0xa5a5a5a5 && ((u32 *)p)[2] == 0xa5a5a5a5);
        free(p);
    }
    u32 *p = calloc(1, 4);
    CHECK(p && p[0] == 0 && p[1] == 0xa5a5a5a5);
    CHECK(realloc(p, 0) == 0);
    CHECK(calloc(0x80000000u, 2) == 0 && *__errno() == 12);
    p = malloc(4); CHECK(p); p[0] = 7;
    CHECK(realloc(p, 0xffffffffu) == 0 && p[0] == 7); free(p);
    char *text = strdup("guest"); CHECK(text && !strcmp(text,"guest")); free(text);
    CHECK(malloc_usable_size(0) == 0);
    return 0;
}
