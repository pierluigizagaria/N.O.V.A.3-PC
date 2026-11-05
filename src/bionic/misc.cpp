#include "bionic/runtime.h"
#include "net/network.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
namespace nova::bionic {
int __cdecl file_pread(int, void *, uint32_t, int32_t);
extern "C" int __cdecl guest_setjmp(uint32_t *);
extern "C" void __cdecl guest_longjmp(uint32_t *, int);
namespace {
const char *__cdecl environment(const char *key) {
    if (!key)
        return nullptr;
    struct Pair {
        const char *key;
        const char *value;
    };
    static constexpr Pair values[] = {{"ANDROID_ROOT", "/system"},
                                      {"ANDROID_DATA", "/data"},
                                      {"EXTERNAL_STORAGE", "/sdcard"},
                                      {"HOME", "/data/data/com.gameloft.android.ANMP.GloftN3HM"},
                                      {"TMPDIR", "/data/data/com.gameloft.android.ANMP.GloftN3HM/cache"},
                                      {"PATH", "/system/bin:/system/xbin"}};
    for (auto &v : values)
        if (!std::strcmp(key, v.key))
            return v.value;
    return nullptr;
}
int __cdecl process_id() { return static_cast<int>(GetCurrentProcessId()); }
uint32_t __cdecl user_id() { return 10000; }
int __cdecl configuration(int key) {
    switch (key) {
    case 6:
        return 100;
    case 11:
        return 32768;
    case 0x27:
    case 0x28:
        return 4096;
    case 0x4a:
        return 4;
    case 0x4b:
        return 128;
    case 0x4c:
        return 8192;
    case 0x60:
    case 0x61: {
        SYSTEM_INFO s{};
        GetSystemInfo(&s);
        return static_cast<int>(s.dwNumberOfProcessors);
    }
    case 0x62:
    case 0x63: {
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof m;
        GlobalMemoryStatusEx(&m);
        return static_cast<int>(
            std::min<uint64_t>((key == 0x62 ? m.ullTotalPhys : m.ullAvailPhys) / 4096, INT32_MAX));
    }
    default:
        log_once("unsupported sysconf " + std::to_string(key));
        return fail(38);
    }
}
[[noreturn]] void __cdecl process_exit(int code) {
    stream_flush(nullptr);
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    __assume(false);
}
[[noreturn]] void __cdecl process_abort() {
    std::fprintf(stderr, "guest abort\n");
    process_exit(134);
}
int __cdecl command(const char *cmd) {
    if (!cmd)
        return 0;
    log_once("system refused");
    return fail(13);
}
struct SignalAction {
    uint32_t handler, mask, flags, restorer;
}; // i386 old sigaction, asm-x86/signal.h
std::mutex signal_guard;
SignalAction signals[65]{};
int __cdecl action(int sig, const SignalAction *in, SignalAction *out) {
    if (sig <= 0 || sig >= 65 || sig == 9 || sig == 19)
        return fail(22);
    std::lock_guard lock(signal_guard);
    if (out)
        *out = signals[sig];
    if (in)
        signals[sig] = *in;
    log_once("sigaction recorded " + std::to_string(sig));
    return 0;
}
uint32_t __cdecl signal_handler(int sig, uint32_t fn) {
    SignalAction in{fn, 0, 0x10000000, 0}, out{};
    return action(sig, &in, &out) < 0 ? UINT32_MAX : out.handler;
}
int __cdecl raise_signal(int sig) {
    if (sig <= 0 || sig >= 65)
        return fail(22);
    log_once("raise recorded " + std::to_string(sig));
    return 0;
}
int __cdecl android_log(int priority, const char *tag, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::string message;
    try {
        message = format(fmt, a);
    } catch (...) {
        va_end(a);
        return fail(22);
    }
    va_end(a);
    std::fprintf(stderr, "android[%d][%s] %s\n", priority, tag ? tag : "", message.c_str());
    return static_cast<int>(message.size());
}
struct ReadMapping {
    HANDLE section = nullptr;
    uint64_t size = 0;
    ~ReadMapping() {
        if (section)
            CloseHandle(section);
    }
};
struct Mapping {
    size_t size;
    DWORD protection;
    void *allocation;
    std::shared_ptr<ReadMapping> file;
    uint32_t offset = 0;
};
std::mutex map_guard;
std::map<void *, Mapping> mappings;
DWORD protection(int prot) {
    return prot == 0 ? PAGE_NOACCESS : (prot & 2) ? PAGE_READWRITE : PAGE_READONLY;
}
void *map_error(int e) {
    fail(e);
    return reinterpret_cast<void *>(UINT32_MAX);
}
void release_mapping(const Mapping &m) {
    if (m.file)
        UnmapViewOfFile(m.allocation);
    else
        VirtualFree(m.allocation, 0, MEM_RELEASE);
}
void *map_file(Mapping &m) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    uint32_t delta = m.offset % system.dwAllocationGranularity;
    uint32_t base = m.offset - delta;
    // Provisional: reject ranges containing pages entirely beyond EOF instead of
    // emulating SIGBUS. T02 only supports complete unmaps and read-only files.
    if (m.offset >= m.file->size ||
        uint64_t(m.offset) + m.size > ((m.file->size + 4095) & ~uint64_t(4095))) {
        log_once("mmap pages beyond file end unsupported");
        return map_error(95);
    }
    size_t bytes = static_cast<size_t>(std::min<uint64_t>(uint64_t(m.size) + delta, m.file->size - base));
    m.allocation = MapViewOfFile(m.file->section, FILE_MAP_READ, 0, base, bytes);
    if (!m.allocation)
        return map_error(win_error(GetLastError()));
    auto *p = static_cast<unsigned char *>(m.allocation) + delta;
    DWORD previous;
    if (!VirtualProtect(p, m.size, m.protection, &previous)) {
        DWORD error = GetLastError();
        UnmapViewOfFile(m.allocation);
        return map_error(win_error(error));
    }
    return p;
}
void *__cdecl memory_map(void *, size_t size, int prot, int flags, int fd, int32_t offset) {
    if (!size || offset < 0 || (offset & 4095) || (prot & ~3) || ((flags & 3) != 1 && (flags & 3) != 2))
        return map_error(22);
    if (flags & ~(1 | 2 | 0x20))
        return map_error(95);
    if (!(flags & 0x20) && (prot & 2))
        return map_error(13);
    if (size > SIZE_MAX - 4095)
        return map_error(12);
    size = (size + 4095) & ~size_t(4095);
    Mapping m{size, protection(prot), nullptr, {}, static_cast<uint32_t>(offset)};
    void *p;
    if (flags & 0x20) {
        m.allocation = p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, m.protection);
        if (!p)
            return map_error(12);
    } else {
        HANDLE source = duplicate_read_file(fd);
        if (source == INVALID_HANDLE_VALUE)
            return reinterpret_cast<void *>(UINT32_MAX);
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(source, &length)) {
            DWORD error = GetLastError();
            CloseHandle(source);
            return map_error(win_error(error));
        }
        m.file = std::make_shared<ReadMapping>();
        m.file->size = length.QuadPart;
        m.file->section = CreateFileMappingW(source, nullptr, PAGE_READONLY, 0, 0, nullptr);
        DWORD error = GetLastError();
        CloseHandle(source);
        if (!m.file->section)
            return map_error(win_error(error));
        p = map_file(m);
        if (p == reinterpret_cast<void *>(UINT32_MAX))
            return p;
    }
    std::lock_guard lock(map_guard);
    mappings[p] = std::move(m);
    return p;
}
int __cdecl memory_unmap(void *p, size_t size) {
    if (!size || size > SIZE_MAX - 4095)
        return fail(22);
    std::lock_guard lock(map_guard);
    auto it = mappings.find(p);
    if (it == mappings.end() || ((size + 4095) & ~size_t(4095)) != it->second.size) {
        log_once("munmap requires complete mapping");
        return fail(22);
    }
    release_mapping(it->second);
    mappings.erase(it);
    return 0;
}
void *__cdecl memory_remap(void *p, size_t old_size, size_t new_size, int flags, ...) {
    if (!old_size || !new_size || old_size > SIZE_MAX - 4095 || new_size > SIZE_MAX - 4095 || (flags & ~1))
        return map_error(22);
    std::lock_guard lock(map_guard);
    auto it = mappings.find(p);
    if (it == mappings.end() || ((old_size + 4095) & ~size_t(4095)) != it->second.size)
        return map_error(22);
    size_t rounded = (new_size + 4095) & ~size_t(4095);
    if (rounded == it->second.size)
        return p;
    if (!(flags & 1))
        return map_error(12);
    Mapping next = it->second;
    next.size = rounded;
    void *q;
    if (next.file) {
        q = map_file(next);
        if (q == reinterpret_cast<void *>(UINT32_MAX))
            return q;
    } else {
        next.allocation = q = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!q)
            return map_error(12);
        DWORD previous;
        VirtualProtect(p, it->second.size, PAGE_READONLY, &previous);
        std::memcpy(q, p, std::min(it->second.size, rounded));
        VirtualProtect(q, rounded, next.protection, &previous);
    }
    release_mapping(it->second);
    mappings.erase(it);
    mappings[q] = std::move(next);
    return q;
}
} // namespace
void register_misc(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("getenv", environment);
    B("getpid", process_id);
    B("getuid", user_id);
    B("sysconf", configuration);
    B("exit", process_exit);
    B("abort", process_abort);
    B("system", command);
    B("signal", signal_handler);
    B("bsd_signal", signal_handler);
    B("sigaction", action);
    B("raise", raise_signal);
    B("__android_log_print", android_log);
    B("setjmp", guest_setjmp);
    B("longjmp", guest_longjmp);
    B("mmap", memory_map);
    B("munmap", memory_unmap);
    B("mremap", memory_remap);
#undef B
}
void register_boot(ImportRegistry &r) {
    register_data(r);
    register_stdlib(r);
    register_string(r);
    register_pthread(r);
    register_wchar(r);
    register_time(r);
    register_files(r);
    net::register_network(r);
    register_stdio(r);
    register_format(r);
    register_misc(r);
    register_math(r);
}
} // namespace nova::bionic
