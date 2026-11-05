#include "platform/preflight.h"
#include "bionic/runtime.h"
#include <stdexcept>
#include <string>
namespace nova::platform {
namespace {
constexpr std::string_view files[] = {
#include "bootstrap_files.inc"
};
} // namespace
std::span<const std::string_view> bootstrap_files() { return files; }
void preflight_profile() {
    std::string missing;
    unsigned count = 0;
    for (auto name : files) {
        auto guest =
            "/sdcard/Android/data/com.gameloft.android.ANMP.GloftN3HM/files/" + std::string(name);
        auto path = bionic::map_path(guest.c_str());
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) ||
            std::filesystem::file_size(path, error) == 0 || error) {
            if (count++ < 5)
                missing += (missing.empty() ? "" : ", ") + std::string(name);
        }
    }
    if (count)
        throw std::runtime_error("Profile is not prepared: " + std::to_string(count) +
                                 " missing/empty bootstrap files (" + missing +
                                 "). After input setup with --with-obb, run: "
                                 "python tools/setup_inputs.py --prepare-profile \"" +
                                 bionic::profile_directory().string() +
                                 "\". Prepared game data must stay under local/; select "
                                 "--profile-root local/profiles/<name> if needed.");
}
} // namespace nova::platform
