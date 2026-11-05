#include "loader/imports.h"
#include "loader/elf_image.h"
#include "loader/diagnostics.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace nova {
void ImportRegistry::function(const std::string& name, const void* ptr) {
    if (!ptr || !entries_.emplace(name, Binding{reinterpret_cast<uint32_t>(ptr), ImportKind::function, false}).second)
        throw std::runtime_error("Invalid or duplicate function binding: " + name);
}
void ImportRegistry::data(const std::string& name, const void* ptr, bool placeholder) {
    if (!ptr || !entries_.emplace(name, Binding{reinterpret_cast<uint32_t>(ptr), ImportKind::data, placeholder}).second)
        throw std::runtime_error("Invalid or duplicate data binding: " + name);
}
const Binding* ImportRegistry::find(const std::string& name) const {
    const auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second;
}

struct TrapStubs::Entry {
    const ElfImage* image;
    std::string name;
    [[noreturn]] static void __cdecl hit(const Entry* entry, uint32_t caller, uint32_t stack) {
        std::fprintf(stderr, "unimplemented import: %s\ncaller: %s (return=0x%08x)\n",
                     entry->name.c_str(), entry->image->symbolize(caller - 1).c_str(), caller);
        print_stack(*entry->image, stack);
        std::fflush(nullptr);
        TerminateProcess(GetCurrentProcess(), 70);
        __assume(false);
    }
};
TrapStubs::TrapStubs(const ElfImage& image) : image_(image), capacity_(2048) {
    code_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, capacity_ * 32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code_) throw std::runtime_error("Could not allocate import stubs");
}
TrapStubs::~TrapStubs() {
    for (auto* entry : entries_) delete entry;
    if (code_) VirtualFree(code_, 0, MEM_RELEASE);
}
uint32_t TrapStubs::add(const std::string& name) {
    if (sealed_ || entries_.size() >= capacity_) throw std::runtime_error("Import trap capacity exceeded");
    auto entry = std::make_unique<Entry>(Entry{&image_, name});
    uint8_t* stub = code_ + entries_.size() * 32;
    // mov eax,esp; push eax; push [eax]; push entry; mov eax,hit; call eax; int3
    const uint8_t bytes[] = {0x89,0xe0,0x50,0xff,0x30,0x68,0,0,0,0,0xb8,0,0,0,0,0xff,0xd0,0xcc};
    std::memcpy(stub, bytes, sizeof(bytes));
    const auto metadata = reinterpret_cast<uint32_t>(entry.get());
    const auto handler = reinterpret_cast<uint32_t>(&Entry::hit);
    std::memcpy(stub + 6, &metadata, 4);
    std::memcpy(stub + 11, &handler, 4);
    entries_.push_back(entry.get());
    entry.release();
    return reinterpret_cast<uint32_t>(stub);
}
void TrapStubs::seal() {
    DWORD previous;
    if (!VirtualProtect(code_, capacity_ * 32, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), code_, capacity_ * 32))
        throw std::runtime_error("Could not protect import stubs");
    sealed_ = true;
}
}
