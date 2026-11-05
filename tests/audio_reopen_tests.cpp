// Device-free fixture: includes the production adapter and supplies every SDL symbol locally.
// Do not link SDL3::SDL3 or nova_media into this target. No guest image is loaded.
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifndef NOVA_AUDIO_REOPEN_SOURCE
#define NOVA_AUDIO_REOPEN_SOURCE "../src/audio/opensl.cpp"
#endif
#include NOVA_AUDIO_REOPEN_SOURCE

struct SDL_AudioStream {
    SDL_AudioDeviceID device = 42;
    int queued = 0;
    bool paused = true;
    SDL_AudioStreamCallback callback = nullptr;
    void *userdata = nullptr;
    std::vector<uint8_t> bytes;
};
namespace mock {
bool init_ok = true, open_ok = true, put_ok = true;
unsigned opens = 0, closes = 0, queries = 0, quits = 0;
SDL_AudioSpec last_spec{};
std::function<void()> during_open;
std::vector<uint8_t> submitted;
void reset() {
    init_ok = open_ok = put_ok = true;
    opens = closes = queries = quits = 0;
    last_spec = {};
    during_open = {};
    submitted.clear();
}
}
extern "C" {
bool SDLCALL SDL_InitSubSystem(SDL_InitFlags) { return mock::init_ok; }
void SDLCALL SDL_QuitSubSystem(SDL_InitFlags) { ++mock::quits; }
bool SDLCALL SDL_SetHint(const char *, const char *) { return true; }
const char *SDLCALL SDL_GetError() { return "injected audio failure"; }
const char *SDLCALL SDL_GetCurrentAudioDriver() { return "fixture-only"; }
bool SDLCALL SDL_IsAudioDevicePhysical(SDL_AudioDeviceID d) {
    ++mock::queries;
    return d == 11;
}
const char *SDLCALL SDL_GetAudioDeviceName(SDL_AudioDeviceID) { return "fixture device"; }
bool SDLCALL SDL_GetAudioDeviceFormat(SDL_AudioDeviceID, SDL_AudioSpec *s, int *frames) {
    *s = mock::last_spec;
    *frames = 160;
    return true;
}
SDL_AudioStream *SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID d,
    const SDL_AudioSpec *s, SDL_AudioStreamCallback cb, void *ud) {
    if (d != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
        std::fputs("M7a fixture invariant: not default playback\n", stderr);
        std::exit(1);
    }
    ++mock::opens;
    mock::last_spec = *s;
    auto action = std::move(mock::during_open);
    mock::during_open = {};
    if (action)
        action();
    if (!mock::open_ok)
        return nullptr;
    auto *out = new SDL_AudioStream;
    out->callback = cb;
    out->userdata = ud;
    return out;
}
SDL_AudioDeviceID SDLCALL SDL_GetAudioStreamDevice(SDL_AudioStream *s) { return s->device; }
void SDLCALL SDL_DestroyAudioStream(SDL_AudioStream *s) { ++mock::closes; delete s; }
int SDLCALL SDL_GetAudioStreamQueued(SDL_AudioStream *s) { return s->queued; }
bool SDLCALL SDL_PutAudioStreamData(SDL_AudioStream *s, const void *p, int n) {
    if (!mock::put_ok)
        return false;
    const auto *b = static_cast<const uint8_t *>(p);
    s->bytes.insert(s->bytes.end(), b, b + n);
    mock::submitted.insert(mock::submitted.end(), b, b + n);
    s->queued += n;
    return true;
}
bool SDLCALL SDL_FlushAudioStream(SDL_AudioStream *) { return true; }
bool SDLCALL SDL_ClearAudioStream(SDL_AudioStream *s) { s->queued = 0; return true; }
bool SDLCALL SDL_PauseAudioStreamDevice(SDL_AudioStream *s) { s->paused = true; return true; }
bool SDLCALL SDL_ResumeAudioStreamDevice(SDL_AudioStream *s) { s->paused = false; return true; }
}

