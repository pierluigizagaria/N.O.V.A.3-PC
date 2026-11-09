#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace nova::net::social {
inline constexpr uint32_t control_opcode = 0x4e335349;
bool receive(std::span<const uint8_t>);
bool poll_invitation();
std::string inviter();
void reset_notification();
void clear();
} // namespace nova::net::social
