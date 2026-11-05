#include "platform/profile.h"
#include "bionic/runtime.h"
#include <algorithm>
#include <bcrypt.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace nova::profile {
namespace {
Config settings;
std::mutex guard;
using Values = std::map<std::pair<std::string, std::string>, std::string>;
std::string hex(const unsigned char *p, size_t n) {
    const char *digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += digits[p[i] >> 4];
        out += digits[p[i] & 15];
    }
    return out;
}
std::string encode(const std::string &s) {
    return s.empty() ? "-" : hex(reinterpret_cast<const unsigned char *>(s.data()), s.size());
}
std::string decode(const std::string &s) {
    if (s == "-")
        return {};
    if (s.size() % 2)
        throw std::runtime_error("Invalid profile hex field");
    std::string result;
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned value = 0;
        auto r = std::from_chars(s.data() + i, s.data() + i + 2, value, 16);
        if (r.ec != std::errc{} || r.ptr != s.data() + i + 2)
            throw std::runtime_error("Invalid profile hex field");
        result += static_cast<char>(value);
    }
    return result;
}
void reject_reparse(const std::filesystem::path &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Reparse point in profile storage");
}
struct Store {
    std::filesystem::path path;
    HANDLE lock = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    Values values;
    Store() {
        auto dir = bionic::profile_directory();
        path = dir / "preferences.db";
        auto lockpath = dir / "preferences.lock";
        reject_reparse(path);
        reject_reparse(lockpath);
        lock =
            CreateFileW(lockpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open profile lock");
        if (!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov)) {
            CloseHandle(lock);
            throw std::runtime_error("Cannot lock profile");
        }
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in && std::filesystem::exists(path))
                throw std::runtime_error("Cannot read profile");
            if (!in)
                return;
            std::string header;
            std::getline(in, header);
            if (header != "NOVA3PC_PROFILE_V1")
                throw std::runtime_error("Invalid profile header");
            std::string row;
            while (std::getline(in, row)) {
                std::istringstream stream(row);
                std::string space, key, value, extra;
                if (!(stream >> space >> key >> value) || (stream >> extra))
                    throw std::runtime_error("Invalid profile row");
                values[{decode(space), decode(key)}] = decode(value);
            }
            if (in.bad())
                throw std::runtime_error("Cannot read profile");
        } catch (...) {
            CloseHandle(lock);
            throw;
        }
    }
    ~Store() {
        if (lock != INVALID_HANDLE_VALUE) {
            UnlockFileEx(lock, 0, 1, 0, &ov);
            CloseHandle(lock);
        }
    }
    void save() {
        auto temp = path;
        temp += L".tmp";
        reject_reparse(temp);
        std::string bytes = "NOVA3PC_PROFILE_V1\n";
        for (const auto &[key, value] : values)
            bytes += encode(key.first) + " " + encode(key.second) + " " + encode(value) + "\n";
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot write profile");
        DWORD written = 0;
        bool ok = bytes.size() <= MAXDWORD &&
                  WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                  written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok ||
            !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot commit profile preferences");
    }
};
int64_t number(const std::string &text, int64_t fallback) {
    int64_t value = 0;
    auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size() ? value : fallback;
}
std::string trim(std::string s) {
    auto first = s.find_first_not_of(" \t\r");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}
