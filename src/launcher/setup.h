#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nova::launcher {
namespace fs = std::filesystem;
struct Payload { std::string name, sha256; uint64_t size; };
struct Inputs {
    std::string apk_sha256;
    Payload library, main, patch;
    std::vector<std::string> bootstrap;
};
const Inputs &known_inputs();
struct Progress {
    std::function<void(const std::string &)> log;
    std::function<void(unsigned)> percent;
    const std::atomic_bool *cancel = nullptr;
    void check() const;
    void begin(const std::string &) const;
    void update(uint64_t done, uint64_t total) const;
};
enum class RequiredInput { apk, data };
struct InputRequired : std::runtime_error {
    RequiredInput kind;
    InputRequired(RequiredInput k, const char *message): std::runtime_error(message), kind(k) {}
};
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("Setup cancelled.") {} };
struct Layout {
    fs::path root;
    fs::path library() const;
    fs::path obb() const;
    fs::path profile() const;
    fs::path resources() const;
    fs::path receipt() const;
};
bool ready(const Layout &, const Inputs & = known_inputs());
void prepare(const Layout &, const fs::path &apk, const fs::path &data,
             const Progress &, const Inputs & = known_inputs());
// CRT-compatible quoting for CreateProcessW, including trailing backslashes.
std::wstring quote_argument(const std::wstring &);
std::wstring game_arguments(const Layout &, const fs::path &executable);
fs::path installation_root(const fs::path &executable_directory);
} // namespace nova::launcher
