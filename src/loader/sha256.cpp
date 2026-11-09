#include "loader/sha256.h"
#include <Windows.h>
#include <array>
#include <bcrypt.h>
#include <limits>
#include <stdexcept>

namespace nova {
std::string sha256(std::span<const uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("SHA256 input exceeds provider limit");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open SHA256 provider");
    std::array<uint8_t, 32> digest{};
    const auto result = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                                   static_cast<ULONG>(bytes.size()), digest.data(),
                                   static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0)
        throw std::runtime_error("Cannot compute SHA256");
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (auto byte : digest) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}
} // namespace nova
