#include "net/native_save_trace.h"
#include "loader/sha256.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace nova::net {
namespace {
// The original caller supplies a C string and CMemoryStream. Match only the
// proved purchase record; other native saves are forwarded without inspection.
bool purchase(uint32_t name) {
    return name && std::strcmp(reinterpret_cast<const char *>(name),
        "/data/data/com.gameloft.android.ANMP.GloftN3HM/a7.dat") == 0;
}
void report(const char *operation, uint32_t version, uint32_t stream, uint32_t result) noexcept {
    try {
        std::fprintf(stderr, "native-save record=purchases operation=%s version=%u accepted=%u",
                     operation, version, result & 255);
        // EncryptAndSave mutates its stream to the exact encrypted bytes written
        // after the two int32 header words. Hash ciphertext only, after success.
        if ((result & 255) && std::strcmp(operation, "encrypt-save") == 0 && stream) {
            uint32_t data = 0, size = 0;
            std::memcpy(&data, reinterpret_cast<const void *>(stream), 4);
            std::memcpy(&size, reinterpret_cast<const void *>(stream + 8), 4);
            if (data && size && size <= 1024 * 1024) {
                const auto digest = sha256({reinterpret_cast<const uint8_t *>(data), size});
                std::fprintf(stderr, " ciphertext_bytes=%u ciphertext_sha256=%s", size, digest.c_str());
            }
        }
        std::fputc('\n', stderr);
    } catch (...) {
        std::fputs("\nnative-save diagnostic unavailable\n", stderr);
    }
}
} // namespace

NativeSaveTrace *NativeSaveTrace::active_ = nullptr;
NativeSaveTrace::NativeSaveTrace(const ElfImage &image) {
    if (active_)
        throw std::runtime_error("Native save trace already installed");
    active_ = this;
    try {
        save_ = std::make_unique<abi::GuestHook>(image,
            "_ZN11Application14EncryptAndSaveEPKciP13CMemoryStream",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x48},
            reinterpret_cast<uint32_t>(&save));
        save_original_ = save_->original();
        load_ = std::make_unique<abi::GuestHook>(image,
            "_ZN11Application14DecryptAndLoadEPKciP13CMemoryStream",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x57, 0x56, 0x53},
            reinterpret_cast<uint32_t>(&load));
        load_original_ = load_->original();
    } catch (...) {
        active_ = nullptr;
        throw;
    }
}
NativeSaveTrace::~NativeSaveTrace() {
    load_.reset();
    save_.reset();
    if (active_ == this)
        active_ = nullptr;
}
uint32_t __cdecl NativeSaveTrace::save(uint32_t receiver, uint32_t name, uint32_t version,
                                       uint32_t stream) noexcept {
    try {
        auto &self = *active_;
        const auto result = abi::invoke(self.save_original_, {receiver, name, version, stream});
        if (purchase(name))
            report("encrypt-save", version, stream, result);
        return result;
    } catch (...) {
        // An exception in the original call cannot unwind across guest frames.
        TerminateProcess(GetCurrentProcess(), 86);
        return 0;
    }
}
uint32_t __cdecl NativeSaveTrace::load(uint32_t receiver, uint32_t name, uint32_t version,
                                       uint32_t stream) noexcept {
    try {
        auto &self = *active_;
        const auto result = abi::invoke(self.load_original_, {receiver, name, version, stream});
        if (purchase(name))
            report("decrypt-load", version, stream, result);
        return result;
    } catch (...) {
        TerminateProcess(GetCurrentProcess(), 86);
        return 0;
    }
}
} // namespace nova::net
