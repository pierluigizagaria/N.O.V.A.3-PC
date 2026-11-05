#pragma once
#include <cmath>
#include <optional>
#include <stdexcept>

namespace nova::input {
constexpr float default_mouse_sensitivity = 0.10f;
// User-facing FPS units. Persistence and native input retain degrees/count, so
// opening the panel never changes the feel of an existing saved preference.
constexpr float mouse_degrees_per_fps_unit = 0.022f;
constexpr float mouse_fps_min = 0.10f, mouse_fps_max = 10.00f;
constexpr float aim_multiplier_min = 0.f, aim_multiplier_max = 2.f;
inline float mouse_fps_units(float degrees_per_count) {
    return degrees_per_count / mouse_degrees_per_fps_unit;
}
inline float mouse_degrees_per_count(float fps_units) {
    return fps_units * mouse_degrees_per_fps_unit;
}
struct MouseLookConfig {
    float sensitivity = default_mouse_sensitivity; // Degrees per relative mouse count.
    bool invert_y = false;
};
struct MouseSensitivity {
    float normal = default_mouse_sensitivity, aim_multiplier = 1.f;
    bool operator==(const MouseSensitivity &) const = default;
};
inline bool valid_sensitivity(float value) {
    return std::isfinite(value) && value > 0 && value <= 10;
}
inline bool valid_sensitivity(MouseSensitivity value) {
    return valid_sensitivity(value.normal) && std::isfinite(value.aim_multiplier) &&
        value.aim_multiplier>=aim_multiplier_min && value.aim_multiplier<=aim_multiplier_max;
}
// UI-owned completion model. The last valid pair survives tab/window/close;
// rejected edits and idle frames never emit settings or persistence requests.
class MouseSensitivityEdit {
    MouseSensitivity value_;
    bool dirty_=false,ready_=false;
  public:
    explicit MouseSensitivityEdit(MouseSensitivity value={}) : value_(value) {
        if(!valid_sensitivity(value)) throw std::invalid_argument("Invalid mouse sensitivity");
    }
    MouseSensitivity value() const { return value_; }
    bool edit(MouseSensitivity value) {
        if(!valid_sensitivity(value)) return false;
        if(value!=value_) { value_=value; dirty_=true; }
        return true;
    }
    void finish() { ready_|=dirty_; dirty_=false; }
    void reset() { value_={}; dirty_=true; finish(); }
    std::optional<MouseSensitivity> take() {
        if(!ready_ || dirty_) return {};
        ready_=false; return value_;
    }
};
}
