#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include "loader/report.h"
#include <Windows.h>
#include <charconv>
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "Usage: nova3pc-inspect.exe <lib> [--run-init] [--symbolize <ELF-vaddr>]\n");
        return 2;
    }
    try {
        bool initialize = false, symbol_requested = false;
        uint32_t symbol_address = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--run-init")
                initialize = true;
            else if (option == "--symbolize" && i + 1 < argc) {
                std::string value = argv[++i];
                int radix = 10;
                if (value.starts_with("0x")) {
                    value.erase(0, 2);
                    radix = 16;
                }
                const auto result =
                    std::from_chars(value.data(), value.data() + value.size(), symbol_address, radix);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                    throw std::runtime_error("Invalid ELF virtual address");
                symbol_requested = true;
            } else
                throw std::runtime_error("Unknown or incomplete option: " + option);
        }
        nova::ImportRegistry registry;
        nova::bionic::register_boot(registry);
        nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(argv[1], registry);
        nova::Diagnostics diagnostics(image);
        std::printf("base=0x%08x reservation=0x%08x mapped=%u bytes pointer_bits=%zu\n", image.base(),
                    image.reservation(), image.mapped_size(), sizeof(void *) * 8);
        for (const auto &s : image.segments())
            std::printf("PT_LOAD vaddr=0x%08x host=0x%08x filesz=0x%x memsz=0x%x %c%c%c\n", s.vaddr,
                        image.base() + s.vaddr, s.filesz, s.memsz, s.flags & 4 ? 'R' : '-',
                        s.flags & 2 ? 'W' : '-', s.flags & 1 ? 'X' : '-');
        for (auto [type, name] : {std::pair{8u, "R_386_RELATIVE"},
                                  {7u, "R_386_JMP_SLOT"},
                                  {6u, "R_386_GLOB_DAT"},
                                  {1u, "R_386_32"}}) {
            const auto it = image.relocation_counts().find(type);
            std::printf("%s=%zu\n", name, it == image.relocation_counts().end() ? 0 : it->second);
        }
        nova::report_imports(image);
        size_t java = 0;
        for (const auto &symbol : image.exports()) {
            if (symbol.name == "JNI_OnLoad" || symbol.name == "JNI_GetEnv" ||
                symbol.name.starts_with("Java_")) {
                std::printf("export %s 0x%08x\n", symbol.name.c_str(), image.lookup(symbol.name));
                if (symbol.name.starts_with("Java_"))
                    ++java;
            }
        }
        std::printf("Java_exports=%zu symbols=%zu initializer_slots=%zu\n", java, image.symbol_count(),
                    image.initializer_count());
        if (symbol_requested)
            std::printf("symbolize ELF-vaddr=0x%08x: %s\n", symbol_address,
                        image.symbolize(image.base() + symbol_address).c_str());
        if (initialize) {
            image.run_initializers();
            std::printf("__cxa_atexit registrations=%zu\n", cxa.size());
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "nova3pc-inspect: %s\n", error.what());
        return 1;
    }
}