namespace {
using namespace nova::audio;
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Clock::time_point at(std::chrono::nanoseconds n) { return Clock::time_point{} + n; }
std::vector<uint8_t> pcm(size_t n, uint8_t seed) {
    std::vector<uint8_t> bytes(n);
    for (size_t i = 0; i < n; ++i)
        bytes[i] = static_cast<uint8_t>(seed + i % 31);
    return bytes;
}
struct Player {
    Object &o;
    Player() : o(*create_object(Object::player)) {
        o.realized = true;
        o.play_state = playing;
        o.capacity = 8;
        o.channels = 1;
        o.rate = 8000000;
        o.container_bits = 8;
        o.endian = 2;
    }
    ~Player() {
        destroy(&o.object);
        if (o.stream) {
            SDL_DestroyAudioStream(o.stream);
            o.stream = nullptr;
            o.device = 0;
        }
        if (o.wave) {
            o.wave->close();
            o.wave.reset();
        }
    }
    void add(const std::vector<uint8_t> &bytes) {
        require(enqueue(&o.queue, bytes.data(), static_cast<Word>(bytes.size())) == success,
                "enqueue failed");
    }
    void initial() {
        std::lock_guard lock(o.mutex);
        open_initial(o);
    }
    Reopen retry(std::chrono::nanoseconds now) {
        std::unique_lock lock(o.mutex);
        return try_reopen(o, lock, at(now));
    }
    void null_progress(std::chrono::nanoseconds elapsed) {
        std::unique_lock lock(o.mutex);
        require(!o.stream && !o.buffers.empty(), "not a null buffer");
        prepare_null(o);
        advance_null(o, at(0ns), at(elapsed));
        if (o.buffers.front().remaining == 0ns)
            complete(o, lock);
    }
    void consume_input(uint32_t bytes) {
        std::unique_lock lock(o.mutex);
        require(o.stream && o.stream->queued >= static_cast<int>(bytes), "bad mock consumption");
        o.stream->queued -= static_cast<int>(bytes);
        account_sdl(o, static_cast<uint32_t>(o.stream->queued));
        while (!o.buffers.empty() && o.buffers.front().used == o.buffers.front().data.size())
            complete(o, lock);
    }
    void submit() {
        std::lock_guard lock(o.mutex);
        submit_sdl(o, o.stream->queued);
    }
    void lose() {
        std::lock_guard lock(o.mutex);
        fallback(o, "fixture loss after accounted consumption");
    }
};
std::function<void()> on_completion;
unsigned completions = 0;
Word __cdecl callback(Word, Word) {
    ++completions;
    if (on_completion)
        on_completion();
    return 0;
}
void callbacks(Player &p) {
    completions = 0;
    on_completion = {};
    require(queue_callback(&p.o.queue, pointer(callback), 0) == success, "callback registration");
}
void begin() {
    mock::reset();
    configure({"sdl", {}});
}
std::vector<uint8_t> wav_payload(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    require(bytes.size() >= 44, "missing WAV header");
    return {bytes.begin() + 44, bytes.end()};
}
void finish_wave(Player &p) {
    p.o.wave->close();
    p.o.wave.reset();
}
void partial_loss_reopen(const std::filesystem::path &dir) {
    begin();
    Player p;
    auto a = pcm(320, 16), b = pcm(80, 60), c = pcm(160, 100);
    callbacks(p);
    bool refilled = false;
    on_completion = [&] { if (!refilled) { refilled = true; p.add(c); } };
    const auto file = dir / "partial.wav";
    p.o.wave = std::make_unique<Wave>(file, 1, 8000, 8);
    p.initial();
    p.add(a); p.add(b);
    p.submit();
    require(p.o.stream->queued == 160 && p.o.buffers.front().sent == 160, "20 ms cap");
    p.consume_input(64);
    require(p.o.position == 8ms && p.o.buffers.front().used == 64, "partial SDL accounting");
    p.lose();
    p.null_progress(7ms);
    require(p.o.position == 15ms && p.o.buffers.front().remaining == 25ms,
            "null must continue after consumed prefix");
    device_added(11);
    const auto opens = mock::opens;
    p.retry(10s);
    // Negative mutation removes the pristine checks. This must reject that transition.
    require(mock::opens == opens && !p.o.stream && p.o.stats.callbacks == 0 &&
            p.o.position == 15ms && p.o.buffers.front().remaining == 25ms,
            "REOPEN_TAINTED_QUEUE: consumed/tapped PCM admitted to empty stream");
    p.null_progress(25ms);
    require(completions == 1 && p.o.stats.callback_enqueues == 1 && p.o.position == 40ms,
            "loss completion/reentrant refill");
    require(p.retry(11s) == Reopen::opened, "continuous refill did not reopen");
    require(p.o.stream->paused && p.o.stream->queued == 0 && p.o.submitted == 0 &&
            p.o.consumed == 0 && p.o.stats.submitted_bytes == a.size(),
            "new stream inherited offsets or tap count reset");
    p.submit();
    require(p.o.stream->queued == 160, "reopened input cap");
    p.consume_input(160);
    p.submit();
    p.consume_input(80);
    require(p.o.position == 70ms && completions == 3 && p.o.buffers.empty(),
            "reopen position/completion total");
    Word guest_ms = UINT32_MAX, guest_queue[2] = {UINT32_MAX, UINT32_MAX};
    require(position(&p.o.play, &guest_ms) == success && guest_ms == 70 &&
            queue_state(&p.o.queue, guest_queue) == success && guest_queue[0] == 0 &&
            guest_queue[1] == 3, "guest-visible position/queue state after recovery");
    std::vector<uint8_t> audible(b);
    audible.insert(audible.end(), c.begin(), c.end());
    require(p.o.stream->bytes == audible, "reopened stream replayed/lost PCM");
    std::vector<uint8_t> expected(a);
    expected.insert(expected.end(), b.begin(), b.end());
    expected.insert(expected.end(), c.begin(), c.end());
    finish_wave(p);
    require(wav_payload(file) == expected && p.o.stats.submitted_bytes == expected.size(),
            "WAV tap duplicated/lost PCM across fallback");
}
void prefed_successor() {
    begin(); Player p; callbacks(p); p.initial();
    auto a = pcm(80, 10), b = pcm(160, 50), c = pcm(80, 90);
    p.add(a); p.add(b); p.submit(); p.consume_input(40);
    require(p.o.buffers[1].sent == 80, "fixture needs partially prefed successor");
    p.lose(); device_added(11);
    p.null_progress(5ms); // First buffer completes; successor already tapped/submitted.
    p.add(c);
    const auto count = mock::opens;
    require(p.retry(1s) == Reopen::skipped && mock::opens == count,
            "prefed successor was treated as pristine");
    p.null_progress(20ms);
    require(p.retry(2s) == Reopen::opened, "pristine tail failed to reopen");
    p.submit(); p.consume_input(80);
    require(p.o.stream->bytes == c && completions == 3 && p.o.position == 40ms &&
            p.o.stats.submitted_bytes == 320, "prefed successor replay/accounting");
}
void failed_retries() {
    begin(); mock::open_ok = false;
    Player p; callbacks(p); p.initial();
    require(!p.o.stream && p.o.reopen_eligible && mock::opens == 1, "initial open failure");
    auto a = pcm(80, 10); p.add(a); device_added(11);
    require(p.retry(0s) == Reopen::failed && mock::opens == 2, "first retry failure");
    require(p.retry(2s) == Reopen::skipped && mock::opens == 2, "retry without hint");
    p.null_progress(3ms); device_added(11);
    require(p.retry(2s) == Reopen::skipped && p.o.buffers.front().remaining == 7ms,
            "retry rewound partially consumed null buffer");
    p.null_progress(7ms); p.add(a);
    require(p.retry(2s) == Reopen::failed && mock::opens == 3, "second notified failure");
    device_added(11); device_added(11);
    require(p.retry(2100ms) == Reopen::skipped && mock::opens == 3, "cooldown/coalescing");
    p.null_progress(10ms); p.add(a);
    mock::open_ok = true;
    require(p.retry(3100ms) == Reopen::opened && mock::opens == 4, "retained hint after cooldown");
    require(p.o.position == 20ms && completions == 2 && p.o.stats.fallbacks == 1 &&
            p.o.stats.submitted_bytes == 160, "failed retries changed null accounting");
    p.submit(); p.consume_input(80);
    require(p.o.position == 30ms && completions == 3 && p.o.stream->bytes == a,
            "failed retry recovery lost next buffer");
}
void pending_states() {
    begin(); mock::open_ok = false;
    Player p; callbacks(p); p.initial(); p.add(pcm(80, 20)); device_added(11);
    p.null_progress(2ms);
    require(set_play_state(&p.o.play, 2) == success, "pause");
    const auto attempts = mock::opens;
    require(p.retry(1s) == Reopen::skipped && p.o.position == 2ms && mock::opens == attempts,
            "paused retry progressed audio");
    require(clear(&p.o.queue) == success && p.o.position == 2ms && p.o.completed == 0,
            "clear altered position");
    p.add(pcm(80, 40));
    require(p.retry(2s) == Reopen::skipped, "clear while paused resumed stream");
    require(set_play_state(&p.o.play, stopped) == success && p.o.position == 0ns,
            "stop did not reset position");
    require(p.retry(3s) == Reopen::skipped, "stopped retry");
    mock::open_ok = true;
    require(set_play_state(&p.o.play, playing) == success, "resume");
    require(p.retry(4s) == Reopen::opened, "pending hint lost across pause/clear/stop");
    p.submit(); p.consume_input(80);
    require(p.o.position == 10ms && completions == 1 && p.o.stats.callbacks == 1,
            "clear generated completion or stop retained old position");
}
void inflight_changes() {
    for (int mode = 0; mode < 4; ++mode) {
        begin(); mock::open_ok = false;
        Player p; callbacks(p); p.initial(); p.add(pcm(80, 50)); device_added(11);
        mock::open_ok = true;
        const auto closes = mock::closes;
        mock::during_open = [&] {
            if (mode == 0) {
                require(set_play_state(&p.o.play, 2) == success, "inflight pause");
                require(set_play_state(&p.o.play, playing) == success, "inflight resume");
            } else if (mode == 1) {
                require(clear(&p.o.queue) == success, "inflight clear");
                p.add(pcm(80, 70));
            } else if (mode == 2) {
                require(set_play_state(&p.o.play, stopped) == success, "inflight stop");
                require(set_play_state(&p.o.play, playing) == success, "inflight play");
            } else {
                destroy(&p.o.object);
            }
        };
        require(p.retry(1s) == Reopen::cancelled && !p.o.stream && p.o.device == 0 &&
                mock::closes == closes + 1 && mock::submitted.empty(),
                "stale candidate published or leaked");
        require(p.o.position == 0ns && completions == 0, "cancel synthesized progress");
        require(p.retry(1500ms) == Reopen::skipped, "cancel bypassed cooldown");
        if (mode == 3) {
            device_added(11);
            require(p.retry(3s) == Reopen::skipped && p.o.dead, "destroyed player reopened");
        } else {
            require(p.retry(2s) == Reopen::opened, "cancelled availability not retained");
            p.submit(); p.consume_input(80);
            require(completions == 1 && p.o.position == 10ms, "post-cancel buffer accounting");
        }
    }
}
void reentrant_completion() {
    for (int mode = 0; mode < 3; ++mode) {
        begin(); mock::open_ok = false;
        Player p; callbacks(p); p.initial(); p.add(pcm(80, 40)); device_added(11);
        on_completion = [&] {
            if (mode == 0) {
                require(clear(&p.o.queue) == success, "callback clear");
                p.add(pcm(80, 80));
            } else if (mode == 1) {
                require(set_play_state(&p.o.play, stopped) == success, "callback stop");
                p.add(pcm(80, 90));
            } else {
                destroy(&p.o.object);
            }
        };
        p.null_progress(10ms); // complete() must unlock before entering this host callback.
        on_completion = {};
        mock::open_ok = true;
        require(completions == 1, "missing or duplicate reentrant completion");
        if (mode == 0) {
            require(p.retry(1s) == Reopen::opened && p.o.position == 10ms && p.o.completed == 0,
                    "reentrant clear semantics");
        } else {
            require(p.retry(1s) == Reopen::skipped, "reentrant stop/destroy reopened");
            require(p.o.position == (mode == 1 ? 0ms : 10ms), "reentrant state position");
        }
    }
}
void excluded_backends() {
    for (bool explicit_null : {true, false}) {
        mock::reset();
        mock::init_ok = explicit_null;
        configure({explicit_null ? "null" : "sdl", {}});
        Player p; callbacks(p); p.initial(); p.add(pcm(80, 40));
        device_added(11);
        device_removed(11);
        { std::lock_guard lock(p.o.mutex); request_reopen(p.o); }
        require(p.retry(1s) == Reopen::skipped && mock::opens == 0 && mock::queries == 0 &&
                !p.o.reopen_eligible, "explicit null/init failure queried or opened backend");
        p.null_progress(10ms);
        require(completions == 1 && p.o.position == 10ms, "ineligible null stopped progressing");
    }
}
void notification_and_shutdown() {
    begin(); Player p; callbacks(p); p.initial(); p.add(pcm(80, 50));
    device_added(11);
    require(!p.o.reopen_pending, "healthy stream disrupted");
    device_removed(11);
    require(p.o.stream && p.o.removal_deadline != Clock::time_point{}, "migration grace removed");
    device_added(11);
    require(p.o.reopen_pending, "availability during grace lost");
    p.lose();
    require(p.retry(1s) == Reopen::opened, "grace hint not usable after fallback");
    p.lose();
    device_added(42); // Logical devices, including our own stream opens, must not cause retries.
    require(!p.o.reopen_pending && p.retry(3s) == Reopen::skipped, "logical event retry loop");
    device_added(11); mock::during_open = [&] { destroy(&p.o.object); };
    require(p.retry(4s) == Reopen::cancelled, "destroy during open");
    const auto opens = mock::opens;
    shutdown();
    const auto queries = mock::queries;
    device_added(11);
    require(p.retry(6s) == Reopen::skipped && mock::opens == opens && mock::quits == 1 &&
            mock::queries == queries,
            "shutdown left reopenable player");
}
void absolute_null_clock() {
    mock::reset(); configure({"null", {}});
    Player p; callbacks(p); p.initial();
    auto a = pcm(80, 20); // 10 ms at 8 kHz.
    p.add(a); p.add(a); p.add(a);
    std::unique_lock lock(p.o.mutex);
    prepare_null(p.o);
    advance_null(p.o, at(0ms), at(15ms)); // First deadline was 10 ms; callback is late.
    require(p.o.position == 10ms, "late callback advanced beyond buffer duration");
    complete(p.o, lock);
    prepare_null(p.o);
    advance_null(p.o, at(10ms), at(15ms)); // Carry the absolute deadline, not current wall time.
    require(p.o.position == 15ms && p.o.buffers.front().remaining == 5ms,
            "absolute null clock lost catch-up interval");
    advance_null(p.o, at(15ms), at(14ms)); // Negative clock input is clamped.
    require(p.o.position == 15ms, "negative null elapsed");
    advance_null(p.o, at(15ms), at(20ms));
    complete(p.o, lock);
    prepare_null(p.o); advance_null(p.o, at(20ms), at(35ms)); complete(p.o, lock);
    require(p.o.position == 30ms && completions == 3 && p.o.stats.submitted_bytes == 240,
            "absolute null completion/accounting total");
}
void volume_and_put_failure() {
    begin(); Player p; callbacks(p);
    p.o.channels = 2; p.o.container_bits = 16; p.o.endian = 1;
    mock::open_ok = false; p.initial();
    std::vector<uint8_t> a{0x12,0x34,0xed,0xcc,0x20,0,0xe0,0};
    p.add(a); device_added(11); mock::open_ok = true;
    require(p.retry(1s) == Reopen::opened && mock::last_spec.freq == 8000 &&
            mock::last_spec.channels == 2 && mock::last_spec.format == SDL_AUDIO_S16LE,
            "reopen changed format/channels");
    require(set_volume(&p.o.volume, -600) == success, "volume");
    p.submit();
    auto expected = scale_pcm(a, 16, true, linear_gain(-600, false));
    require(p.o.stream->bytes == expected, "reopen omitted gain/endian conversion");
    p.consume_input(8);
    p.add(a); mock::put_ok = false; p.submit();
    require(!p.o.stream && p.o.buffers.front().sent == 0 &&
            p.o.stats.submitted_bytes == 8, "failed put changed sent/tap watermark");
    require(set_mute(&p.o.volume, 1) == success, "mute");
    p.null_progress(250us);
    require(p.o.position == 500us && completions == 2 && p.o.stats.submitted_bytes == 16,
            "post-reopen loss lost position or completion");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: audio-reopen-tests OUTPUT_DIRECTORY");
        const std::filesystem::path dir(argv[1]);
        std::filesystem::create_directories(dir);
        partial_loss_reopen(dir);
        prefed_successor();
        failed_retries();
        pending_states();
        inflight_changes();
        reentrant_completion();
        excluded_backends();
        notification_and_shutdown();
        absolute_null_clock();
        volume_and_put_failure();
        std::cout << "audio reopen production-accounting fixture passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
