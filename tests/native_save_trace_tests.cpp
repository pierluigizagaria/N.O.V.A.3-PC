#include "net/native_save_trace.h"
#include "bionic/runtime.h"
#include "loader/sha256.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> uint32_t ptr(T *value) { return reinterpret_cast<uint32_t>(value); }
constexpr std::array names{
    "_ZN11Application14EncryptAndSaveEPKciP13CMemoryStream",
    "_ZN11Application14DecryptAndLoadEPKciP13CMemoryStream"};
}
int main(int argc, char **argv) {
    try {
        check(argc == 2 || argc == 3, "usage: native-save-trace-tests ELF [real]");
        const bool real = argc == 3 && std::string(argv[2]) == "real";
        if (real && (!std::getenv("NOVA_REAL_TESTS") ||
            std::string(std::getenv("NOVA_REAL_TESTS")) != "1" ||
            !std::filesystem::is_regular_file(argv[1]))) {
            std::puts("SKIP native save hook admission: requires opt-in and local library");
            return 77;
        }
        nova::ImportRegistry imports;
        if (real) nova::bionic::register_boot(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1], imports);
        if (real)
            check(nova::sha256(image.source_bytes()) ==
                "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e",
                "supported native library identity");
        std::array<uint32_t, 2> functions{};
        std::array<std::array<uint8_t, 6>, 2> original{};
        for (size_t i = 0; i < names.size(); ++i) {
            functions[i] = nova::abi::require_function(image, names[i]);
            std::memcpy(original[i].data(), reinterpret_cast<void *>(functions[i]), 6);
        }
        std::array<uint8_t, 4> bytes{1, 3, 5, 7};
        std::array<uint32_t, 4> stream{ptr(bytes.data()), 4, 4, 0};
        std::array<uint32_t, 5> receiver{};
        const char *name = "/data/data/com.gameloft.android.ANMP.GloftN3HM/a7.dat";
        auto execute = [&] {
            for (auto function : functions) {
                for (uint32_t result : {0x12340001u, 0xabcd0000u}) {
                    receiver = {0, 0, 0, 0, result};
                    const auto value = nova::abi::invoke(function,
                        {ptr(receiver.data()), ptr(name), 5, ptr(stream.data())});
                    check(value == result && receiver[0] == 1 && receiver[1] == ptr(name) &&
                          receiver[2] == 5 && receiver[3] == ptr(stream.data()),
                          "single aligned forwarding call preserves arguments and complete return");
                    check(bytes == std::array<uint8_t, 4>{1, 3, 5, 7} &&
                          stream == std::array<uint32_t, 4>{ptr(bytes.data()), 4, 4, 0},
                          "diagnostics leave native stream unchanged");
                }
            }
        };
        if (!real) execute();
        {
            nova::net::NativeSaveTrace trace(image);
            if (!real) execute();
        }
        for (size_t i = 0; i < functions.size(); ++i)
            check(std::memcmp(reinterpret_cast<void *>(functions[i]), original[i].data(), 6) == 0,
                  "original code restored");
        if (!real) execute();
        std::puts(real ? "PASS original save/load hook admission and restoration (no native save execution)"
                       : "PASS synthetic save/load forwarding, failure, alignment, stream and restoration");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
