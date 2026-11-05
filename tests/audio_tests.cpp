#include "abi/guest_call.h"
#include "audio/opensl.h"
#include "audio/pcm.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include "platform/preflight.h"
#include "platform/profile.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
using Word = uint32_t;
using Itf = void *;
using Clock = std::chrono::steady_clock;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> F method(Itf itf, unsigned slot) {
    return reinterpret_cast<F>((*static_cast<Word **>(itf))[slot]);
}
using State = Word(__cdecl *)(Itf, Word);
using Get = Word(__cdecl *)(Itf, const void *, Itf *);
using Query = Word(__cdecl *)(Itf, Word *);
using Queue = Word(__cdecl *)(Itf, const void *, Word);
using Callback = Word(__cdecl *)(Itf, void(__cdecl *)(Itf, void *), void *);
using Clear = Word(__cdecl *)(Itf);
using Destroy = void(__cdecl *)(Itf);
struct Player {
    Itf engine_object{}, engine{}, mix{}, object{}, play{}, queue{}, volume{};
    std::atomic<unsigned> count{0};
    static void __cdecl callback(Itf, void *raw) { ++static_cast<Player *>(raw)->count; }
    Player(nova::ImportRegistry &r) {
        auto id = [&](const char *name) {
            return *reinterpret_cast<const void **>(r.find(name)->address);
        };
        auto create =
            reinterpret_cast<Word(__cdecl *)(Itf *, Word, const void *, Word, const void *,
                                             const void *)>(r.find("slCreateEngine")->address);
        check(create(&engine_object, 0, nullptr, 0, nullptr, nullptr) == 0, "create engine");
        check(method<State>(engine_object, 0)(engine_object, 0) == 0, "realize engine");
        check(method<Get>(engine_object, 3)(engine_object, id("SL_IID_ENGINE"), &engine) == 0,
              "engine interface");
        using Mix = Word(__cdecl *)(Itf, Itf *, Word, const void *, const void *);
        check(method<Mix>(engine, 7)(engine, &mix, 0, nullptr, nullptr) == 0, "create mix");
        check(method<State>(mix, 0)(mix, 0) == 0, "realize mix");
        Word locator[] = {6, 2}, pcm[] = {2, 2, 44100000, 16, 16, 3, 2},
             output[] = {4, reinterpret_cast<Word>(mix)};
        const void *source[] = {locator, pcm}, *sink[] = {output, nullptr};
        const void *ids[] = {id("SL_IID_BUFFERQUEUE"), id("SL_IID_VOLUME")};
        Word required[] = {1, 1};
        using Create = Word(__cdecl *)(Itf, Itf *, const void *, const void *, Word, const void *,
                                       const void *);
        check(method<Create>(engine, 2)(engine, &object, source, sink, 2, ids, required) == 0,
              "create player");
        check(method<State>(object, 0)(object, 0) == 0, "realize player");
        check(method<Get>(object, 3)(object, id("SL_IID_PLAY"), &play) == 0, "play interface");
        check(method<Get>(object, 3)(object, id("SL_IID_BUFFERQUEUE"), &queue) == 0,
              "queue interface");
        check(method<Get>(object, 3)(object, id("SL_IID_VOLUME"), &volume) == 0,
              "volume interface");
        check(method<Callback>(queue, 3)(queue, callback, this) == 0, "register callback");
    }
    void state(Word s) { check(method<State>(play, 0)(play, s) == 0, "set play state"); }
    void put(const std::vector<uint8_t> &data) {
        check(method<Queue>(queue, 0)(queue, data.data(), static_cast<Word>(data.size())) == 0,
              "enqueue");
    }
    void clear() { check(method<Clear>(queue, 1)(queue) == 0, "clear"); }
    void wait(unsigned n) {
        auto until = Clock::now() + std::chrono::seconds(3);
        while (count < n && Clock::now() < until)
            SDL_Delay(1);
        check(count == n, "completion timeout/count");
    }
    void destroy() {
        if (object) {
            method<Destroy>(object, 6)(object);
            object = nullptr;
        }
    }
    ~Player() {
        destroy();
        if (mix)
            method<Destroy>(mix, 6)(mix);
        if (engine_object)
            method<Destroy>(engine_object, 6)(engine_object);
    }
};
std::vector<uint8_t> read(const std::filesystem::path &file) {
    std::ifstream f(file, std::ios::binary);
    check(bool(f), "open WAV");
    return {std::istreambuf_iterator<char>(f), {}};
}
Word le(const std::vector<uint8_t> &b, size_t offset, size_t count = 4) {
    Word v = 0;
    for (size_t i = 0; i < count; ++i)
        v |= Word(b.at(offset + i)) << (8 * i);
    return v;
}
std::vector<uint8_t> wav(const std::filesystem::path &path, Word bits, Word channels, Word rate) {
    auto b = read(path);
    check(b.size() >= 44, "WAV header size");
    check(std::string(b.begin(), b.begin() + 4) == "RIFF" &&
              std::string(b.begin() + 8, b.begin() + 16) == "WAVEfmt " &&
              std::string(b.begin() + 36, b.begin() + 40) == "data",
          "WAV tags");
    check(le(b, 4) == b.size() - 8 && le(b, 16) == 16 && le(b, 20, 2) == 1 &&
              le(b, 22, 2) == channels && le(b, 24) == rate &&
              le(b, 28) == rate * channels * bits / 8 && le(b, 32, 2) == channels * bits / 8 &&
              le(b, 34, 2) == bits,
          "WAV PCM metadata");
    auto n = le(b, 40);
    check(b.size() == 44 + n + (n & 1), "WAV data size and padding");
    return {b.begin() + 44, b.begin() + 44 + n};
}
void lifecycle(nova::ImportRegistry &registry, bool remove) {
    Player p(registry);
    std::vector<uint8_t> samples(44100 * 4 / 10, 0x11); // 100 ms
    p.put(samples);
    p.state(3);
    SDL_Delay(25);
    p.state(2);
    auto before = p.count.load();
    check(before == 0, "pause before completion");
    SDL_Delay(550);
    check(p.count == before, "pause freezes completion");
    auto resumed = Clock::now();
    p.state(3);
    p.wait(1);
    auto remaining = std::chrono::duration<double, std::milli>(Clock::now() - resumed).count();
    check(remaining > 35 && remaining < 180, "pause preserves remaining samples");
    check(nova::audio::statistics().fallbacks == 0, "long pause must not trigger device watchdog");
    p.put(samples);
    SDL_Delay(25);
    p.state(1);
    SDL_Delay(40);
    check(p.count == 1, "stop suppresses completion");
    resumed = Clock::now();
    p.state(3);
    p.wait(2);
    check(std::chrono::duration<double, std::milli>(Clock::now() - resumed).count() > 85,
          "stop rewinds retained front buffer");
    p.put(samples);
    SDL_Delay(20);
    p.clear();
    SDL_Delay(130);
    check(p.count == 2, "clear cancels queued completion");
    Word state[2];
    check(method<Query>(p.queue, 2)(p.queue, state) == 0 && state[0] == 0 && state[1] == 0,
          "clear resets queue index");
    if (remove) {
        p.put(samples);
        SDL_Delay(20);
        auto device = nova::audio::statistics().active_device;
        check(device != 0, "SDL logical device");
        nova::audio::device_removed(device);
        p.wait(3);
        check(nova::audio::statistics().fallbacks == 1, "device removal falls back once");
        p.put(samples);
        p.wait(4);
        check(nova::audio::statistics().fallbacks == 1, "fallback remains clocked null");
    }
    auto n = p.count.load();
    p.put(samples);
    SDL_Delay(20);
    p.destroy();
    SDL_Delay(120);
    check(p.count == n, "destroy cancels pending callbacks");
}
void volume(nova::ImportRegistry &registry, const std::filesystem::path &path) {
    check(std::abs(nova::audio::linear_gain(-600, false) - 0.5011872f) < 0.000001f,
          "millibel gain");
    check(nova::audio::linear_gain(INT16_MIN, false) == 0 && nova::audio::linear_gain(0, true) == 0,
          "mute/minimum gain");
    auto odd_path = path.parent_path() / "odd.wav";
    {
        nova::audio::Wave odd(odd_path, 1, 22050, 8);
        const uint8_t samples[] = {0, 128, 255};
        odd.append(samples);
        odd.close();
    }
    check(wav(odd_path, 8, 1, 22050) == std::vector<uint8_t>({0, 128, 255}),
          "odd-length WAV padding");
    Player p(registry);
    using SetVolume = Word(__cdecl *)(Itf, int16_t);
    using GetVolume = Word(__cdecl *)(Itf, int16_t *);
    check(method<SetVolume>(p.volume, 0)(p.volume, 1) == 2, "positive volume rejected");
    check(method<SetVolume>(p.volume, 0)(p.volume, -600) == 0, "set millibel volume");
    int16_t value = 0;
    check(method<GetVolume>(p.volume, 1)(p.volume, &value) == 0 && value == -600,
          "volume round trip");
    check(method<GetVolume>(p.volume, 2)(p.volume, &value) == 0 && value == 0, "max volume");
    std::vector<uint8_t> samples(1764);
    for (size_t i = 0; i < samples.size(); i += 4) {
        samples[i] = 0x10;
        samples[i + 1] = 0x27;
        samples[i + 2] = 0xf0;
        samples[i + 3] = 0xd8;
    }
    p.put(samples);
    p.state(3);
    p.wait(1);
    p.state(2);
    check(method<State>(p.volume, 3)(p.volume, 1) == 0, "set mute");
    Word mute = 0;
    check(method<Query>(p.volume, 4)(p.volume, &mute) == 0 && mute == 1, "mute query");
    p.put(samples);
    p.state(3);
    p.wait(2);
    p.destroy();
    auto data = wav(path, 16, 2, 44100);
    check(data.size() == samples.size() * 2, "dump exact submitted frames");
    for (size_t i = 0; i < samples.size(); i += 4)
        check(data[i] == 0x94 && data[i + 1] == 0x13 && data[i + 2] == 0x6c && data[i + 3] == 0xec,
              "gain applied to PCM dump");
    for (size_t i = samples.size(); i < data.size(); ++i)
        check(data[i] == 0, "muted dump is silence");
}
void preflight(const std::filesystem::path &directory) {
    check(nova::platform::bootstrap_files().size() == 40, "complete bootstrap inventory");
    auto base = directory / "sdcard/Android/data/com.gameloft.android.ANMP.GloftN3HM/files";
    // Clean only explicitly enumerated synthetic files in this isolated test profile.
    for (auto name : nova::platform::bootstrap_files())
        std::filesystem::remove(base / std::string(name));
    auto missing = [] {
        try {
            nova::platform::preflight_profile();
            return false;
        } catch (const std::runtime_error &e) {
            return std::string(e.what()).find("setup_inputs.py --prepare-profile") !=
                   std::string::npos;
        }
    };
    check(missing(), "unprepared profile has actionable error");
    for (auto name : nova::platform::bootstrap_files()) {
        std::ofstream out(base / std::string(name));
        out << "synthetic";
    }
    nova::platform::preflight_profile();
    {
        std::ofstream out(base / "eventParameters.xml", std::ios::trunc);
    }
    check(missing(), "empty required file rejected");
    {
        std::ofstream out(base / "eventParameters.xml");
        out << "synthetic";
    }
    std::filesystem::remove(base / "sounds_hi_p5.gla");
    check(missing(), "late bootstrap file required");
    auto conf = directory / "test.conf";
    std::filesystem::path obb;
    {
        std::ofstream out(conf);
        out << "audio=null\n";
    }
    check(nova::profile::read_config(conf, obb).audio == "null", "audio config");
    {
        std::ofstream out(conf);
        out << "audio=invalid\n";
    }
    bool rejected = false;
    try {
        nova::profile::read_config(conf, obb);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "invalid audio config rejected");
    rejected = false;
    try {
        nova::audio::dump_path("outside.wav");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "dump restricted to local");
    rejected = false;
    try {
        nova::audio::dump_path("local/../outside.wav");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "dump traversal rejected");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    try {
        std::string mode = argv[2];
        auto directory = std::filesystem::absolute(argv[3]);
        nova::bionic::PathConfig paths;
        paths.profile_root = directory;
        nova::bionic::configure_paths(paths);
        auto dump = std::filesystem::path(NOVA_SOURCE_DIR) / "local/audio/tests" /
                    directory.parent_path().filename() / directory.filename() / (mode + ".wav");
        if (mode == "preflight") {
            preflight(directory);
            std::puts("PASS audio-preflight");
            return 0;
        }
        check(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIO_DRIVER",
                                         mode == "fallback" ? "nova_missing_driver" : "dummy",
                                         true),
              "select dummy driver");
        // SDL dummy truncates its delay to integer milliseconds. Exact 10 ms periods
        // avoid measuring that driver rounding as port drift (48000 Hz in T04, 44100 otherwise).
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, mode == "t04" ? "480" : "441",
                                SDL_HINT_OVERRIDE);
        nova::audio::configure({mode == "null-stream" ? "null" : "sdl", dump});
        nova::ImportRegistry registry;
        nova::bionic::register_boot(registry);
        nova::audio::register_opensl(registry);
        nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(argv[1], registry);
        nova::Diagnostics diagnostics(image);
        struct AudioScope {
            ~AudioScope() { nova::audio::shutdown(); }
        } audio_scope;
        if (mode == "lifecycle" || mode == "removed")
            lifecycle(registry, mode == "removed");
        else if (mode == "physical-removed") {
            Player p(registry);
            std::vector<uint8_t> samples(44100 * 4, 0x22);
            p.put(samples);
            p.state(3);
            SDL_Delay(20);
            int count = 0;
            auto devices = SDL_GetAudioPlaybackDevices(&count);
            check(devices && count == 1, "dummy physical device inventory");
            auto physical = devices[0];
            SDL_free(devices);
            nova::audio::device_removed(physical);
            SDL_Delay(100);
            check(nova::audio::statistics().fallbacks == 0, "allow default migration grace period");
            p.wait(1);
            check(nova::audio::statistics().fallbacks == 1 &&
                      nova::audio::statistics().active_device == 0,
                  "physical removal retires default-bound zombie stream");
        } else if (mode == "volume")
            volume(registry, dump);
        else if (mode == "t04") {
            *reinterpret_cast<Word *>(image.lookup("fixture_audio_timing_margin")) = 15000;
            auto result = nova::abi::call0(image.lookup("fixture_audio"));
            if (result)
                throw std::runtime_error("T04 media.c:" + std::to_string(result));
        } else if (mode == "trap") {
            Player p(registry);
            method<Clear>(p.volume, 5)(p.volume);
            throw std::runtime_error("trap returned");
        } else {
            unsigned serial = 0;
            auto test = [&](Word bits, Word channels, Word rate, Word endian, Word slow) {
                Word args[] = {bits, channels, rate, endian, slow};
                auto result = nova::abi::call_guest(image.lookup("fixture_audio_stream"), args, 5);
                if (result)
                    throw std::runtime_error("synthetic audio.c:" + std::to_string(result));
                auto file = dump;
                if (++serial > 1)
                    file.replace_filename(file.stem().string() + "-player" +
                                          std::to_string(serial) + ".wav");
                auto data = wav(file, bits, channels, rate);
                size_t bytes = (rate / 100) * channels * bits / 8;
                check(data.size() == bytes * 120, "stream dump frame count");
                for (size_t n = 0; n < 120; ++n)
                    for (size_t i = 0; i < bytes; ++i) {
                        auto index = bits == 16 && endian == 1 ? i ^ 1u : i;
                        check(data[n * bytes + i] == static_cast<uint8_t>(n * 7 + index * 13),
                              "callback FIFO/dump PCM mismatch");
                    }
            };
            if (mode == "formats") {
                for (Word bits : {8u, 16u})
                    for (Word channels : {1u, 2u})
                        for (Word rate : {22050u, 44100u})
                            test(bits, channels, rate, 2, 0);
                test(16, 2, 44100, 1, 0);
            } else
                test(16, 2, 44100, 2, mode == "slow" ? 1 : 0);
            auto stats = nova::audio::statistics();
            check(stats.callbacks == 120 * serial && stats.callback_enqueues == 118 * serial,
                  "guest callback accounting");
            if (mode == "fallback")
                check(stats.fallbacks == 1 && stats.max_queued_bytes == 0,
                      "failed SDL init uses null");
            else if (mode != "null-stream")
                check(stats.fallbacks == 0 && stats.max_queued_bytes <= 3528, "bounded SDL queue");
            if (mode == "slow")
                check(stats.underruns > 0,
                      "slow guest produces counted silence without device stall");
        }
        nova::audio::shutdown();
        nova::bionic::stream_flush(nullptr);
        std::printf("PASS audio-%s\n", mode.c_str());
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "audio test: %s\n", e.what());
        nova::audio::shutdown();
        return 1;
    }
}
