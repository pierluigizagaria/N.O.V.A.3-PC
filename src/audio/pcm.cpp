#include "audio/pcm.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace nova::audio {
float linear_gain(int16_t millibels, bool mute) {
    return mute || millibels == INT16_MIN ? 0.0f : std::pow(10.0f, millibels / 2000.0f);
}
std::vector<uint8_t> scale_pcm(std::span<const uint8_t> data, unsigned bits, bool big, float gain) {
    std::vector<uint8_t> out(data.size());
    if (bits == 8) {
        for (size_t i = 0; i < data.size(); ++i)
            out[i] = static_cast<uint8_t>(
                std::clamp(std::lround((int(data[i]) - 128) * gain) + 128, 0L, 255L));
    } else {
        for (size_t i = 0; i < data.size(); i += 2) {
            auto raw = big ? (uint16_t(data[i]) << 8) | data[i + 1]
                           : data[i] | (uint16_t(data[i + 1]) << 8);
            auto value = static_cast<int16_t>(
                std::clamp(std::lround(static_cast<int16_t>(raw) * gain), -32768L, 32767L));
            out[i] = static_cast<uint8_t>(value);
            out[i + 1] = static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8);
        }
    }
    return out;
}
std::filesystem::path dump_path(const std::filesystem::path &requested) {
    if (requested.empty())
        return {};
    auto local = (std::filesystem::current_path() / "local").lexically_normal();
    auto path = std::filesystem::absolute(requested).lexically_normal();
    auto a = local.begin(), b = path.begin();
    for (; a != local.end() && b != path.end(); ++a, ++b)
        if (_wcsicmp(a->c_str(), b->c_str()) != 0)
            break;
    if (a != local.end() || b == path.end() || path.extension() != ".wav")
        throw std::invalid_argument("--audio-dump requires a .wav file under local/");
    for (auto i = b; i != path.end(); ++i) {
        auto name = i->wstring();
        if (name.empty() || name.back() == L'.' || name.back() == L' ' ||
            name.find(L':') != std::wstring::npos)
            throw std::invalid_argument("Invalid Windows audio dump component");
    }
    // Lexical containment plus rejection of all existing reparse components avoids escapes.
    auto part = std::filesystem::absolute(requested).lexically_normal();
    for (; !part.empty() && part != part.root_path(); part = part.parent_path()) {
        auto attributes = GetFileAttributesW(part.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("Audio dump path contains a reparse point");
    }
    return path;
}
Wave::Wave(const std::filesystem::path &path, uint16_t channels, uint32_t rate, uint16_t bits)
    : channels_(channels), bits_(bits), rate_(rate) {
    std::filesystem::create_directories(path.parent_path());
    file_.open(path, std::ios::binary | std::ios::out | std::ios::in | std::ios::trunc);
    if (!file_)
        throw std::runtime_error("Cannot open audio dump: " + path.string());
    header();
}
void Wave::header() {
    std::array<char, 44> h{};
    auto put = [&](int at, uint32_t value, int n) {
        for (int i = 0; i < n; ++i)
            h[at + i] = static_cast<char>(value >> (8 * i));
    };
    std::copy_n("RIFF", 4, h.data());
    put(4, bytes_ + 36 + (bytes_ & 1), 4);
    std::copy_n("WAVEfmt ", 8, h.data() + 8);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, channels_, 2);
    put(24, rate_, 4);
    put(28, rate_ * channels_ * bits_ / 8, 4);
    put(32, channels_ * bits_ / 8, 2);
    put(34, bits_, 2);
    std::copy_n("data", 4, h.data() + 36);
    put(40, bytes_, 4);
    file_.seekp(0);
    file_.write(h.data(), h.size());
    file_.seekp(0, std::ios::end);
    if (!file_)
        throw std::runtime_error("Cannot write audio dump header");
}
void Wave::append(std::span<const uint8_t> data) {
    if (data.size() > UINT32_MAX - 36u - bytes_)
        throw std::runtime_error("Audio dump exceeds RIFF size limit");
    file_.write(reinterpret_cast<const char *>(data.data()),
                static_cast<std::streamsize>(data.size()));
    if (!file_)
        throw std::runtime_error("Cannot write audio dump PCM");
    bytes_ += static_cast<uint32_t>(data.size());
}
void Wave::close() {
    if (file_.is_open()) {
        if (bytes_ & 1) {
            file_.seekp(44ull + bytes_);
            file_.put(0);
        }
        header();
        file_.flush();
        if (!file_)
            throw std::runtime_error("Cannot finalize audio dump");
        file_.close();
    }
}
Wave::~Wave() {
    try {
        close();
    } catch (...) {
    }
}
} // namespace nova::audio
