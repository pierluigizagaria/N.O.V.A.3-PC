typedef unsigned int u32;
typedef unsigned char u8;
typedef void *Itf;
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x))                                                                                       \
            return __LINE__;                                                                            \
    } while (0)
extern void glBindFramebuffer(u32, u32);
extern void glGenFramebuffers(int, u32 *);
extern void glDeleteFramebuffers(int, const u32 *);
extern void glGenRenderbuffers(int, u32 *);
extern void glBindRenderbuffer(u32, u32);
extern void glRenderbufferStorage(u32, u32, int, int);
extern void glFramebufferRenderbuffer(u32, u32, u32, u32);
extern void glGetIntegerv(u32, int *);
extern void glGetFloatv(u32, float *);
extern u32 glCheckFramebufferStatus(u32);
extern u32 glGetError(void);
extern void glClearColor(float, float, float, float);
extern void glClear(u32);
extern void glViewport(int, int, int, int);
extern void glReadPixels(int, int, int, int, u32, u32, void *);
extern void glGenBuffers(int, u32 *);
extern void glBindBuffer(u32, u32);
extern void glBufferData(u32, int, const void *, u32);
extern void *glMapBufferOES(u32, u32);
extern u8 glUnmapBufferOES(u32);
extern void glGenTextures(int, u32 *);
extern void glBindTexture(u32, u32);
extern void glTexImage3DOES(u32, int, int, int, int, int, int, u32, u32, const void *);
extern void glDeleteTextures(int, const u32 *);
int fixture_framebuffer(void) {
    const u32 FB = 0x8d40, RB = 0x8d41;
    int binding = -1;
    float floating = -1;
    glBindFramebuffer(FB, 0);
    glGetIntegerv(0x8ca6, &binding);
    CHECK(binding == 0);
    glGetFloatv(0x8ca6, &floating);
    CHECK(floating == 0.0f);
    CHECK(glCheckFramebufferStatus(FB) == 0x8cd5);
    glViewport(0, 0, 64, 64);
    glClearColor(.25f, .5f, .75f, 1.0f);
    glClear(0x4000 | 0x100 | 0x400);
    u8 pixel[4] = {0};
    glReadPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    CHECK(pixel[0] >= 63 && pixel[0] <= 64 && pixel[1] == 128 && pixel[2] == 191 && pixel[3] == 255);
    glFramebufferRenderbuffer(FB, 0x8ce0, RB, 0);
    CHECK(glGetError() == 0x502 && glGetError() == 0);
    u32 fb, rb;
    glGenFramebuffers(1, &fb);
    glGenRenderbuffers(1, &rb);
    glBindFramebuffer(FB, fb);
    glBindRenderbuffer(RB, rb);
    glRenderbufferStorage(RB, 0x8058, 8, 8);
    glFramebufferRenderbuffer(FB, 0x8ce0, RB, rb);
    glGetIntegerv(0x8ca6, &binding);
    CHECK(binding == (int)fb);
    CHECK(glCheckFramebufferStatus(FB) == 0x8cd5);
    glDeleteFramebuffers(1, &fb);
    glGetIntegerv(0x8ca6, &binding);
    CHECK(binding == 0);
    CHECK(glCheckFramebufferStatus(FB) == 0x8cd5);
    glReadPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    CHECK(pixel[1] == 128);
    glBindFramebuffer(0xdead, 0);
    CHECK(glGetError() == 0x500);
    u32 buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(0x8892, buffer);
    glBufferData(0x8892, 16, (void *)0, 0x88e8);
    u32 *mapped = glMapBufferOES(0x8892, 0x88b9);
    CHECK(mapped != (void *)0);
    mapped[0] = 0x12345678;
    mapped[3] = 0xabcdef01;
    CHECK(glUnmapBufferOES(0x8892) == 1);
    glBindBuffer(0x8892, 0);
    u32 texture;
    glGenTextures(1, &texture);
    glBindTexture(0x806f, texture);
    glTexImage3DOES(0x806f, 0, 0x1908, 1, 1, 1, 0, 0x1908, 0x1401, pixel);
    glDeleteTextures(1, &texture);
    CHECK(glGetError() == 0);
    glViewport(-2, 3, 60, 50);
    return 0;
}

