#include "platform/build_interface.h"
#include <cstdlib>
#include "bionic/descriptors.h"
#include "net/network.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
namespace nova::bionic {
Descriptor::~Descriptor() {
    if (!owned)
        return;
    if (is_socket())
        closesocket(socket);
    else if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
}
namespace {
std::mutex fd_guard;
std::map<int, std::shared_ptr<Descriptor>> descriptors;
bool descriptors_ready = false;
void initialize_descriptors() {
    if (descriptors_ready)
        return;
    descriptors_ready = true;
    for (int fd = 0; fd < 3; ++fd) {
        auto d = std::make_shared<Descriptor>();
        d->handle = GetStdHandle(fd == 0   ? STD_INPUT_HANDLE
                                 : fd == 1 ? STD_OUTPUT_HANDLE
                                           : STD_ERROR_HANDLE);
        d->flags = fd ? 1 : 0;
        d->owned = false;
        descriptors[fd] = d;
    }
}
} // namespace
std::shared_ptr<Descriptor> descriptor(int fd) {
    std::lock_guard lock(fd_guard);
    initialize_descriptors();
    auto it = descriptors.find(fd);
    if (it == descriptors.end()) {
        fail(9);
        return {};
    }
    return it->second;
}
int adopt_descriptor(std::shared_ptr<Descriptor> d) {
    std::lock_guard lock(fd_guard);
    initialize_descriptors();
    int fd = 0;
    while (descriptors.contains(fd))
        ++fd;
    if (fd > 32767)
        return fail(24);
    static uint64_t serial = 0;
    d->serial = ++serial;
    descriptors[fd] = d;
    return fd;
}
namespace {
int adopt(HANDLE h, int flags) {
    auto d = std::make_shared<Descriptor>();
    d->handle = h;
    d->flags = flags;
    return adopt_descriptor(d);
}
int64_t position(HANDLE h, int64_t n, int origin) {
    LARGE_INTEGER offset{}, result{};
    offset.QuadPart = n;
    if (origin < 0 || origin > 2)
        return fail(22);
    if (GetFileType(h) == FILE_TYPE_PIPE)
        return fail(29);
    if (!SetFilePointerEx(h, offset, &result, static_cast<DWORD>(origin)))
        return fail(win_error(GetLastError()));
    return result.QuadPart;
}
void timestamp(const FILETIME &t, uint32_t &sec, uint32_t &ns) {
    uint64_t ticks = (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    ticks = ticks < 116444736000000000ULL ? 0 : ticks - 116444736000000000ULL;
    sec = static_cast<uint32_t>(ticks / 10000000);
    ns = static_cast<uint32_t>((ticks % 10000000) * 100);
}
int stat_handle(HANDLE h, Stat *s) {
    if (!s)
        return fail(14);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info))
        return fail(win_error(GetLastError()));
    *s = {};
    s->dev = info.dwVolumeSerialNumber;
    s->ino = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    s->ino32 = static_cast<uint32_t>(s->ino);
    s->mode = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0040777 : 0100666;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        s->mode &= ~0222u;
    s->nlink = info.nNumberOfLinks;
    s->uid = s->gid = 10000;
    s->size = (int64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    s->blksize = 4096;
    s->blocks = (s->size + 511) / 512;
    timestamp(info.ftLastAccessTime, s->atime, s->atime_nsec);
    timestamp(info.ftLastWriteTime, s->mtime, s->mtime_nsec);
    timestamp(info.ftCreationTime, s->ctime, s->ctime_nsec);
    return 0;
}
int __cdecl path_stat(const char *p, Stat *s) {
    auto path = map_path(p);
    if (path.empty())
        return -1;
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return fail(win_error(GetLastError()));
    int result = stat_handle(h, s);
    CloseHandle(h);
    return result;
}
int __cdecl make_directory(const char *p, uint32_t) {
    auto path = map_path(p, true);
    if (path.empty())
        return -1;
    return CreateDirectoryW(path.c_str(), nullptr) ? 0 : fail(win_error(GetLastError()));
}
int __cdecl unlink_file(const char *p) {
    auto path = map_path(p, true);
    if (path.empty())
        return -1;
    return DeleteFileW(path.c_str()) ? 0 : fail(win_error(GetLastError()));
}
int __cdecl remove_path(const char *p) {
    auto path = map_path(p, true);
    if (path.empty())
        return -1;
    DWORD a = GetFileAttributesW(path.c_str());
    BOOL ok = (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
                  ? RemoveDirectoryW(path.c_str())
                  : DeleteFileW(path.c_str());
    return ok ? 0 : fail(win_error(GetLastError()));
}
int __cdecl rename_path(const char *a, const char *b) {
    auto source = map_path(a, true);
    if (source.empty())
        return -1;
    auto dest = map_path(b, true);
    if (dest.empty())
        return -1;
    return MoveFileExW(source.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)
               ? 0
               : fail(win_error(GetLastError()));
}
int __cdecl control(int fd, int request, ...) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    std::lock_guard lock(d->guard);
    switch (request) {
    case 1:
        return (d->flags & 0x80000) ? 1 : 0; // F_GETFD
    case 2: {
        va_list a;
        va_start(a, request);
        int v = va_arg(a, int);
        va_end(a);
        d->flags = (d->flags & ~0x80000) | ((v & 1) ? 0x80000 : 0);
        return 0;
    }
    case 3:
        return d->flags & ~(64 | 128 | 512 | 0x80000); // F_GETFL
    case 4: {
        va_list a;
        va_start(a, request);
        int v = va_arg(a, int);
        va_end(a);
        if (v & ~(3 | 1024 | 2048))
            return fail(22);
        if (d->is_socket() && net::nonblocking(d, (v & 2048) != 0) < 0)
            return -1;
        d->flags = (d->flags & ~(1024 | 2048)) | (v & (1024 | 2048));
        return 0;
    }
    default:
        log_once("unsupported fcntl " + std::to_string(request));
        return fail(22);
    }
}
int __cdecl io_control(int fd, uint32_t request, ...) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    const bool interface_request = request == 0x8912 || request == 0x8913;
    if (interface_request && !d->is_socket())
        return fail(25);
    if (request == 0x5421 || request == 0x541b || interface_request) {
        va_list a;
        va_start(a, request);
        void *p = va_arg(a, void *);
        va_end(a);
        if (!p)
            return fail(14);
        if (d->is_socket())
            return net::socket_ioctl(d, request, p);
        if (request == 0x541b) {
            std::lock_guard lock(d->guard);
            LARGE_INTEGER size;
            auto pos = position(d->handle, 0, 1);
            if (pos < 0 || !GetFileSizeEx(d->handle, &size))
                return fail(25);
            *static_cast<int *>(p) = static_cast<int>(
                (std::min)(int64_t(INT32_MAX), (std::max)(int64_t(0), size.QuadPart - pos)));
            return 0;
        }
    }
    log_once("unsupported ioctl " + std::to_string(request));
    return fail(25);
}
char *__cdecl base_name(char *path) {
    static char dot[] = ".";
    if (!path || !*path)
        return dot;
    size_t n = std::strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    char *last = std::strrchr(path, '/');
    return last ? (last[1] ? last + 1 : last) : path;
}
struct Directory {
    std::vector<std::pair<std::string, uint8_t>> entries;
    size_t index = 0;
    Dirent current{};
};
Directory *__cdecl directory_open(const char *p) {
    if (nova::build_interface::developer && std::getenv("NOVA_TRACE_FILES") != nullptr)
        log_once(std::string("opendir path=") + (p ? p : "<null>"));
    auto path = map_path(p);
    if (path.empty())
        return nullptr;
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        fail(ec ? win_error(static_cast<DWORD>(ec.value())) : 20);
        return nullptr;
    }
    auto d = std::make_unique<Directory>();
    d->entries = {{".", 4}, {"..", 4}};
    for (auto it = std::filesystem::directory_iterator(path, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        auto u8 = it->path().filename().u8string();
        std::string name(u8.begin(), u8.end());
        if (name.size() > 255) {
            fail(36);
            return nullptr;
        }
        d->entries.emplace_back(name, it->is_directory(ec) ? 4 : 8);
    }
    if (ec) {
        fail(win_error(static_cast<DWORD>(ec.value())));
        return nullptr;
    }
    return d.release();
}
Dirent *__cdecl directory_read(Directory *d) {
    if (!d) {
        fail(9);
        return nullptr;
    }
    if (d->index == d->entries.size())
        return nullptr;
    const auto &e = d->entries[d->index++];
    if (nova::build_interface::developer && std::getenv("NOVA_TRACE_FILES") != nullptr)
        log_once("readdir name=" + e.first + " type=" + std::to_string(e.second));
    d->current = {};
    d->current.ino = d->index;
    d->current.off = d->index;
    d->current.reclen = static_cast<uint16_t>((19 + e.first.size() + 1 + 7) & ~size_t(7));
    d->current.type = e.second;
    std::memcpy(d->current.name, e.first.c_str(), e.first.size() + 1);
    return &d->current;
}
int __cdecl directory_close(Directory *d) {
    if (!d)
        return fail(9);
    delete d;
    return 0;
}
} // namespace
int descriptor_flags(int fd) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    std::lock_guard lock(d->guard);
    return d->flags;
}
int descriptor_append(int fd) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    std::lock_guard lock(d->guard);
    d->flags |= 1024;
    return 0;
}
HANDLE duplicate_read_file(int fd) {
    auto d = descriptor(fd);
    if (!d)
        return INVALID_HANDLE_VALUE;
    std::lock_guard lock(d->guard);
    if (d->is_socket()) {
        fail(19);
        return INVALID_HANDLE_VALUE;
    }
    if ((d->flags & 3) == 1) {
        fail(13);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE result = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), d->handle, GetCurrentProcess(), &result, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        fail(win_error(GetLastError()));
        return INVALID_HANDLE_VALUE;
    }
    return result;
}
int __cdecl file_open(const char *p, int flags, ...) {
    static const bool trace = (nova::build_interface::developer ? std::getenv("NOVA_TRACE_FILES") : nullptr) != nullptr;
    if (trace)
        log_once("open flags=" + std::to_string(flags) + " path=" + (p ? p : "<null>"));
    const int access = flags & 3;
    if (access == 3)
        return fail(22);
    // Linux O_LARGEFILE/O_CLOEXEC/O_NOCTTY are accepted; unknown flags are rejected.
    if (flags & ~(3 | 64 | 128 | 256 | 512 | 1024 | 2048 | 0x8000 | 0x80000 | 0x10000 | 0x20000))
        return fail(22);
    bool writing = access != 0 || (flags & (64 | 512));
    auto path = map_path(p, writing);
    if (path.empty())
        return -1;
    DWORD disposition = (flags & 64)    ? ((flags & 128)   ? CREATE_NEW
                                           : (flags & 512) ? CREATE_ALWAYS
                                                           : OPEN_ALWAYS)
                        : (flags & 512) ? TRUNCATE_EXISTING
                                        : OPEN_EXISTING;
    DWORD rights = access == 0 ? GENERIC_READ : access == 1 ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE;
    HANDLE h = CreateFileW(
        path.c_str(), rights, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disposition,
        FILE_ATTRIBUTE_NORMAL | ((flags & 0x10000) ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        int code = win_error(GetLastError());
        if (trace)
            log_once("open failed errno=" + std::to_string(code) + " path=" + p);
        return fail(code);
    }
    if (flags & 0x10000) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            CloseHandle(h);
            return fail(20);
        }
    }
    return adopt(h, flags);
}
int __cdecl file_close(int fd) {
    std::lock_guard lock(fd_guard);
    initialize_descriptors();
    auto it = descriptors.find(fd);
    if (it == descriptors.end())
        return fail(9);
    descriptors.erase(it);
    return 0;
}
int __cdecl file_read(int fd, void *bytes, uint32_t size) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    if (d->is_socket())
        return net::read_socket(d, bytes, size);
    if ((d->flags & 3) == 1)
        return fail(9);
    if (size > INT32_MAX)
        return fail(22);
    std::lock_guard lock(d->guard);
    DWORD n = 0;
    if (!ReadFile(d->handle, bytes, size, &n, nullptr)) {
        DWORD e = GetLastError();
        return e == ERROR_HANDLE_EOF || e == ERROR_BROKEN_PIPE ? 0 : fail(win_error(e));
    }
    return static_cast<int>(n);
}
int __cdecl file_write(int fd, const void *bytes, uint32_t size) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    if (d->is_socket())
        return net::write_socket(d, bytes, size);
    if ((d->flags & 3) == 0)
        return fail(9);
    if (size > INT32_MAX)
        return fail(22);
    std::lock_guard lock(d->guard);
    if ((d->flags & 1024) && position(d->handle, 0, 2) < 0)
        return -1;
    DWORD n = 0;
    if (!WriteFile(d->handle, bytes, size, &n, nullptr))
        return fail(win_error(GetLastError()));
    return static_cast<int>(n);
}
int32_t __cdecl file_seek(int fd, int32_t offset, int origin) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    std::lock_guard lock(d->guard);
    if (d->is_socket())
        return fail(29);
    auto n = position(d->handle, offset, origin);
    if (n > INT32_MAX)
        return fail(75);
    return static_cast<int32_t>(n);
}
int __cdecl file_pread(int fd, void *bytes, uint32_t size, int32_t offset) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    if (d->is_socket())
        return fail(29);
    if (offset < 0 || size > INT32_MAX)
        return fail(22);
    if ((d->flags & 3) == 1)
        return fail(9);
    std::lock_guard lock(d->guard);
    auto old = position(d->handle, 0, 1);
    if (old < 0 || position(d->handle, offset, 0) < 0)
        return -1;
    DWORD n = 0;
    BOOL ok = ReadFile(d->handle, bytes, size, &n, nullptr);
    DWORD error = GetLastError();
    position(d->handle, old, 0);
    return ok ? static_cast<int>(n) : fail(win_error(error));
}
int __cdecl file_stat(int fd, Stat *s) {
    auto d = descriptor(fd);
    if (!d)
        return -1;
    if (d->is_socket()) {
        if (!s)
            return fail(14);
        *s = {};
        s->mode = 0140777;
        s->nlink = 1;
        s->blksize = 4096;
        return 0;
    }
    return stat_handle(d->handle, s);
}
int file_temp() {
    auto dir = profile_directory() / "data" / "cache";
    wchar_t path[MAX_PATH];
    if (!GetTempFileNameW(dir.c_str(), L"n3p", 0, path))
        return fail(win_error(GetLastError()));
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        DeleteFileW(path);
        return fail(win_error(e));
    }
    return adopt(h, 2);
}
void register_files(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("open", file_open);
    B("close", file_close);
    B("read", file_read);
    B("write", file_write);
    B("lseek", file_seek);
    B("pread", file_pread);
    B("stat", path_stat);
    B("fstat", file_stat);
    B("mkdir", make_directory);
    B("unlink", unlink_file);
    B("remove", remove_path);
    B("rename", rename_path);
    B("fcntl", control);
    B("ioctl", io_control);
    B("basename", base_name);
    B("opendir", directory_open);
    B("readdir", directory_read);
    B("closedir", directory_close);
#undef B
}
} // namespace nova::bionic
