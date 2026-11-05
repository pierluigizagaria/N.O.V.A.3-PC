#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>
namespace nova::audio {
float linear_gain(int16_t millibels, bool mute);
// Canonical WAV/SDL input: unsigned 8-bit or signed 16-bit little endian.
std::vector<uint8_t> scale_pcm(std::span<const uint8_t> data, unsigned bits, bool big_endian,
                               float gain);
std::filesystem::path dump_path(const std::filesystem::path &requested);
class Wave {
    std::fstream file_;
    uint32_t bytes_ = 0;
    uint16_t channels_, bits_;
    uint32_t rate_;
    void header();

  public:
    Wave(const std::filesystem::path &, uint16_t channels, uint32_t rate, uint16_t bits);
    ~Wave();
    void append(std::span<const uint8_t>);
    void close();
};
} // namespace nova::audio
