#include "abi/guest_call.h"
#include "bionic/imports.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("Check failed at line ") + std::to_string(__LINE__) + ": " #expression); } while (false)

namespace nova::abi {
// Exercise the boundary from all four possible Windows x86 stack residues.
__declspec(naked) uint32_t __cdecl test_offset(uint32_t, uint32_t) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, [ebp + 12]
        push 0
        push 0
        push [ebp + 8]
        call call_guest
        mov esp, ebp
        pop ebp
        ret
    }
}
__declspec(naked) uint32_t __cdecl test_saved_registers(uint32_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ebx, 12345678h
        mov esi, 23456789h
        mov edi, 3456789ah
        push 0
        push 0
        push [ebp + 8]
        call call_guest
        xor eax, eax
        cmp ebx, 12345678h
        jne done
        cmp esi, 23456789h
        jne done
        cmp edi, 3456789ah
        jne done
        mov eax, 1
    done:
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
}
namespace {
int host_data = 7;
int __cdecl host_function(int value) { return value * 3; }
void require_error(const std::function<void()>& function, const std::string& expected) {
    try { function(); }
    catch (const std::exception& error) {
        CHECK(std::string(error.what()).find(expected) != std::string::npos);
        return;
    }
    throw std::runtime_error("Expected rejection containing: " + expected);
}
uint32_t value_at(uint32_t address) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), 4);
    return value;
}
DWORD protection(uint32_t address) {
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_COMMIT);
    CHECK(info.Protect != PAGE_EXECUTE_READWRITE && info.Protect != PAGE_EXECUTE_WRITECOPY);
    return info.Protect;
}
template<class Function> Function binding(const nova::ImportRegistry& imports, const char* name) {
    const auto* result = imports.find(name);
    CHECK(result && result->kind == nova::ImportKind::function);
    return reinterpret_cast<Function>(result->address);
}
void check_bionic(nova::ImportRegistry& registry, nova::ElfImage& image, nova::bionic::CxaRegistry& cxa) {
    const auto allocate = binding<void* (__cdecl*)(size_t)>(registry, "malloc");
    const auto release = binding<void (__cdecl*)(void*)>(registry, "free");
    for (size_t size : {size_t(0), size_t(1), size_t(31), size_t(4096)}) {
        void* memory = allocate(size);
        CHECK(memory && reinterpret_cast<uint32_t>(memory) % 16 == 0);
        if (size) std::memset(memory, 0xab, size);
        release(memory);
    }
    release(nullptr);
    char buffer[16]{};
    auto copy = binding<void* (__cdecl*)(void*, const void*, size_t)>(registry, "memcpy");
    auto move = binding<void* (__cdecl*)(void*, const void*, size_t)>(registry, "memmove");
    CHECK(copy(buffer, "abcdef", 7) == buffer);
    CHECK(move(buffer + 1, buffer, 6) == buffer + 1);
    CHECK(std::memcmp(buffer, "aabcdef", 7) == 0);
    CHECK(binding<size_t (__cdecl*)(const char*)>(registry, "strlen")("hello") == 5);
    CHECK(binding<int (__cdecl*)(const char*, const char*)>(registry, "strcmp")("\xff", "\x01") > 0);

    auto wctob = binding<int (__cdecl*)(uint32_t)>(registry, "wctob");
    auto btowc = binding<uint32_t (__cdecl*)(int)>(registry, "btowc");
    CHECK(wctob('Z') == 'Z' && wctob(0x10000) == -1 && wctob(UINT32_MAX) == -1);
    CHECK(btowc('A') == 'A' && btowc(-1) == UINT32_MAX && btowc(0xff) == UINT32_MAX);
    auto wctype = binding<uint32_t (__cdecl*)(const char*)>(registry, "wctype");
    CHECK(wctype("alpha") == 3 && wctype("unknown") == 0);
    for (const char* name : {"__sF", "_ctype_", "_tolower_tab_", "SL_IID_ENGINE", "SL_IID_PLAY", "SL_IID_BUFFERQUEUE"}) {
        CHECK(registry.find(name) && registry.find(name)->kind == nova::ImportKind::data);
        CHECK(registry.find(name)->placeholder == std::string(name).starts_with("SL_IID_"));
    }
    CHECK(value_at(registry.find("SL_IID_ENGINE")->address) != value_at(registry.find("SL_IID_PLAY")->address));

    const auto lock = binding<int (__cdecl*)(uint32_t*)>(registry, "pthread_mutex_lock");
    const auto unlock = binding<int (__cdecl*)(uint32_t*)>(registry, "pthread_mutex_unlock");
    const auto init = binding<int (__cdecl*)(uint32_t*, const uint32_t*)>(registry, "pthread_mutex_init");
    const auto attr_init = binding<int (__cdecl*)(uint32_t*)>(registry, "pthread_mutexattr_init");
    const auto attr_type = binding<int (__cdecl*)(uint32_t*, int)>(registry, "pthread_mutexattr_settype");
    const auto attr_destroy = binding<int (__cdecl*)(uint32_t*)>(registry, "pthread_mutexattr_destroy");
    struct Storage { uint32_t before, mutex, after; } storage{0x12345678, 0, 0x87654321};
    uint32_t attribute = 99;
    CHECK(attr_init(&attribute) == 0 && attribute == 0);
    CHECK(attr_type(&attribute, 3) == 22);
    CHECK(attr_type(&attribute, 1) == 0);
    CHECK(init(&storage.mutex, &attribute) == 0 && storage.mutex == 0x4000);
    CHECK(lock(&storage.mutex) == 0 && lock(&storage.mutex) == 0);
    CHECK(unlock(&storage.mutex) == 0 && unlock(&storage.mutex) == 0);
    CHECK(unlock(&storage.mutex) == 1);
    CHECK(attr_type(&attribute, 2) == 0 && init(&storage.mutex, &attribute) == 0);
    CHECK(lock(&storage.mutex) == 0 && lock(&storage.mutex) == 35 && unlock(&storage.mutex) == 0);
    CHECK(init(&storage.mutex, nullptr) == 0);
    int shared_counter = 0;
    std::atomic<bool> thread_error{false};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i) threads.emplace_back([&] {
        for (unsigned j = 0; j < 1000; ++j) {
            if (lock(&storage.mutex) != 0) { thread_error = true; return; }
            ++shared_counter;
            if (unlock(&storage.mutex) != 0) { thread_error = true; return; }
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK(!thread_error && shared_counter == 4000);
    CHECK(storage.before == 0x12345678 && storage.after == 0x87654321);
    CHECK(attr_destroy(&attribute) == 0 && attribute == UINT32_MAX);

    CHECK(nova::abi::call0(image.lookup("once_probe")) == 1);
    auto* once_word = reinterpret_cast<long*>(image.lookup("once_control"));
    auto* once_count = reinterpret_cast<uint32_t*>(image.lookup("once_count"));
    *once_word = 0; *once_count = 0;
    const auto once = binding<int (__cdecl*)(volatile long*, uint32_t)>(registry, "pthread_once");
    threads.clear();
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&] {
        if (once(once_word, image.lookup("once_callback")) != 0) thread_error = true;
    });
    for (auto& thread : threads) thread.join();
    CHECK(!thread_error && *once_word == 2 && *once_count == 1);

    const auto cond_init = binding<int (__cdecl*)(uint32_t*, const uint32_t*)>(registry, "pthread_cond_init");
    const auto broadcast = binding<int (__cdecl*)(volatile long*)>(registry, "pthread_cond_broadcast");
    uint32_t condition = 99;
    CHECK(cond_init(&condition, nullptr) == 0 && condition == 0);
    std::atomic<bool> waiter_ready{false}, awakened{false};
    std::thread waiter([&] {
        const uint32_t expected = 0;
        waiter_ready = true;
        while (InterlockedCompareExchange(reinterpret_cast<long*>(&condition), 0, 0) == 0) {
            if (!WaitOnAddress(&condition, const_cast<uint32_t*>(&expected), 4, 3000)) {
                thread_error = true;
                return;
            }
        }
        awakened = true;
    });
    while (!waiter_ready) SwitchToThread();
    const int broadcast_result = broadcast(reinterpret_cast<long*>(&condition));
    waiter.join();
    CHECK(broadcast_result == 0 && awakened && !thread_error && condition != 0);
    const auto key_create = binding<int (__cdecl*)(uint32_t*, uint32_t)>(registry, "pthread_key_create");
    uint32_t key1 = UINT32_MAX, key2 = UINT32_MAX;
    CHECK(key_create(&key1, 0) == 0 && key_create(&key2, image.lookup("once_callback")) == 0);
    CHECK(key1 != key2 && TlsGetValue(key1) == nullptr);

    struct Timeval { int32_t seconds, microseconds; };
    struct GuardedTime { uint32_t before; Timeval value; uint32_t after; };
    GuardedTime time{0x1234, {}, 0x5678};
    const auto now = binding<int (__cdecl*)(Timeval*, void*)>(registry, "gettimeofday");
    CHECK(now(&time.value, nullptr) == 0);
    CHECK(time.value.seconds > 1600000000 && time.value.microseconds >= 0 && time.value.microseconds < 1000000);
    CHECK(time.before == 0x1234 && time.after == 0x5678);
    auto clock = binding<int32_t (__cdecl*)()>(registry, "clock");
    FILETIME created{}, exited{}, kernel{}, user{};
    CHECK(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
    const uint64_t expected_clock = (((uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
                                    ((uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime)) / 10;
    const auto actual_clock = static_cast<uint32_t>(clock());
    CHECK(actual_clock >= expected_clock && actual_clock - expected_clock < 100000);
    CHECK(cxa.size() == 0);
    CHECK(nova::abi::call0(image.lookup("register_destructor")) == 0);
    CHECK(cxa.size() == 1 && *once_count == 1);
}
}
int main(int argc, char** argv) {
    try {
        CHECK(argc >= 2);
        const std::filesystem::path fixtures = argv[1];
        nova::ImportRegistry registry;
        registry.data("host_data", &host_data);
        registry.function("host_function", reinterpret_cast<const void*>(&host_function));
        nova::bionic::register_data(registry);
        nova::bionic::register_stdlib(registry);
        nova::bionic::register_string(registry);
        nova::bionic::register_pthread(registry);
        nova::bionic::register_wchar(registry);
        nova::bionic::register_time(registry);
        nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(fixtures / "loader.so", registry);
        nova::Diagnostics diagnostics(image);
        if (argc > 2) {
            const std::string mode = argv[2];
            nova::abi::call0(image.lookup(mode == "trap-first" ? "trigger_first" :
                                         mode == "trap-second" ? "trigger_second" : "trigger_crash"));
            throw std::runtime_error("Failure fixture unexpectedly returned");
        }
        CHECK(image.lookup("does_not_exist") == 0);
        CHECK(image.lookup("absolute_export") == 0x100);
        CHECK(image.lookup("file_only_symbol") == 0);
        CHECK(image.lookup("alignment_probe") != 0);
        for (uint32_t type : {1u, 6u, 7u, 8u}) CHECK(image.relocation_counts().at(type) > 0);
        CHECK(value_at(image.lookup("absolute_pointer")) == reinterpret_cast<uint32_t>(&host_data));
        CHECK(value_at(image.lookup("absolute_addend_pointer")) == reinterpret_cast<uint32_t>(&host_data) + 3);
        CHECK(image.contains(value_at(image.lookup("relative_pointer"))));
        CHECK(value_at(value_at(image.lookup("relative_pointer"))) == 41);
        CHECK(nova::abi::call0(image.lookup("relocation_probe")) == 69);
        for (unsigned i = 0; i < 64; ++i) CHECK(value_at(image.lookup("zero_bss") + i * 4) == 0);
        for (uint32_t offset : {0u, 4u, 8u, 12u}) {
            CHECK(nova::abi::test_offset(image.lookup("alignment_probe"), offset) == 12);
        }
        CHECK(nova::abi::test_saved_registers(image.lookup("alignment_probe")) == 1);
        const uint32_t args[] = {1, 2, 3, 4, 5, 6, 7};
        CHECK(nova::abi::call_guest(image.lookup("sum_seven"), args, 7) == 140);
        CHECK(image.symbolize(image.lookup("relocation_probe") + 1).find("relocation_probe+0x1") != std::string::npos);
        const auto hidden = nova::abi::call0(image.lookup("file_only_address"));
        CHECK(image.symbolize(hidden).find("file_only_symbol+0x0") != std::string::npos);
        for (uint32_t page = image.reservation(); page < image.reservation() + image.mapped_size(); page += 4096)
            protection(page);
        CHECK(protection(image.lookup("alignment_probe")) == PAGE_EXECUTE_READ);
        CHECK(protection(image.lookup("zero_bss")) == PAGE_READWRITE);
        uint32_t trap_address = 0;
        for (const auto& import : image.imports()) {
            if (import.trapped) {
                CHECK(protection(import.address) == PAGE_EXECUTE_READ);
                CHECK(trap_address != import.address);
                trap_address = import.address;
            }
        }
        CHECK(value_at(image.lookup("init_order")) == 0);
        CHECK(image.run_initializers() == 3);
        CHECK(value_at(image.lookup("init_order")) == 123);
        require_error([&] { image.run_initializers(); }, "only be run once");
        check_bionic(registry, image, cxa);
        {
            nova::ElfImage other(fixtures / "loader.so", registry);
            CHECK(image.base() != other.base());
            CHECK(value_at(other.lookup("relative_pointer")) != value_at(image.lookup("relative_pointer")));
            CHECK(nova::abi::call0(other.lookup("relocation_probe")) == 69);
        }
        {
            nova::ElfImage stripped(fixtures / "stripped.so", registry);
            CHECK(nova::abi::call0(stripped.lookup("relocation_probe")) == 69);
            CHECK(stripped.symbolize(stripped.lookup("relocation_probe")).find("relocation_probe+") != std::string::npos);
        }
        for (const auto& [file, message] : std::vector<std::pair<std::string, std::string>>{
            {"bad_relocation.so", "unsupported relocation type 42 at offset 0x"},
            {"bad_target.so", "unmapped virtual range"},
            {"bad_symbol.so", "relocation symbol index"},
            {"wx.so", "writable/executable page"},
            {"arm.so", "expected ET_DYN / EM_386"},
            {"truncated.so", "truncated program headers"}}) {
            require_error([&] { nova::ElfImage bad(fixtures / file, registry); }, message);
        }
        nova::ImportRegistry missing_data;
        require_error([&] { nova::ElfImage bad(fixtures / "loader.so", missing_data); }, "unregistered data import");
        nova::ImportRegistry wrong_kind;
        wrong_kind.function("host_data", reinterpret_cast<const void*>(&host_function));
        require_error([&] { nova::ElfImage bad(fixtures / "loader.so", wrong_kind); }, "import kind mismatch");
        std::puts("PASS: relocation values, imports, BSS, two bases, W^X, init order, ABI, symbols, bionic, malformed ELF");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
