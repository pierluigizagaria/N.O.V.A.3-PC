#pragma once
// Host settings model and explicit transactional writer.
#include "camera/native_world_fov.h"
#include "input/controls.h"
#include <filesystem>
#include <functional>
#include "platform/display_settings.h"
#include "platform/guest_extent.h"
#include "movement_wobble/movement_wobble.h"
#include "weapon_placement/position_policy.h"
#include "ui/scale.h"
#include "platform/graphics_quality.h"
#include <string>
#include <string_view>
#include <windows.h>

namespace nova::platform {
using FovOptions = camera::NativeWorldFov::Options;
// Pure text operation: validates every effective parser line, edits every FOV
// occurrence, and retains unrelated bytes and each existing line terminator.
std::string merge_fov_config(std::string_view original, const FovOptions &options);
struct FovSaveResult {
    bool saved = false;
    uint64_t visible_revision = 0;
    std::string error;
};
// Each filesystem effect is replaceable before Save; tests use an in-memory
// handle/path model and never pass an actual player config to these functions.
struct FovConfigIo {
    decltype(&CreateFileW) open=&CreateFileW;
    decltype(&GetFileInformationByHandle) information=&GetFileInformationByHandle;
    decltype(&ReadFile) read=&ReadFile;
    decltype(&WriteFile) write=&WriteFile;
    decltype(&FlushFileBuffers) flush=&FlushFileBuffers;
    decltype(&CloseHandle) close=&CloseHandle;
    decltype(&ReplaceFileW) replace=&ReplaceFileW;
    decltype(&MoveFileExW) move=&MoveFileExW;
    decltype(&DeleteFileW) remove=&DeleteFileW;
    decltype(&GetLastError) error=&GetLastError;
};
// Save reads the current file at the moment of the explicit user action. It
// never uses a startup text snapshot or mutates profile::config/preferences.
FovSaveResult save_fov_config(const std::filesystem::path &path,
                             const FovOptions &visible, uint64_t revision,
                             const FovConfigIo *injected=nullptr);
std::string merge_display_config(std::string_view,const DisplaySettings &);
FovSaveResult save_display_config(const std::filesystem::path &,const DisplaySettings &,
                                 const FovConfigIo *injected=nullptr);
std::string merge_movement_wobble_config(std::string_view,movement_wobble::Strength);
std::string merge_ui_scale_config(std::string_view,int);
std::string merge_graphics_quality_config(std::string_view,GraphicsQuality);
FovSaveResult save_graphics_quality_config(const std::filesystem::path &,GraphicsQuality,const FovConfigIo *injected=nullptr);
FovSaveResult save_ui_scale_config(const std::filesystem::path &,int,const FovConfigIo *injected=nullptr);
FovSaveResult save_movement_wobble_config(const std::filesystem::path &,movement_wobble::Strength,
                                        const FovConfigIo *injected=nullptr);
std::string merge_render_config(std::string_view,GuestExtent);
FovSaveResult save_render_config(const std::filesystem::path &,GuestExtent,const FovConfigIo *injected=nullptr);
// Separate EXE-adjacent controls.conf. Preserve scalar options and comments;
// replace the complete binding set so removed defaults cannot return on restart.
std::string merge_controls_config(std::string_view,const input::ControlsConfig &);
FovSaveResult save_controls_config(const std::filesystem::path &,const input::ControlsConfig &,
                                  const FovConfigIo *injected=nullptr);
// Sensitivity edits own both concrete values, preserving all binding bytes.
std::string merge_mouse_sensitivity(std::string_view,input::MouseSensitivity);
// Retry can include either failed section; both sections share one transaction.
FovSaveResult save_controls_preferences(const std::filesystem::path &,const input::ControlsConfig &,
    bool bindings,bool sensitivity,const FovConfigIo *injected=nullptr);
// Independent X/Y transaction; never serializes stale FOV/Display/Controls fields.
std::string merge_weapon_position_config(std::string_view original,weapon_placement::Position);
FovSaveResult save_weapon_position_config(const std::filesystem::path &,weapon_placement::Position,
                                         const FovConfigIo *injected=nullptr);
} // namespace nova::platform
