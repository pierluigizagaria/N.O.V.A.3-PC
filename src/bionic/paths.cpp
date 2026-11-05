#include "bionic/runtime.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
namespace nova::bionic {
namespace {
PathConfig paths;
bool configured = false;
std::mutex case_guard;
std::unordered_map<std::string, std::string> case_names;
constexpr auto package = "com.gameloft.android.ANMP.GloftN3HM";
bool safe_component(const std::string &s) {
    if (s.empty() || s == "." || s == ".." || s.back() == '.' || s.back() == ' ')
        return false;
    if (s.find_first_of(":\\<>\"|?*") != std::string::npos)
        return false;
    for (unsigned char c : s)
        if (c < 32)
            return false;
    auto base = s.substr(0, s.find('.'));
    for (auto &c : base)
        c = static_cast<char>(toupper(c));
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" ||
        base == "CONOUT$")
        return false;
    if (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '0' &&
        base[3] <= '9')
        return false;
    return true;
}
bool reparse(const std::filesystem::path &p) {
    std::filesystem::path current;
    for (const auto &part : p) {
        current /= part;
        DWORD attr = GetFileAttributesW(current.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT))
            return true;
    }
    return false;
}
} // namespace
void configure_paths(const PathConfig &config) {
    if (!safe_component(config.profile))
        throw std::runtime_error("Invalid profile name");
    paths = config;
    if (paths.profile_root.empty()) {
        wchar_t local[32768];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (!n || n >= 32768)
            throw std::runtime_error("LOCALAPPDATA is unavailable");
        paths.profile_root = std::filesystem::path(local) / L"Nova3PC" / config.profile;
    }
    paths.profile_root = std::filesystem::absolute(paths.profile_root).lexically_normal();
    if (!paths.obb.empty())
        paths.obb = std::filesystem::absolute(paths.obb).lexically_normal();
    if (reparse(paths.profile_root) || (!paths.obb.empty() && reparse(paths.obb)))
        throw std::runtime_error("Reparse point in configured root");
    for (const auto &dir : {paths.profile_root / "data" / "files", paths.profile_root / "data" / "cache",
                            paths.profile_root / "sdcard" / "Android" / "data" / package / "files"})
        std::filesystem::create_directories(dir);
    configured = true;
}
std::filesystem::path profile_directory() {
    if (!configured)
        configure_paths({});
    return paths.profile_root;
}
std::filesystem::path map_path(const char *input, bool writing) {
    if (!configured)
        configure_paths({});
    std::string name = input ? input : "";
    // Android apps inherit cwd=/; this library never imports chdir. Its VFS
    // rewrites /sdcard paths to ./sdcard before passing them to libc.
    while (name.starts_with("./"))
        name.erase(0, 2);
    if (!name.empty() && name.front() != '/')
        name.insert(name.begin(), '/');
    auto deny = [&] {
        log_once("unmapped/denied path: " + name);
        fail(2);
        return std::filesystem::path{};
    };
    const std::string obb = "/sdcard/Android/obb/" + std::string(package);
    const std::string data = "/data/data/" + std::string(package);
    auto matches = [&](const std::string &prefix) {
        return name == prefix || name.starts_with(prefix + "/");
    };
    std::filesystem::path result;
    size_t start = 0;
    if (matches(obb)) {
        if (writing) {
            fail(30);
            return {};
        }
        if (paths.obb.empty())
            return deny();
        result = paths.obb;
        start = obb.size();
    } else if (matches(data)) {
        result = paths.profile_root / "data";
        start = data.size();
    } else if (matches("/sdcard")) {
        result = paths.profile_root / "sdcard";
        start = 7;
    } else
        return deny();
    while (start < name.size()) {
        if (name[start] == '/') {
            ++start;
            continue;
        }
        auto end = name.find('/', start);
        if (end == std::string::npos)
            end = name.size();
        auto part = name.substr(start, end - start);
        if (!safe_component(part))
            return deny();
        result /= std::filesystem::path(std::u8string(part.begin(), part.end()));
        start = end;
    }
    if (reparse(result))
        return deny();
    {
        auto folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](unsigned char c) { return static_cast<char>(tolower(c)); });
        std::lock_guard lock(case_guard);
        auto [it, inserted] = case_names.emplace(folded, name);
        if (!inserted && it->second != name)
            log_once("case-sensitive guest paths alias on Windows: " + it->second + " / " + name);
    }
    return result;
}
} // namespace nova::bionic
