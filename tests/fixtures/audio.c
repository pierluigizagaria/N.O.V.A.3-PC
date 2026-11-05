// Independent synthetic guest. No Android headers or game code.
typedef unsigned int u32;
typedef void *Itf;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            return __LINE__;                                                                       \
    } while (0)
#define FN(i, slot, type) ((type)(*(u32 **)(i))[slot])
extern const void *SL_IID_ENGINE, *SL_IID_PLAY, *SL_IID_BUFFERQUEUE;
extern u32 slCreateEngine(Itf *, u32, const void *, u32, const void *const *, const u32 *);
extern int usleep(u32), printf(const char *, ...);
extern int *__errno(void);
extern u32 pthread_self(void);
extern int pthread_key_create(u32 *, void (*)(void *)), pthread_key_delete(u32);
extern int pthread_setspecific(u32, const void *);
extern void *pthread_getspecific(u32);
struct timeval {
    int seconds, micros;
};
extern int gettimeofday(struct timeval *, void *);
extern void aligned_stream_callback(Itf, void *), aligned_stream_destructor(void *);
typedef u32 (*StateFn)(Itf, u32);
typedef u32 (*GetFn)(Itf, const void *, Itf *);
typedef u32 (*MixFn)(Itf, Itf *, u32, const void *const *, const u32 *);
typedef u32 (*PlayerFn)(Itf, Itf *, const void *, const void *, u32, const void *const *,
                        const u32 *);
typedef u32 (*QueueFn)(Itf, const void *, u32);
typedef u32 (*CallbackFn)(Itf, void (*)(Itf, void *), void *);
typedef u32 (*QueryFn)(Itf, u32 *);
typedef void (*DestroyFn)(Itf);
static Itf queue, player;
static u32 key, main_thread, bits_, channels_, endian_, bytes_, sent, target, slow_;
static volatile u32 callbacks, error, destructors;
static unsigned char pcm[8192];
static long long first_time, last_time;
static long long now_us(void) {
    struct timeval t;
    gettimeofday(&t, (void *)0);
    return (long long)t.seconds * 1000000 + t.micros;
}
static void fill(u32 sequence) {
    for (u32 i = 0; i < bytes_; ++i)
        pcm[i] = (unsigned char)(sequence * 7 + i * 13);
}
void stream_destructor_body(void *value) {
    if (value != (void *)0x4455 || *__errno() != 1337)
        error = 10;
    __atomic_add_fetch(&destructors, 1, __ATOMIC_RELEASE);
}
void stream_callback_body(Itf q, void *context) {
    u32 n = __atomic_load_n(&callbacks, __ATOMIC_ACQUIRE);
    if (q != queue || context != (void *)0x1234 || pthread_self() == main_thread)
        error = 1;
    if (n == 0) {
        if (*__errno() != 0 || pthread_getspecific(key))
            error = 2;
        *__errno() = 1337;
        pthread_setspecific(key, (void *)0x4455);
        first_time = now_us();
        if (slow_)
            usleep(120000);
    } else if (*__errno() != 1337 || pthread_getspecific(key) != (void *)0x4455)
        error = 3;
    u32 state[2];
    if (FN(q, 2, QueryFn)(q, state) || state[1] != n + 1 || state[0] > 1)
        error = 4;
    if (sent < target) {
        fill(sent++);
        if (FN(q, 0, QueueFn)(q, pcm, bytes_))
            error = 5;
    }
    last_time = now_us();
    __atomic_store_n(&callbacks, n + 1, __ATOMIC_RELEASE);
}
int fixture_audio_stream(u32 bits, u32 channels, u32 rate, u32 endian, u32 slow) {
    bits_ = bits;
    channels_ = channels;
    endian_ = endian;
    slow_ = slow;
    callbacks = error = destructors = sent = 0;
    target = 120;
    bytes_ = (rate / 100) * channels * (bits / 8);
    CHECK(bytes_ <= sizeof pcm);
    main_thread = pthread_self();
    *__errno() = 73;
    CHECK(pthread_key_create(&key, aligned_stream_destructor) == 0);
    Itf engine_object, engine, mix, play;
    CHECK(slCreateEngine(&engine_object, 0, (void *)0, 0, (void *)0, (void *)0) == 0);
    CHECK(FN(engine_object, 0, StateFn)(engine_object, 0) == 0);
    CHECK(FN(engine_object, 3, GetFn)(engine_object, SL_IID_ENGINE, &engine) == 0);
    CHECK(FN(engine, 7, MixFn)(engine, &mix, 0, (void *)0, (void *)0) == 0);
    CHECK(FN(mix, 0, StateFn)(mix, 0) == 0);
    u32 locator[] = {6, 2};
    u32 format[] = {2, channels, rate * 1000, bits, bits, channels == 1 ? 4 : 3, endian};
    const void *source[] = {locator, format};
    u32 output[] = {4, (u32)mix};
    const void *sink[] = {output, (void *)0};
    const void *ids[] = {SL_IID_BUFFERQUEUE};
    u32 required[] = {1};
    CHECK(FN(engine, 2, PlayerFn)(engine, &player, source, sink, 1, ids, required) == 0);
    CHECK(FN(player, 0, StateFn)(player, 0) == 0);
    CHECK(FN(player, 3, GetFn)(player, SL_IID_PLAY, &play) == 0);
    CHECK(FN(player, 3, GetFn)(player, SL_IID_BUFFERQUEUE, &queue) == 0);
    CHECK(FN(queue, 3, CallbackFn)(queue, aligned_stream_callback, (void *)0x1234) == 0);
    for (; sent < 2; ++sent) {
        fill(sent);
        CHECK(FN(queue, 0, QueueFn)(queue, pcm, bytes_) == 0);
    }
    long long start = now_us(), deadline = start + 6000000;
    CHECK(FN(play, 0, StateFn)(play, 3) == 0);
    while (__atomic_load_n(&callbacks, __ATOMIC_ACQUIRE) < target && now_us() < deadline)
        usleep(1000);
    CHECK(callbacks == target && error == 0);
    long long expected = target * 10000 - (rate % 100) * target * 10000 / rate;
    // Dummy devices are scheduler-paced; allow one device period plus bounded CI jitter.
    CHECK(last_time - start > expected - 40000 &&
          last_time - start < expected + (slow ? 500000 : 250000));
    CHECK(*__errno() == 73 && pthread_getspecific(key) == (void *)0);
    CHECK(FN(play, 0, StateFn)(play, 1) == 0);
    FN(player, 6, DestroyFn)(player);
    CHECK(destructors == 1 && error == 0);
    CHECK(pthread_key_delete(key) == 0);
    FN(mix, 6, DestroyFn)(mix);
    FN(engine_object, 6, DestroyFn)(engine_object);
    printf("audio_stream bits=%u channels=%u rate=%u endian=%u callbacks=%u first_us=%lld "
           "duration_us=%lld expected_us=%lld\n",
           bits, channels, rate, endian, callbacks, first_time - start, last_time - start,
           expected);
    return 0;
}