std::string random_id(const std::string &name) {
    unsigned char bytes[16];
    if (BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate random profile identifier");
    if (name == "mac") {
        bytes[0] = (bytes[0] | 2) & 0xfe;
        std::string out;
        for (int i = 0; i < 6; ++i) {
            if (i)
                out += ':';
            out += hex(bytes + i, 1);
        }
        return out;
    }
    if (name == "imei") {
        std::string out;
        int sum = 0;
        for (int i = 0; i < 14; ++i) {
            int d = bytes[i] % 10;
            out += static_cast<char>('0' + d);
            if (i % 2)
                d *= 2;
            sum += d / 10 + d % 10;
        }
        out += static_cast<char>('0' + (10 - sum % 10) % 10);
        return out;
    }
    if (name == "udid") {
        bytes[6] = (bytes[6] & 15) | 0x40;
        bytes[8] = (bytes[8] & 63) | 0x80;
        auto s = hex(bytes, 16);
        return s.substr(0, 8) + "-" + s.substr(8, 4) + "-" + s.substr(12, 4) + "-" + s.substr(16, 4) + "-" +
               s.substr(20);
    }
    return hex(bytes, 8);
}
} // namespace
Config read_config(const std::filesystem::path &path, std::filesystem::path &obb) {
    Config out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("Invalid config line: " + line);
        auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "obb_directory")
            obb = std::filesystem::path(std::u8string(value.begin(), value.end()));
        else if (key == "graphics_backend")
            out.graphics_backend = value;
        else if (key == "audio") {
            if (value != "sdl" && value != "null") throw std::runtime_error("audio must be sdl or null");
            out.audio = value;
        }
        else if (key == "language")
            out.language = value;
        else if (key == "fullscreen")
            out.display.fullscreen=value=="true"; // Invalid host video preferences fall back safely.
        else if (key == "fullscreen_resolution" || key == "window_size") {
            const auto resolution=nova::platform::parse_display_resolution(value);
            auto &target=key=="window_size" ? out.display.window_size : out.display.fullscreen_resolution;
            target=resolution.value_or(nova::platform::OutputResolution{});
        }
        else if (key == "render_resolution")
            out.render_extent=nova::platform::parse_render_extent(value).value_or(nova::platform::GuestExtent{});
        else if (key == "weapon_position_x" || key == "weapon_position_y") {
            const auto coordinate=weapon_placement::parse_coordinate(value);
            if(!coordinate) throw std::runtime_error(key+" must be a finite viewport fraction from -0.05 to 0.05");
            (key=="weapon_position_x" ? out.weapon_position.x : out.weapon_position.y)=*coordinate;
        }
        else if (key == "texture_filtering") {
            int quality=-1;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),quality);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || quality<0 || quality>2)
                throw std::runtime_error("texture_filtering must be 0 (original), 1 (trilinear) or 2 (trilinear + anisotropic)");
            out.texture_filtering=quality;
        }
        else if (key == "lod_distance") {
            int multiplier=0;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),multiplier);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || multiplier<1 || multiplier>4)
                throw std::runtime_error("lod_distance must be an integer from 1 to 4");
            out.lod_distance=multiplier;
        }
        else if (key == "ui_scale") {
            const auto parsed=nova::ui::parse_percent(value);
            if(!parsed) throw std::runtime_error("ui_scale must be an integer percentage from 50 to 300");
            out.ui_scale=*parsed;
        }
        else if (key == "movement_wobble") {
            const auto parsed=nova::movement_wobble::parse_strength(value);
            if(!parsed) throw std::runtime_error("movement_wobble must be an integer percentage from 0 to 100");
            out.movement_wobble=*parsed;
        }
        else if (key == "world_fov" || key == "weapon_fov") {
            auto &setting = key == "world_fov" ? out.world_fov : out.weapon_fov;
            const float maximum = key == "world_fov" ? 120.f : 130.f;
            if (value == "native") {
                setting.reset();
            } else {
                float degrees = 0;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), degrees,
                                              std::chars_format::general);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    !std::isfinite(degrees) || degrees < 60.f || degrees > maximum)
                    throw std::runtime_error(key + " must be native or finite horizontal degrees from 60 to " +
                                             (key == "world_fov" ? "120" : "130") + " at 16:9");
                // Read old 60..130 preferences without rewriting the user's file.
                // The effective weapon limit is 104; normal FOV persistence saves it.
                setting = key == "weapon_fov" ? std::min(degrees,104.f) : degrees;
            }
        }
        else if (key.starts_with("hosts.")) {
            auto name = key.substr(6);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(tolower(c)); });
            if (name.ends_with('.'))
                name.pop_back();
            out.hosts[name] = value;
        } else
            throw std::runtime_error("Unknown config key: " + key);
    }
    if (!obb.empty() && obb.is_relative())
        obb = path.parent_path() / obb;
    return out;
}
void configure(const Config &c) { settings = c; }
const Config &config() { return settings; }
std::string get(const std::string &space, const std::string &key, const std::string &fallback) {
    std::lock_guard hold(guard);
    Store s;
    auto it = s.values.find({space, key});
    return it == s.values.end() ? fallback : it->second;
}
void put(const std::string &space, const std::string &key, const std::string &value) {
    std::lock_guard hold(guard);
    Store s;
    s.values[{space, key}] = value;
    s.save();
}
int64_t get_number(const std::string &space, const std::string &key, int64_t fallback) {
    return number(get(space, key), fallback);
}
void put_number(const std::string &space, const std::string &key, int64_t value) {
    put(space, key, std::to_string(value));
}
bool first_launch(const std::string &key) {
    std::lock_guard hold(guard);
    Store s;
    auto it = s.values.find({"MySavedData", key});
    if (it != s.values.end() && number(it->second, -1) != -1)
        return false;
    s.values[{"MySavedData", key}] = "1";
    s.save();
    return true;
}
bool new_day(int64_t milliseconds) {
    std::lock_guard hold(guard);
    Store s;
    auto key = std::make_pair(std::string("MySavedData"), std::string("isNewDay"));
    auto it = s.values.find(key);
    if (it != s.values.end()) {
        auto old = number(it->second, -1);
        if (old != -1 && milliseconds - old < 86400000)
            return false;
    }
    s.values[key] = std::to_string(milliseconds);
    s.save();
    return true;
}
std::string identifier(const std::string &name) {
    std::lock_guard hold(guard);
    Store s;
    auto key = std::make_pair(std::string("Identity"), name);
    auto it = s.values.find(key);
    if (it != s.values.end())
        return it->second;
    auto value = random_id(name);
    s.values[key] = value;
    s.save();
    return value;
}
std::string language_for_locale(std::string locale) {
    std::transform(locale.begin(), locale.end(), locale.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    std::replace(locale.begin(), locale.end(), '_', '-');
    auto primary = locale.substr(0, locale.find('-'));
    if (primary == "ja" || primary == "jp")
        return "jp";
    if (primary == "ko" || primary == "kr")
        return "kr";
    if (primary == "br" || locale == "pt-br")
        return "br";
    if (primary == "zh")
        return locale == "zh" || locale == "zh-cn" || locale == "zh-hans" || locale == "zh-sg" ? "zh"
                                                                                               : "en";
    for (auto p : {"en", "fr", "de", "es", "it", "ru"})
        if (primary == p)
            return p;
    return "en";
}
std::string language() {
    if (!settings.language.empty())
        return language_for_locale(settings.language);
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH,
                          0))
        return "en";
    std::wstring wide(name);
    return language_for_locale(std::string(wide.begin(), wide.end()));
}
std::string mac_digest() {
    auto mac = identifier("mac");
    unsigned char digest[16];
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("MD5 unavailable");
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0)
        status =
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(mac.data()), static_cast<ULONG>(mac.size()), 0);
    if (status >= 0)
        status = BCryptFinishHash(hash, digest, sizeof digest, 0);
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
        throw std::runtime_error("MD5 failed");
    auto result = hex(digest, sizeof digest);
    auto first = result.find_first_not_of('0');
    return first == std::string::npos ? "0" : result.substr(first);
}
} // namespace nova::profile