extern const void *SL_IID_ENGINE, *SL_IID_PLAY, *SL_IID_BUFFERQUEUE;
extern u32 slCreateEngine(Itf *, u32, const void *, u32, const void *const *, const u32 *);
extern int usleep(u32);
extern int printf(const char *, ...);
struct timeval {
    int seconds, micros;
};
extern int gettimeofday(struct timeval *, void *);
extern void aligned_audio_callback(Itf, void *);
// SDL may retire a buffer at the start of the final device period; null uses zero margin.
u32 fixture_audio_timing_margin;
static Itf expected_queue;
static volatile u32 callbacks, callback_error;
static long long timestamps[32];
static long long now_us(void) {
    struct timeval value;
    gettimeofday(&value, (void *)0);
    return (long long)value.seconds * 1000000 + value.micros;
}
void audio_callback_body(Itf queue, void *context) {
    if (queue != expected_queue || context != (void *)0x1234)
        __atomic_store_n(&callback_error, 1, __ATOMIC_RELEASE);
    u32 n = __atomic_load_n(&callbacks, __ATOMIC_ACQUIRE);
    if (n < 32)
        timestamps[n] = now_us();
    __atomic_store_n(&callbacks, n + 1, __ATOMIC_RELEASE);
}
#define FN(i, slot, type) ((type)(*(u32 **)(i))[slot])
typedef u32 (*StateFn)(Itf, u32);
typedef u32 (*GetItfFn)(Itf, const void *, Itf *);
typedef u32 (*MixFn)(Itf, Itf *, u32, const void *const *, const u32 *);
typedef u32 (*PlayerFn)(Itf, Itf *, const void *, const void *, u32, const void *const *, const u32 *);
typedef u32 (*QueueFn)(Itf, const void *, u32);
typedef u32 (*CallbackFn)(Itf, void (*)(Itf, void *), void *);
typedef u32 (*QueryFn)(Itf, u32 *);
typedef u32 (*ClearFn)(Itf);
typedef void (*DestroyFn)(Itf);
static int wait_callbacks(u32 n) {
    long long deadline = now_us() + 2000000;
    while (__atomic_load_n(&callbacks, __ATOMIC_ACQUIRE) < n && now_us() < deadline)
        usleep(1000);
    return __atomic_load_n(&callbacks, __ATOMIC_ACQUIRE) == n;
}
int fixture_audio(void) {
    Itf engine_object, engine, mix, player_object, play, queue;
    CHECK(slCreateEngine(&engine_object, 0, (void *)0, 0, (void *)0, (void *)0) == 0);
    CHECK(FN(engine_object, 0, StateFn)(engine_object, 0) == 0);
    CHECK(FN(engine_object, 3, GetItfFn)(engine_object, SL_IID_ENGINE, &engine) == 0);
    CHECK(FN(engine, 7, MixFn)(engine, &mix, 0, (void *)0, (void *)0) == 0);
    CHECK(FN(mix, 0, StateFn)(mix, 0) == 0);
    u32 locator[] = {6, 2};
    u32 pcm[] = {2, 1, 48000000, 16, 16, 4, 2};
    const void *source[] = {locator, pcm};
    u32 output[] = {4, (u32)mix};
    const void *sink[] = {output, (void *)0};
    const void *ids[] = {SL_IID_BUFFERQUEUE};
    u32 required[] = {1};
    CHECK(FN(engine, 2, PlayerFn)(engine, &player_object, source, sink, 1, ids, required) == 0);
    CHECK(FN(player_object, 0, StateFn)(player_object, 0) == 0);
    CHECK(FN(player_object, 3, GetItfFn)(player_object, SL_IID_PLAY, &play) == 0);
    CHECK(FN(player_object, 3, GetItfFn)(player_object, SL_IID_BUFFERQUEUE, &queue) == 0);
    expected_queue = queue;
    CHECK(FN(queue, 3, CallbackFn)(queue, aligned_audio_callback, (void *)0x1234) == 0);
    static unsigned char samples[9600];
    CHECK(FN(queue, 0, QueueFn)(queue, samples, sizeof samples) == 0);
    CHECK(FN(queue, 0, QueueFn)(queue, samples, sizeof samples) == 0);
    CHECK(FN(queue, 0, QueueFn)(queue, samples, sizeof samples) == 7);
    usleep(30000);
    CHECK(callbacks == 0);
    long long started = now_us();
    CHECK(FN(play, 0, StateFn)(play, 3) == 0);
    CHECK(wait_callbacks(2));
    printf("audio_timing first_us=%lld second_us=%lld\n", timestamps[0]-started,timestamps[1]-started);
    CHECK(timestamps[0] - started >= 90000 && timestamps[0] - started < 1000000);
    CHECK(timestamps[1] - started >= 195000 - fixture_audio_timing_margin && timestamps[1] - started < 300000);
    CHECK(callback_error == 0);
    u32 state[2];
    CHECK(FN(queue, 2, QueryFn)(queue, state) == 0 && state[0] == 0 && state[1] == 2);
    CHECK(FN(play, 0, StateFn)(play, 2) == 0);
    CHECK(FN(queue, 0, QueueFn)(queue, samples, sizeof samples) == 0);
    usleep(120000);
    CHECK(callbacks == 2);
    CHECK(FN(play, 0, StateFn)(play, 3) == 0);
    CHECK(wait_callbacks(3));
    CHECK(FN(queue, 0, QueueFn)(queue, samples, sizeof samples) == 0);
    CHECK(FN(queue, 1, ClearFn)(queue) == 0);
    usleep(150000);
    CHECK(callbacks == 3);
    CHECK(FN(queue, 2, QueryFn)(queue, state) == 0 && state[0] == 0 && state[1] == 0);
    CHECK(FN(play, 0, StateFn)(play, 1) == 0);
    FN(player_object, 6, DestroyFn)(player_object);

    printf("null_audio first_us=%lld second_interval_us=%lld callbacks=%u\n", timestamps[0] - started,
           timestamps[1] - timestamps[0], callbacks);
    // Short buffers expose cumulative scheduler drift: 20 x 10 ms should
    // consume about 200 ms, not 20 rounded-up Windows timer intervals.
    locator[1] = 32;
    CHECK(FN(engine, 2, PlayerFn)(engine, &player_object, source, sink, 1, ids, required) == 0);
    CHECK(FN(player_object, 0, StateFn)(player_object, 0) == 0);
    CHECK(FN(player_object, 3, GetItfFn)(player_object, SL_IID_PLAY, &play) == 0);
    CHECK(FN(player_object, 3, GetItfFn)(player_object, SL_IID_BUFFERQUEUE, &queue) == 0);
    expected_queue = queue;
    callbacks = 0;
    CHECK(FN(queue, 3, CallbackFn)(queue, aligned_audio_callback, (void *)0x1234) == 0);
    for (int i = 0; i < 20; ++i)
        CHECK(FN(queue, 0, QueueFn)(queue, samples, 960) == 0);
    long long short_started = now_us();
    CHECK(FN(play, 0, StateFn)(play, 3) == 0 && wait_callbacks(20));
    long long short_elapsed = timestamps[19] - short_started;
    CHECK(short_elapsed >= 195000 - fixture_audio_timing_margin && short_elapsed <= 260000 && callback_error == 0);
    FN(player_object, 6, DestroyFn)(player_object);
    printf("null_audio short_buffers=20 duration_us=%lld expected_us=200000\n", short_elapsed);
    FN(mix, 6, DestroyFn)(mix);
    FN(engine_object, 6, DestroyFn)(engine_object);
    return 0;
}
