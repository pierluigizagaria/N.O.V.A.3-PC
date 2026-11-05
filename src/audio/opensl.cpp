#include "audio/opensl.h"
#include "abi/guest_call.h"
#include "audio/pcm.h"
#include "bionic/runtime.h"
#include <SDL3/SDL.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace nova::audio {
namespace {
using Word = uint32_t;
using Clock = std::chrono::steady_clock;
constexpr Word success = 0, preconditions = 1, invalid = 2, insufficient = 7, unsupported = 12;
constexpr Word stopped = 1, playing = 3;
struct Object;
struct Interface {
    const Word *table;
    Object *owner;
};
struct Source {
    const Word *locator;
    const Word *format;
};
struct Buffer {
    std::chrono::nanoseconds duration, remaining;
    std::vector<uint8_t> data;
    size_t sent = 0, used = 0;
};
struct Object {
    enum Kind { engine, mix, player } kind;
    Interface object{}, engine_itf{}, play{}, queue{}, volume{};
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    bool realized = false, dead = false;
    Word play_state = stopped, capacity = 2, completed = 0, epoch = 0;
    Word queue_callback = 0, queue_context = 0;
    Word channels = 2, rate = 44100000, container_bits = 16;
    std::deque<Buffer> buffers;
    std::chrono::nanoseconds position{};
    unsigned serial = 0;
    Word endian = 2;
    int16_t millibels = 0;
    bool mute = false, started = false;
    SDL_AudioStream *stream = nullptr;
    std::atomic<uint32_t> device{0}, underruns{0}, heartbeat{0};
    std::atomic<bool> removed{false};
    std::unique_ptr<Wave> wave;
    uint64_t submitted = 0, consumed = 0;
    Statistics stats;
    std::set<DWORD> enqueue_threads;
    std::vector<double> intervals;
    Clock::time_point last_callback{}, last_progress{}, removal_deadline{};
    std::string removed_device_name;
    bool reopen_eligible = false, reopen_pending = false;
    uint64_t reopen_revision = 0;
    Clock::time_point reopen_after{};
};
thread_local bool in_callback = false;
const void *engine_id = nullptr, *play_id = nullptr, *queue_id = nullptr;
const std::array<uint32_t, 4> volume_identity{0x56334f4e, 0, 0, 0};
const void *volume_id = volume_identity.data();
Options options;
std::string init_error;
std::atomic<bool> backend_ready{false};
unsigned next_player = 0;
std::mutex objects_mutex;
// Process lifetime matches the guest image. Destroy retires interfaces instead of leaving dangling
// pointers.
auto &objects = *new std::vector<std::unique_ptr<Object>>;
std::array<Word, 10> object_table;
std::array<Word, 16> engine_table;
std::array<Word, 12> play_table;
std::array<Word, 4> queue_table;
std::array<Word, 9> volume_table;
template <class F> Word pointer(F function) { return reinterpret_cast<Word>(function); }
[[noreturn]] Word unused_method(const char *name);
template <unsigned Slot> Word __cdecl object_trap() {
    constexpr const char *names[] = {"Realize",          "Resume",
                                     "GetState",         "GetInterface",
                                     "RegisterCallback", "AbortAsyncOperation",
                                     "Destroy",          "SetPriority",
                                     "GetPriority",      "SetLossOfControlInterfaces"};
    return unused_method(names[Slot]);
}
template <unsigned Slot> Word __cdecl play_trap() {
    constexpr const char *names[] = {"SetPlayState",
                                     "GetPlayState",
                                     "GetDuration",
                                     "GetPosition",
                                     "RegisterPlayCallback",
                                     "SetCallbackEventsMask",
                                     "GetCallbackEventsMask",
                                     "SetMarkerPosition",
                                     "ClearMarkerPosition",
                                     "GetMarkerPosition",
                                     "SetPositionUpdatePeriod",
                                     "GetPositionUpdatePeriod"};
    return unused_method(names[Slot]);
}
template <unsigned Slot> [[noreturn]] Word __cdecl engine_trap() {
    constexpr const char *names[] = {"CreateLEDDevice",
                                     "CreateVibraDevice",
                                     "CreateAudioPlayer",
                                     "CreateAudioRecorder",
                                     "CreateMidiPlayer",
                                     "CreateListener",
                                     "Create3DGroup",
                                     "CreateOutputMix",
                                     "CreateMetadataExtractor",
                                     "CreateExtensionObject",
                                     "QueryNumSupportedInterfaces",
                                     "QuerySupportedInterfaces",
                                     "QueryNumSupportedExtensions",
                                     "QuerySupportedExtension",
                                     "IsExtensionSupported",
                                     "UnsupportedSlot15"};
    std::fprintf(stderr, "unimplemented OpenSL engine method: %s\n", names[Slot]);
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 70);
    __assume(false);
}
// SDL calls this under its stream lock. Never take an object lock or enter guest code here.
void SDLCALL device_request(void *raw, SDL_AudioStream *, int additional, int) {
    auto &o = *static_cast<Object *>(raw);
    o.heartbeat.fetch_add(1, std::memory_order_relaxed);
    if (additional > 0)
        o.underruns.fetch_add(1, std::memory_order_relaxed);
    o.changed.notify_one();
}
void dump(Object &o, std::span<const uint8_t> bytes) {
    if (!o.wave)
        return;
    try {
        o.wave->append(bytes);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "OpenSL player=%u dump disabled: %s\n", o.serial, e.what());
        o.wave.reset();
    }
}
void fallback(Object &o, const std::string &reason) {
    std::fprintf(stderr, "OpenSL player=%u falling back to clocked null: %s\n", o.serial,
                 reason.c_str());
    ++o.stats.fallbacks;
    if (o.stream)
        SDL_DestroyAudioStream(o.stream);
    o.stream = nullptr;
    o.device = 0;
    o.started = false;
    for (auto &b : o.buffers) {
        b.remaining =
            std::chrono::nanoseconds(uint64_t(b.data.size() - b.used) * 1000000000000ull /
                                     (o.channels * (o.container_bits / 8) * uint64_t(o.rate)));
        // Bytes already submitted to SDL are already in the dump; do not duplicate them.
    }
}
// Called with the object lock; notification is only a coalesced availability hint.
void request_reopen(Object &o) {
    if (!o.dead && o.reopen_eligible &&
        (!o.stream || o.removed.load() || o.removal_deadline != Clock::time_point{})) {
        o.reopen_pending = true;
        o.changed.notify_all();
    }
}
bool pristine_queue(const Object &o) {
    return !o.buffers.empty() &&
           std::all_of(o.buffers.begin(), o.buffers.end(), [](const Buffer &b) {
               return b.sent == 0 && b.used == 0 && b.remaining == b.duration;
           });
}
enum class Reopen { skipped, failed, opened, cancelled };
Reopen try_reopen(Object &o, std::unique_lock<std::mutex> &lock, Clock::time_point now) {
    if (!backend_ready.load() || !o.reopen_eligible || !o.reopen_pending || o.stream ||
        o.dead || !o.realized ||
        o.play_state != playing || now < o.reopen_after || !pristine_queue(o))
        return Reopen::skipped;
    o.reopen_pending = false; // One attempt per hint, at most once per second.
    o.reopen_after = now + std::chrono::seconds(1);
    const auto epoch = o.epoch;
    const auto revision = o.reopen_revision;
    const SDL_AudioSpec spec{o.container_bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE,
                             static_cast<int>(o.channels), static_cast<int>(o.rate / 1000)};
    // OpenAudioDeviceStream returns paused. Guest state can change while opening;
    // no PCM is fed and the candidate is not published until it is revalidated.
    lock.unlock();
    auto *candidate = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                               device_request, &o);
    const auto device = candidate ? SDL_GetAudioStreamDevice(candidate) : 0;
    lock.lock();
    if (!backend_ready.load() || o.dead || o.epoch != epoch || o.reopen_revision != revision ||
        !o.realized || o.play_state != playing || !pristine_queue(o)) {
        if (candidate)
            SDL_DestroyAudioStream(candidate);
        if (!o.dead)
            o.reopen_pending = true; // A cancelled hint is retained, still rate limited.
        return Reopen::cancelled;
    }
    if (!candidate || !device) {
        if (candidate)
            SDL_DestroyAudioStream(candidate);
        // Do not call fallback again: null remaining/used have different meanings.
        // The caller must progress null PCM even if another hint arrived during open.
        return Reopen::failed;
    }
    o.stream = candidate;
    o.device = device;
    o.started = false;
    o.submitted = o.consumed = 0; // New empty SDL stream; lifetime tap counters survive.
    o.removed = false;
    o.removal_deadline = {};
    o.removed_device_name.clear();
    o.reopen_pending = false;
    o.last_progress = now;
    std::printf("OpenSL player=%u reopened default playback at untouched queue boundary\n",
                o.serial);
    return Reopen::opened;
}
void complete(Object &o, std::unique_lock<std::mutex> &lock) {
    o.buffers.pop_front();
    ++o.completed;
    ++o.stats.callbacks;
    auto now = Clock::now();
    if (o.last_callback != Clock::time_point{})
        o.intervals.push_back(
            std::chrono::duration<double, std::milli>(now - o.last_callback).count());
    o.last_callback = now;
    Word callback = o.queue_callback;
    Word args[] = {reinterpret_cast<Word>(&o.queue), o.queue_context};
    lock.unlock();
    in_callback = true;
    if (callback)
        abi::call_guest(callback, args, 2);
    in_callback = false;
    lock.lock();
}
void report(Object &o) {
    if (o.kind != Object::player)
        return;
    auto values = o.intervals;
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (auto v : values)
        sum += v;
    std::printf("OpenSL summary player=%u enqueued=%llu callbacks=%llu callback_enqueues=%llu "
                "submitted_bytes=%llu max_sdl_queued=%u underruns=%u fallbacks=%llu "
                "interval_ms mean=%.3f p50=%.3f p95=%.3f max=%.3f\n",
                o.serial, o.stats.enqueued, o.stats.callbacks, o.stats.callback_enqueues,
                o.stats.submitted_bytes, o.stats.max_queued_bytes, o.underruns.load(),
                o.stats.fallbacks, values.empty() ? 0 : sum / values.size(),
                values.empty() ? 0 : values[values.size() / 2],
                values.empty() ? 0 : values[(values.size() - 1) * 95 / 100],
                values.empty() ? 0 : values.back());
}
// These accounting steps are shared by the worker and the device-free fixture.
void account_sdl(Object &o, uint32_t queued) {
    uint64_t consumed = o.submitted - static_cast<uint32_t>(queued);
    auto delta = consumed - o.consumed;
    o.consumed = consumed;
    o.position +=
        std::chrono::nanoseconds(delta * 1000000000000ull /
                                 (o.channels * (o.container_bits / 8) * uint64_t(o.rate)));
    for (auto &b : o.buffers) {
        auto n = std::min<uint64_t>(delta, b.sent - b.used);
        b.used += static_cast<size_t>(n);
        delta -= n;
    }
}
void submit_sdl(Object &o, int queued) {
    // Limit SDL input to 20 ms even if a guest submits very large buffers.
    const size_t frame_bytes = o.channels * (o.container_bits / 8);
    const size_t limit = std::max<size_t>(1, o.rate / 1000 * 20 / 1000) * frame_bytes;
    size_t room = limit > static_cast<size_t>(queued) ? limit - queued : 0;
    for (auto &b : o.buffers) {
        size_t n = std::min(room, b.data.size() - b.sent);
        n -= n % frame_bytes;
        if (!n)
            continue;
        auto bytes = scale_pcm(std::span(b.data).subspan(b.sent, n), o.container_bits,
                               o.endian == 1, linear_gain(o.millibels, o.mute));
        if (!SDL_PutAudioStreamData(o.stream, bytes.data(),
                                    static_cast<int>(bytes.size()))) {
            fallback(o, SDL_GetError());
            break;
        }
        dump(o, bytes);
        b.sent += n;
        o.submitted += n;
        o.stats.submitted_bytes += n;
        room -= n;
        queued += static_cast<int>(n);
        o.stats.max_queued_bytes =
            std::max(o.stats.max_queued_bytes, static_cast<uint32_t>(queued));
    }
}
void prepare_null(Object &o) {
    auto &b = o.buffers.front();
    if (b.sent < b.data.size()) {
        auto bytes = scale_pcm(std::span(b.data).subspan(b.sent), o.container_bits,
                               o.endian == 1, linear_gain(o.millibels, o.mute));
        dump(o, bytes);
        o.stats.submitted_bytes += bytes.size();
        b.sent = b.data.size();
    }
}
void advance_null(Object &o, Clock::time_point start, Clock::time_point now) {
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start);
    auto &b = o.buffers.front();
    elapsed = std::clamp(elapsed, std::chrono::nanoseconds::zero(), b.remaining);
    o.position += elapsed;
    b.remaining -= elapsed;
}
void consume(Object *object) {
    // Adopt this host worker into the same errno/TLS lifecycle as guest pthreads.
    bionic::enter_host_thread();
    auto &o = *object;
    std::unique_lock lock(o.mutex);
    Clock::time_point cursor{};
    bool continuous = false;
    uint32_t heartbeat = 0;
    while (!o.dead) {
        if (o.play_state != playing || o.buffers.empty())
            continuous = false;
        if (!o.realized || o.play_state != playing || o.buffers.empty()) {
            o.changed.wait(lock, [&] {
                return o.dead || (o.realized && o.play_state == playing && !o.buffers.empty());
            });
            continue;
        }
        if (!o.stream) {
            const auto reopened = try_reopen(o, lock, Clock::now());
            if (reopened == Reopen::opened || reopened == Reopen::cancelled) {
                continuous = false;
                continue;
            }
            // Failed/skipped attempts fall through: never starve null progress with retries.
        }
        if (o.stream) {
            if (o.removal_deadline != Clock::time_point{} && Clock::now() >= o.removal_deadline) {
                o.removal_deadline = {};
                const char *current = SDL_GetAudioDeviceName(o.device.load());
                if (!current || o.removed_device_name == current) {
                    fallback(o, "physical default device removed without migration within 250 ms");
                    continuous = false;
                    continue;
                }
                o.reopen_pending = false;
                std::printf("OpenSL player=%u default device migrated to %s\n", o.serial, current);
            }
            int queued = SDL_GetAudioStreamQueued(o.stream);
            if (queued < 0 || o.removed.exchange(false)) {
                fallback(o, queued < 0 ? SDL_GetError() : "logical playback device removed");
                continuous = false;
                continue;
            }
            account_sdl(o, static_cast<uint32_t>(queued));
            // Retire only bytes SDL has removed from its input queue for the device.
            if (!o.buffers.empty() && o.buffers.front().used == o.buffers.front().data.size()) {
                complete(o, lock);
                continue;
            }
            auto pulse = o.heartbeat.load(std::memory_order_relaxed);
            if (pulse != heartbeat || !o.started) {
                heartbeat = pulse;
                o.last_progress = Clock::now();
            }
            if (o.started && Clock::now() - o.last_progress > std::chrono::milliseconds(500)) {
                fallback(o, "device stopped requesting PCM for 500 ms");
                continuous = false;
                continue;
            }
            submit_sdl(o, queued);
            if (o.stream && !o.started) {
                if (!SDL_ResumeAudioStreamDevice(o.stream))
                    fallback(o, SDL_GetError());
                else
                    o.started = true;
            }
            // Flush only the final finite tail: resampling may otherwise hold the last samples.
            // Continuous VOX refills before this condition and retains resampler continuity.
            if (o.stream && o.buffers.size() == 1 &&
                o.buffers.front().sent == o.buffers.front().data.size())
                SDL_FlushAudioStream(o.stream);
            o.changed.wait_for(lock, std::chrono::milliseconds(1));
        } else {
            auto epoch = o.epoch;
            auto &b = o.buffers.front();
            prepare_null(o);
            auto duration = b.remaining;
            auto start = continuous ? cursor : Clock::now();
            auto deadline = start + duration;
            o.changed.wait_until(lock, deadline, [&] {
                return o.dead || o.play_state != playing || o.epoch != epoch;
            });
            if (o.dead)
                break;
            if (o.epoch != epoch) {
                continuous = false;
                continue;
            }
            advance_null(o, start, Clock::now());
            if (o.buffers.front().remaining.count() > 0) {
                continuous = false;
                continue;
            }
            cursor = deadline;
            complete(o, lock);
            continuous = !o.buffers.empty() && o.epoch == epoch;
            // Reentrant enqueue keeps the absolute clock; reset after an actual starvation gap.
        }
    }
    if (o.stream) {
        SDL_DestroyAudioStream(o.stream);
        o.stream = nullptr;
        o.device = 0;
    }
    if (o.wave) {
        try {
            o.wave->close();
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Audio dump close: %s\n", e.what());
        }
        o.wave.reset();
    }
    report(o);
    lock.unlock();
    bionic::leave_host_thread();
}
Word __cdecl realize(Interface *self, Word async) {
    if (!self || async > 1)
        return invalid;
    auto &o = *self->owner;
    std::lock_guard lock(o.mutex);
    if (o.dead || o.realized)
        return preconditions;
    if (async)
        return unsupported; // Android's observed VOX path realizes synchronously.
    o.realized = true;
    o.changed.notify_all();
    return success;
}

