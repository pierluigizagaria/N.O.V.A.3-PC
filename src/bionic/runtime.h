#pragma once
#include "bionic/imports.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
namespace nova::bionic {
extern thread_local int guest_errno;
int fail(int error);
void *__cdecl heap_allocate(size_t size);
void __cdecl heap_release(void *p);
size_t __cdecl heap_usable_size(void *p);
int win_error(DWORD error);
int crt_error(int error);
void log_once(const std::string &message);
// ABI contracts: AOSP bionic android-2.3.7_r1 libc/include/{stdio,dirent,sys/stat}.h.
// i386 aligns 64-bit members to four bytes, unlike MSVC's default.
#pragma pack(push, 4)
struct Buffer {
    unsigned char *base;
    int32_t size;
};
struct File {
    unsigned char *p;
    int32_t r, w;
    int16_t flags, file;
    Buffer bf;
    int32_t lbfsize;
    void *cookie;
    int(__cdecl *close)(void *);
    int(__cdecl *read)(void *, char *, int);
    int32_t(__cdecl *seek)(void *, int32_t, int);
    int(__cdecl *write)(void *, const char *, int);
    Buffer ext;
    unsigned char *up;
    int32_t ur;
    unsigned char ubuf[3], nbuf[1];
    Buffer lb;
    int32_t blksize, offset;
};
struct Stat {
    uint64_t dev;
    uint8_t pad0[4];
    uint32_t ino32, mode, nlink, uid, gid;
    uint64_t rdev;
    uint8_t pad3[4];
    int64_t size;
    uint32_t blksize;
    uint64_t blocks;
    uint32_t atime, atime_nsec, mtime, mtime_nsec, ctime, ctime_nsec;
    uint64_t ino;
};
struct Dirent {
    uint64_t ino;
    int64_t off;
    uint16_t reclen;
    uint8_t type;
    char name[256];
};
struct Timespec {
    int32_t seconds, nanoseconds;
};
struct ThreadAttr {
    uint32_t flags, stack_base, stack_size, guard_size;
    int32_t policy, priority;
};
struct Tm {
    int32_t sec, min, hour, mday, mon, year, wday, yday, isdst, gmtoff;
    const char *zone;
};
#pragma pack(pop)
static_assert(sizeof(File) == 84 && offsetof(File, file) == 14 && offsetof(File, bf) == 16);
static_assert(sizeof(Stat) == 96 && offsetof(Stat, size) == 44 && offsetof(Stat, ino) == 88);
static_assert(sizeof(Dirent) == 276 && offsetof(Dirent, name) == 19);
static_assert(sizeof(ThreadAttr) == 24 && sizeof(Tm) == 44);
extern File standard_files[3];
uint8_t classify(int c);
struct PathConfig {
    std::string profile = "default";
    std::filesystem::path profile_root, obb;
};
void configure_paths(const PathConfig &config);
std::filesystem::path profile_directory();
std::filesystem::path map_path(const char *path, bool write = false);
int __cdecl file_open(const char *path, int flags, ...);
int __cdecl file_close(int fd);
int __cdecl file_read(int fd, void *bytes, uint32_t size);
int __cdecl file_write(int fd, const void *bytes, uint32_t size);
int32_t __cdecl file_seek(int fd, int32_t offset, int origin);
int __cdecl file_stat(int fd, Stat *out);
int file_temp();
int descriptor_flags(int fd);
int descriptor_append(int fd);
HANDLE duplicate_read_file(int fd);
// One process-scoped JNI runtime registers cleanup for guest pthread exit.
void set_thread_cleanup(void(__cdecl *cleanup)());
// Host-owned callback workers use the guest errno, pthread identity and TLS cleanup contract.
void enter_host_thread();
void leave_host_thread();
void initialize_stdio();
File *__cdecl stream_open(const char *, const char *);
int __cdecl stream_close(File *);
int __cdecl stream_get(File *);
int __cdecl stream_unget(int, File *);
size_t __cdecl stream_read(void *, size_t, size_t, File *);
size_t __cdecl stream_write(const void *, size_t, size_t, File *);
int __cdecl stream_flush(File *);
int __cdecl stream_seek(File *, int32_t, int);
int32_t __cdecl stream_tell(File *);
std::string format(const char *, char *args);
int scan(const char *, const char *, char *, size_t *consumed = nullptr);
void register_files(ImportRegistry &);
void register_stdio(ImportRegistry &);
void register_format(ImportRegistry &);
void register_misc(ImportRegistry &);
void register_math(ImportRegistry &);
void register_boot(ImportRegistry &);
} // namespace nova::bionic
