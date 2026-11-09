#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace nova {
std::string sha256(std::span<const uint8_t> bytes);
}