Word __cdecl get_state(Interface *self, Word *state) {
    if (!self || !state)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    if (self->owner->dead)
        return preconditions;
    *state = self->owner->realized ? 2 : 1;
    return success;
}
Word __cdecl get_interface(Interface *self, const void *id, void **out) {
    if (!self || !out)
        return invalid;
    *out = nullptr;
    std::fprintf(stderr, "OpenSL GetInterface kind=%u id=%s thread=%lu\n", self->owner->kind,
                 id == engine_id   ? "ENGINE"
                 : id == play_id   ? "PLAY"
                 : id == queue_id  ? "BUFFERQUEUE"
                 : id == volume_id ? "VOLUME"
                                   : "unknown",
                 GetCurrentThreadId());
    auto &o = *self->owner;
    std::lock_guard lock(o.mutex);
    if (o.dead || !o.realized)
        return preconditions;
    if (id == engine_id && o.kind == Object::engine)
        *out = &o.engine_itf;
    else if (id == play_id && o.kind == Object::player)
        *out = &o.play;
    else if (id == queue_id && o.kind == Object::player)
        *out = &o.queue;
    else if (id == volume_id && o.kind == Object::player)
        *out = &o.volume;
    else
        return unsupported;
    return success;
}

void __cdecl destroy(Interface *self) {
    if (!self)
        return;
    auto &o = *self->owner;
    {
        std::lock_guard lock(o.mutex);
        o.dead = true;
        o.buffers.clear();
        ++o.epoch;
        o.changed.notify_all();
    }
    if (o.worker.joinable() && o.worker.get_id() != std::this_thread::get_id())
        o.worker.join();
}

