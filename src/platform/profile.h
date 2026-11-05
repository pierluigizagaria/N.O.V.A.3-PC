#pragma once
#include <cstdint>
#include "platform/display_settings.h"
#include "platform/guest_extent.h"
#include "movement_wobble/movement_wobble.h"
#include "weapon_placement/position_policy.h"
#include "ui/scale.h"
#include "platform/native_detail.h"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
namespace nova::profile {
struct Config {
    int lod_distance=nova::platform::default_lod_distance;
    int texture_filtering=2;
    int ui_scale=ui::default_percent;
    nova::platform::DisplaySettings display;
    nova::platform::GuestExtent render_extent;
    weapon_placement::Position weapon_position; // Requested X/Y; original0 while unbound.
    std::string language;
    std::string graphics_backend = "d3d11";
    std::string audio = "sdl";
    std::map<std::string, std::string> hosts;
    bool net_log = false;
    // Real horizontal degrees at 16:9. Absence explicitly retains native optics.
    std::optional<float> world_fov;
    // Provisional canonical-reference weapon framing, independent of world optics.
    std::optional<float> weapon_fov;
    movement_wobble::Strength movement_wobble; // Original100; retained while feature is inactive.
};
Config read_config(const std::filesystem::path &file, std::filesystem::path &obb);
void configure(const Config &config);
const Config &config();
std::string get(const std::string &space, const std::string &key, const std::string &fallback = "");
void put(const std::string &space, const std::string &key, const std::string &value);
int64_t get_number(const std::string &space, const std::string &key, int64_t fallback);
void put_number(const std::string &space, const std::string &key, int64_t value);
bool first_launch(const std::string &key);
bool new_day(int64_t milliseconds);
std::string identifier(const std::string &name);
std::string language_for_locale(std::string locale);
std::string language();
std::string mac_digest();
} // namespace nova::profile
