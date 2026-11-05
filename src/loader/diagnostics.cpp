#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include <Windows.h>
#include <cstdio>
#include <stdexcept>

namespace nova {
namespace {
const ElfImage* active_image = nullptr;
LONG CALLBACK exception_handler(EXCEPTION_POINTERS* details) {
    const DWORD code = details->ExceptionRecord->ExceptionCode;
    // Ignore language exceptions and debugger notifications.
    if (!active_image || (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_IN_PAGE_ERROR &&
        code != EXCEPTION_DATATYPE_MISALIGNMENT && code != EXCEPTION_PRIV_INSTRUCTION))
        return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t fault = details->ContextRecord->Eip;
    std::fprintf(stderr, "guest exception 0x%08lx at %s (address=0x%08x)\n",
                 code, active_image->symbolize(fault).c_str(), fault);
    if (code == EXCEPTION_ACCESS_VIOLATION && details->ExceptionRecord->NumberParameters >= 2)
        std::fprintf(stderr, "access=%lu target=0x%08lx\n",
                     details->ExceptionRecord->ExceptionInformation[0],
                     details->ExceptionRecord->ExceptionInformation[1]);
    print_stack(*active_image, details->ContextRecord->Esp);
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 71);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void print_stack(const ElfImage& image, uint32_t stack) {
    std::fprintf(stderr, "stack scan (candidate return addresses, not an unwind):\n");
    unsigned found = 0;
    for (uint32_t word = 0; word < 128 && found < 12; ++word) {
        const uint64_t candidate = uint64_t(stack) + word * 4;
        if (candidate > UINT32_MAX - 4) break;
        uint32_t value = 0;
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(static_cast<uint32_t>(candidate)),
                               &value, sizeof(value), &read) || read != sizeof(value)) break;
        if (value && image.executable(value - 1)) {
            std::fprintf(stderr, "  esp+0x%x: %s\n", word * 4, image.symbolize(value - 1).c_str());
            ++found;
        }
    }
}
Diagnostics::Diagnostics(const ElfImage& image) {
    if (active_image) throw std::runtime_error("Only one active guest diagnostics scope is supported");
    active_image = &image;
    handler_ = AddVectoredExceptionHandler(1, exception_handler);
    if (!handler_) {
        active_image = nullptr;
        throw std::runtime_error("AddVectoredExceptionHandler failed");
    }
    previous_error_mode_ = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}
Diagnostics::~Diagnostics() {
    if (handler_) RemoveVectoredExceptionHandler(handler_);
    active_image = nullptr;
    SetErrorMode(previous_error_mode_);
}
}