Object *create_object(Object::Kind kind) {
    auto o = std::make_unique<Object>();
    o->kind = kind;
    o->object = {object_table.data(), o.get()};
    o->engine_itf = {engine_table.data(), o.get()};
    o->play = {play_table.data(), o.get()};
    o->queue = {queue_table.data(), o.get()};
    o->volume = {volume_table.data(), o.get()};
    auto result = o.get();
    std::lock_guard lock(objects_mutex);
    objects.push_back(std::move(o));
    return result;
}
Word check_interfaces(Object::Kind kind, Word count, const void *const *ids, const Word *required) {
    if (count && (!ids || !required))
        return invalid;
    for (Word i = 0; i < count; ++i) {
        std::fprintf(stderr, "OpenSL requested kind=%u interface=%s required=%u\n", kind,
                     ids[i] == engine_id   ? "ENGINE"
                     : ids[i] == play_id   ? "PLAY"
                     : ids[i] == queue_id  ? "BUFFERQUEUE"
                     : ids[i] == volume_id ? "VOLUME"
                                           : "unknown",
                     required[i]);
        bool known = (kind == Object::engine && ids[i] == engine_id) ||
                     (kind == Object::player &&
                      (ids[i] == play_id || ids[i] == queue_id || ids[i] == volume_id));
        if (required[i] && !known)
            return unsupported;
    }
    return success;
}
Word __cdecl create_mix(Interface *self, void **out, Word count, const void *const *ids,
                        const Word *required) {
    if (!self || !out)
        return invalid;
    *out = nullptr;
    Word result = check_interfaces(Object::mix, count, ids, required);
    if (result)
        return result;
    *out = &create_object(Object::mix)->object;
    return success;
}
// Shared initial backend selection; no thread is started here.
void open_initial(Object &o) {
    o.reopen_eligible = options.backend == "sdl" && backend_ready.load();
    if (options.backend == "sdl") {
        if (!backend_ready.load())
            fallback(o, init_error.empty() ? "audio subsystem unavailable" : init_error);
        else {
            SDL_AudioSpec spec{o.container_bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE,
                               static_cast<int>(o.channels), static_cast<int>(o.rate / 1000)};
            o.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                  device_request, &o);
            if (!o.stream)
                fallback(o, SDL_GetError());
            else {
                o.device = SDL_GetAudioStreamDevice(o.stream);
                SDL_AudioSpec device_spec{};
                int frames = 0;
                SDL_GetAudioDeviceFormat(o.device, &device_spec, &frames);
                std::printf(
                    "OpenSL player=%u SDL driver=%s device=%u format=0x%x channels=%d rate=%d "
                    "device_frames=%d device_ms=%.3f input_cap_ms=20 default_device_tracking=SDL\n",
                    o.serial, SDL_GetCurrentAudioDriver(), o.device.load(), device_spec.format,
                    device_spec.channels, device_spec.freq, frames,
                    device_spec.freq ? frames * 1000.0 / device_spec.freq : 0.0);
            }
        }
    }
}
Word __cdecl create_player(Interface *self, void **out, const Source *source, const Source *sink,
                           Word count, const void *const *ids, const Word *required) {
    if (!self || !out || !source || !source->locator || !source->format || !sink || !sink->locator)
        return invalid;
    *out = nullptr;
    Word result = check_interfaces(Object::player, count, ids, required);
    if (result)
        return result;
    auto loc = source->locator;
    auto pcm = source->format;
    if ((loc[0] != 6 && loc[0] != 0x800007bdu) || !loc[1] || loc[1] > 256 || pcm[0] != 2 ||
        !pcm[1] || pcm[1] > 2 || pcm[2] < 8000000 || pcm[2] > 192000000 || pcm[2] % 1000 ||
        (pcm[4] != 8 && pcm[4] != 16) || pcm[3] != pcm[4] || (pcm[6] != 1 && pcm[6] != 2) ||
        (pcm[5] && pcm[5] != (pcm[1] == 1 ? 4u : 3u)) || sink->locator[0] != 4) {
        std::fprintf(stderr,
                     "Unsupported OpenSL format: locator=%u capacity=%u format=%u channels=%u "
                     "rate=%u bits=%u container=%u sink=%u\n",
                     loc[0], loc[1], pcm[0], pcm[1], pcm[2], pcm[3], pcm[4], sink->locator[0]);
        return unsupported;
    }
    std::fprintf(
        stderr,
        "OpenSL PCM channels=%u rate_mHz=%u bits=%u container=%u mask=0x%x endian=%u capacity=%u\n",
        pcm[1], pcm[2], pcm[3], pcm[4], pcm[5], pcm[6], loc[1]);
    auto o = create_object(Object::player);
    o->capacity = loc[1];
    o->channels = pcm[1];
    o->rate = pcm[2];
    o->container_bits = pcm[4];
    o->endian = pcm[6];
    {
        std::lock_guard lock(objects_mutex);
        o->serial = ++next_player;
    }
    o->stats.players = 1;
    if (!options.dump.empty()) {
        auto path = options.dump;
        if (o->serial > 1)
            path.replace_filename(path.stem().string() + "-player" + std::to_string(o->serial) +
                                  ".wav");
        try {
            o->wave =
                std::make_unique<Wave>(path, static_cast<uint16_t>(o->channels), o->rate / 1000,
                                       static_cast<uint16_t>(o->container_bits));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Audio dump unavailable: %s\n", e.what());
            return 4;
        }
    }
    open_initial(*o);
    o->worker = std::thread(consume, o);
    *out = &o->object;
    std::printf("OpenSL player: channels=%u rate_mHz=%u bits=%u queue=%u\n", o->channels, o->rate,
                o->container_bits, o->capacity);
    return success;
}
Word __cdecl set_play_state(Interface *self, Word state) {
    if (!self || state < stopped || state > playing)
        return invalid;
    auto &o = *self->owner;
    std::lock_guard lock(o.mutex);
    if (o.dead || !o.realized)
        return preconditions;
    std::printf("OpenSL player=%u SetPlayState=%u thread=%lu\n", o.serial, state,
                GetCurrentThreadId());
    o.play_state = state;
    ++o.reopen_revision; // Also fence pause/resume pairs while an open is in flight.
    o.last_callback = {};
    o.last_progress = Clock::now();
    if (o.stream && state != playing) {
        SDL_PauseAudioStreamDevice(o.stream);
        o.started = false;
    }
    if (state == stopped) {
        o.position = {};
        if (o.stream)
            SDL_ClearAudioStream(o.stream);
        o.submitted = o.consumed = 0;
        for (auto &b : o.buffers) {
            b.remaining = b.duration;
            b.sent = b.used = 0;
        }
        ++o.epoch;
    }
    o.changed.notify_all();
    return success;
}
Word __cdecl get_play_state(Interface *self, Word *state) {
    if (!self || !state)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    *state = self->owner->play_state;
    return success;
}
Word __cdecl duration(Interface *, Word *value) {
    if (!value)
        return invalid;
    *value = UINT32_MAX; // Streaming queues have unknown duration.
    return success;
}
Word __cdecl position(Interface *self, Word *value) {
    if (!self || !value)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    *value = static_cast<Word>(
        std::chrono::duration_cast<std::chrono::milliseconds>(self->owner->position).count());
    return success;
}

