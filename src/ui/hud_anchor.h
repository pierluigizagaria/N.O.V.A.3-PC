#pragma once
#include <array>
#include <optional>
#include <string_view>
namespace nova::ui {
// NDC edges, shared by rendering and inverse input. Offsets from these edges
// scale with the HUD, retaining authored margins in reference pixels.
inline std::optional<std::array<float,2>> hud_edge(std::string_view name) {
    if(name=="shield") return std::array<float,2>{0,1};
    if(name=="hud_jetpack" || name=="hud_jeep") return std::array<float,2>{0,-1};
    if(name=="weapons" || name=="grenade_button") return std::array<float,2>{1,1};
    if(name=="pause_button" || name=="MP_scores_button" || name=="MP_pause_button")
        return std::array<float,2>{-1,1};
    return {};
}
}
