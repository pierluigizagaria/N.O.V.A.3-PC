#pragma once
#include <span>
#include <string_view>
namespace nova::platform {
std::span<const std::string_view> bootstrap_files();
// Runs before ELF mapping/initializers. Uses the same checked guest path mapping as the game.
void preflight_profile();
} // namespace nova::platform