Word __cdecl enqueue(Interface *self, const void *buffer, Word bytes) {
    if (!self || !buffer || !bytes)
        return invalid;
    auto &o = *self->owner;
    std::lock_guard lock(o.mutex);
    if (o.dead || !o.realized)
        return preconditions;
    Word frame_bytes = o.channels * (o.container_bits / 8);
    if (bytes % frame_bytes)
        return invalid;
    if (o.buffers.size() >= o.capacity)
        return insufficient;
    auto ns = std::chrono::nanoseconds((uint64_t(bytes / frame_bytes) * 1000000000000ull) / o.rate);
    if (o.completed < 4 || o.completed % 500 == 0)
        std::fprintf(stderr,
                     "OpenSL enqueue bytes=%u depth=%zu completed=%u thread=%lu in_callback=%d\n",
                     bytes, o.buffers.size(), o.completed, GetCurrentThreadId(), in_callback);
    if (o.enqueue_threads.insert(GetCurrentThreadId()).second)
        std::printf("OpenSL player=%u enqueue_thread=%lu in_callback=%d\n", o.serial,
                    GetCurrentThreadId(), in_callback);
    ++o.stats.enqueued;
    if (in_callback)
        ++o.stats.callback_enqueues;
    const auto *data = static_cast<const uint8_t *>(buffer);
    o.buffers.push_back({ns, ns, std::vector<uint8_t>(data, data + bytes)});
    o.changed.notify_all();
    return success;
}
Word __cdecl clear(Interface *self) {
    if (!self)
        return invalid;
    auto &o = *self->owner;
    std::lock_guard lock(o.mutex);
    o.buffers.clear();
    if (o.stream)
        SDL_ClearAudioStream(o.stream);
    o.submitted = o.consumed = 0;
    o.completed = 0;
    ++o.epoch;
    o.changed.notify_all();
    return success;
}
Word __cdecl queue_state(Interface *self, Word *state) {
    if (!self || !state)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    state[0] = static_cast<Word>(self->owner->buffers.size());
    state[1] = self->owner->completed;
    return success;
}
Word __cdecl queue_callback(Interface *self, Word callback, Word context) {
    if (!self)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    self->owner->queue_callback = callback;
    self->owner->queue_context = context;
    return success;
}
[[noreturn]] Word unused_method(const char *name) {
    std::fprintf(stderr, "unimplemented OpenSL method: %s\n", name);
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 70);
    __assume(false);
}
template <unsigned Slot> Word __cdecl volume_trap() {
    constexpr const char *names[] = {
        "SetVolumeLevel",   "GetVolumeLevel",       "GetMaxVolumeLevel",       "SetMute",
        "GetMute",          "EnableStereoPosition", "IsEnabledStereoPosition", "SetStereoPosition",
        "GetStereoPosition"};
    return unused_method(names[Slot]);
}
Word __cdecl set_volume(Interface *self, int16_t value) {
    if (!self || value > 0)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    if (self->owner->dead)
        return preconditions;
    self->owner->millibels = value;
    return success;
}
Word __cdecl get_volume(Interface *self, int16_t *value) {
    if (!self || !value)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    *value = self->owner->millibels;
    return success;
}
Word __cdecl max_volume(Interface *, int16_t *value) {
    if (!value)
        return invalid;
    *value = 0;
    return success;
}
Word __cdecl set_mute(Interface *self, Word value) {
    if (!self || value > 1)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    if (self->owner->dead)
        return preconditions;
    self->owner->mute = value != 0;
    return success;
}
Word __cdecl get_mute(Interface *self, Word *value) {
    if (!self || !value)
        return invalid;
    std::lock_guard lock(self->owner->mutex);
    *value = self->owner->mute;
    return success;
}
Word __cdecl create_engine(void **out, Word option_count, const Word *options, Word count,
                           const void *const *ids, const Word *required) {
    if (!out || (option_count && !options))
        return invalid;
    *out = nullptr;
    Word result = check_interfaces(Object::engine, count, ids, required);
    if (result)
        return result;
    for (Word i = 0; i < option_count; ++i)
        if (options[i * 2] != 1 && options[i * 2] != 2)
            return invalid;
    *out = &create_object(Object::engine)->object;
    bionic::log_once("OpenSL engine: synchronous PCM queue subset");
    return success;
}
template <size_t... I> void init_engine(std::index_sequence<I...>) {
    engine_table = {pointer(&engine_trap<I>)...};
}
} // namespace
void register_opensl(ImportRegistry &registry) {
    auto id = [&](const char *name) {
        auto binding = registry.find(name);
        if (!binding || binding->kind != ImportKind::data)
            throw std::runtime_error("OpenSL requires bionic interface identity: " +
                                     std::string(name));
        return *reinterpret_cast<const void *const *>(binding->address);
    };
    registry.data("SL_IID_VOLUME", &volume_id);
    engine_id = id("SL_IID_ENGINE");
    play_id = id("SL_IID_PLAY");
    queue_id = id("SL_IID_BUFFERQUEUE");
    object_table = {pointer(&realize),       pointer(&object_trap<1>), pointer(&get_state),
                    pointer(&get_interface), pointer(&object_trap<4>), pointer(&object_trap<5>),
                    pointer(&destroy),       pointer(&object_trap<7>), pointer(&object_trap<8>),
                    pointer(&object_trap<9>)};
    init_engine(std::make_index_sequence<16>{});
    engine_table[2] = pointer(&create_player);
    engine_table[7] = pointer(&create_mix);
    play_table = {pointer(&set_play_state), pointer(&get_play_state), pointer(&duration),
                  pointer(&position),       pointer(&play_trap<4>),   pointer(&play_trap<5>),
                  pointer(&play_trap<6>),   pointer(&play_trap<7>),   pointer(&play_trap<8>),
                  pointer(&play_trap<9>),   pointer(&play_trap<10>),  pointer(&play_trap<11>)};
    volume_table = {pointer(&set_volume),     pointer(&get_volume),     pointer(&max_volume),
                    pointer(&set_mute),       pointer(&get_mute),       pointer(&volume_trap<5>),
                    pointer(&volume_trap<6>), pointer(&volume_trap<7>), pointer(&volume_trap<8>)};
    queue_table = {pointer(&enqueue), pointer(&clear), pointer(&queue_state),
                   pointer(&queue_callback)};
    registry.function("slCreateEngine", reinterpret_cast<const void *>(&create_engine));
}
void configure(const Options &value) {
    if (value.backend != "sdl" && value.backend != "null")
        throw std::invalid_argument("Audio backend must be sdl or null");
    options = value;
    options.dump = dump_path(value.dump);
    init_error.clear();
    backend_ready = false;
    if (value.backend == "sdl") {
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            init_error = SDL_GetError();
        else
            backend_ready = true;
    }
}
void device_removed(uint32_t device) {
    if (!device || !backend_ready.load())
        return;
    const bool physical = SDL_IsAudioDevicePhysical(device);
    const char *name = physical ? SDL_GetAudioDeviceName(device) : nullptr;
    const std::string removed_name = name ? name : "";
    std::lock_guard lock(objects_mutex);
    for (auto &o : objects) {
        std::lock_guard state(o->mutex);
        auto logical = o->device.load();
        if (!logical)
            continue;
        if (logical == device)
            o->removed = true;
        else if (physical && !removed_name.empty()) {
            const char *current = SDL_GetAudioDeviceName(logical);
            if (!current || removed_name != current)
                continue;
            // SDL 3.2 retains default-bound logical devices on a clocked zombie,
            // emitting only a physical removal event. Allow normal migration first.
            // Provisional: equal friendly names conservatively fall back (see design notes).
            o->removed_device_name = removed_name;
            o->removal_deadline = Clock::now() + std::chrono::milliseconds(250);
        } else
            continue;
        o->changed.notify_all();
    }
}
void device_added(uint32_t device) {
    // Ignore logical-device notifications (including our own opens) and never
    // query an uninitialized backend. The event bridge filters recording devices.
    if (!device || !backend_ready.load() ||
        !SDL_IsAudioDevicePhysical(device))
        return;
    std::lock_guard lock(objects_mutex);
    for (auto &o : objects) {
        std::lock_guard state(o->mutex);
        request_reopen(*o);
    }
}
Statistics statistics() {
    Statistics out;
    std::lock_guard lock(objects_mutex);
    for (auto &o : objects) {
        std::lock_guard state(o->mutex);
        if (o->device.load())
            out.active_device = o->device.load();
        out.players += o->stats.players;
        out.enqueued += o->stats.enqueued;
        out.callbacks += o->stats.callbacks;
        out.fallbacks += o->stats.fallbacks;
        out.underruns += o->underruns.load();
        out.submitted_bytes += o->stats.submitted_bytes;
        out.callback_enqueues += o->stats.callback_enqueues;
        out.max_queued_bytes = std::max(out.max_queued_bytes, o->stats.max_queued_bytes);
    }
    return out;
}
void shutdown() {
    // Close retry admission before joining workers; SDL stays initialized until they retire.
    const bool quit_audio = backend_ready.exchange(false);
    std::vector<Object *> copy;
    {
        std::lock_guard lock(objects_mutex);
        for (auto &o : objects)
            copy.push_back(o.get());
    }
    for (auto *o : copy)
        destroy(&o->object);
    if (quit_audio)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
} // namespace nova::audio
