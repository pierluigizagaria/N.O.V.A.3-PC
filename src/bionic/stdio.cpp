#include "bionic/runtime.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
namespace nova::bionic {
namespace {
constexpr int LBF = 1, NBF = 2, RD = 4, WR = 8, RW = 16, END = 32, ERR = 64;
struct Stream {
    std::vector<unsigned char> buffer;
    bool owned = false;
    bool push = false;
};
std::recursive_mutex streams_guard;
std::map<File *, Stream> streams;
int __cdecl cookie_close(void *cookie) { return file_close(static_cast<File *>(cookie)->file); }
int __cdecl cookie_read(void *cookie, char *p, int n) {
    return file_read(static_cast<File *>(cookie)->file, p, n);
}
int __cdecl cookie_write(void *cookie, const char *p, int n) {
    return file_write(static_cast<File *>(cookie)->file, p, n);
}
int32_t __cdecl cookie_seek(void *cookie, int32_t o, int w) {
    return file_seek(static_cast<File *>(cookie)->file, o, w);
}
void setup(File *f, int fd, int flags, bool owned) {
    *f = {};
    f->file = static_cast<int16_t>(fd);
    f->flags = static_cast<int16_t>(flags);
    f->cookie = f;
    f->close = cookie_close;
    f->read = cookie_read;
    f->write = cookie_write;
    f->seek = cookie_seek;
    f->blksize = 4096;
    auto &s = streams[f];
    s.owned = owned;
    s.buffer.resize(1024);
    s.push = false;
    f->bf = {s.buffer.data(), 1024};
    f->p = f->bf.base;
}
bool valid(File *f) {
    if (f && streams.contains(f) && f->flags)
        return true;
    fail(9);
    return false;
}
int mode_flags(const char *m, int &flags) {
    if (!m || !(*m == 'r' || *m == 'w' || *m == 'a'))
        return fail(22);
    bool plus = std::strchr(m, '+') != nullptr;
    flags = plus ? RW : *m == 'r' ? RD : WR;
    return (*m == 'r' ? 0 : *m == 'w' ? 64 | 512 : 64 | 1024) | (plus ? 2 : *m == 'r' ? 0 : 1);
}
void reset_read(File *f) {
    auto &s = streams[f];
    if (s.push) {
        f->p = f->up;
        f->r = f->ur;
        s.push = false;
    }
    f->up = nullptr;
    f->ur = 0;
}
int flush_write(File *f) {
    int count = static_cast<int>(f->p - f->bf.base);
    int sent = 0;
    while (sent < count) {
        int n = file_write(f->file, f->bf.base + sent, count - sent);
        if (n <= 0) {
            f->flags |= ERR;
            return -1;
        }
        sent += n;
    }
    f->p = f->bf.base;
    f->w = (f->flags & (LBF | NBF)) ? 0 : f->bf.size;
    return 0;
}
int write_mode(File *f) {
    if (f->flags & WR)
        return 0;
    if (!(f->flags & RW)) {
        f->flags |= ERR;
        return fail(9);
    }
    if (f->flags & RD) {
        reset_read(f);
        if (f->r > 0 && file_seek(f->file, -f->r, 1) < 0)
            return -1;
    }
    f->flags = static_cast<int16_t>((f->flags & ~(RD | END)) | WR);
    f->r = 0;
    f->p = f->bf.base;
    f->w = (f->flags & (LBF | NBF)) ? 0 : f->bf.size;
    return 0;
}
int read_mode(File *f) {
    if (f->flags & RD)
        return 0;
    if (!(f->flags & RW)) {
        f->flags |= ERR;
        return fail(9);
    }
    if ((f->flags & WR) && flush_write(f) < 0)
        return -1;
    f->flags = static_cast<int16_t>((f->flags & ~WR) | RD);
    f->r = f->w = 0;
    f->p = f->bf.base;
    return 0;
}
File *__cdecl descriptor_open(int fd, const char *mode) {
    std::lock_guard lock(streams_guard);
    int flags;
    int open = mode_flags(mode, flags);
    if (open < 0)
        return nullptr;
    int existing = descriptor_flags(fd);
    if (existing < 0)
        return nullptr;
    if ((flags & RW) && ((existing & 3) != 2)) {
        fail(22);
        return nullptr;
    }
    if ((flags & RD) && (existing & 3) == 1) {
        fail(22);
        return nullptr;
    }
    if ((flags & WR) && (existing & 3) == 0) {
        fail(22);
        return nullptr;
    }
    if ((open & 1024) && descriptor_append(fd) < 0)
        return nullptr;
    auto *f = new File{};
    setup(f, fd, flags, true);
    return f;
}
File *__cdecl reopen(const char *path, const char *mode, File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return nullptr;
    bool owned = streams[f].owned;
    int flags;
    int access = mode_flags(mode, flags);
    stream_flush(f);
    file_close(f->file);
    streams.erase(f);
    f->flags = 0;
    if (access < 0)
        return nullptr;
    int fd = file_open(path, access, 0666);
    if (fd < 0)
        return nullptr;
    setup(f, fd, flags, owned);
    return f;
}
char *__cdecl temporary_name(char *out) {
    // bionic's L_tmpnam is PATH_MAX (1024). Use the app's mapped writable cache.
    static std::atomic<uint32_t> serial{0};
    static thread_local char internal[1024];
    char *result = out ? out : internal;
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        std::snprintf(result, 1024,
            "/data/data/com.gameloft.android.ANMP.GloftN3HM/cache/tmp-%lu-%08x",
            GetCurrentProcessId(), ++serial);
        auto host = map_path(result, true);
        if (host.empty())
            return nullptr;
        if (GetFileAttributesW(host.c_str()) == INVALID_FILE_ATTRIBUTES) {
            auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND)
                return result; // tmpnam never creates/reserves the file.
            fail(win_error(code));
            return nullptr;
        }
    }
    fail(17);
    return nullptr;
}
File *__cdecl temporary() {
    int fd = file_temp();
    return fd < 0 ? nullptr : descriptor_open(fd, "w+");
}
int __cdecl refill(File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f) || read_mode(f) < 0)
        return -1;
    auto &s = streams[f];
    if (s.push) {
        reset_read(f);
        if (f->r > 0) {
            --f->r;
            return *f->p++;
        }
    }
    int n = file_read(f->file, f->bf.base, (f->flags & NBF) ? 1 : f->bf.size);
    f->p = f->bf.base;
    f->r = 0;
    if (n <= 0) {
        f->flags |= n == 0 ? END : ERR;
        return -1;
    }
    f->r = n - 1;
    return *f->p++;
}
int __cdecl put_character(int c, File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f) || write_mode(f) < 0)
        return -1;
    if (f->p - f->bf.base >= f->bf.size && flush_write(f) < 0)
        return -1;
    *f->p++ = static_cast<unsigned char>(c);
    f->w = (f->flags & (LBF | NBF)) ? 0 : f->bf.size - static_cast<int>(f->p - f->bf.base);
    if (((f->flags & NBF) || ((f->flags & LBF) && c == '\n')) && flush_write(f) < 0)
        return -1;
    return static_cast<unsigned char>(c);
}
char *__cdecl get_line(char *p, int n, File *f) {
    if (!p || n <= 0) {
        fail(22);
        return nullptr;
    }
    int i = 0;
    for (; i < n - 1;) {
        int c = stream_get(f);
        if (c < 0)
            break;
        p[i++] = static_cast<char>(c);
        if (c == '\n')
            break;
    }
    p[i] = 0;
    return i || n == 1 ? p : nullptr;
}
int __cdecl put_string(const char *p, File *f) {
    auto n = std::strlen(p);
    return stream_write(p, 1, n, f) == n ? 0 : -1;
}
int __cdecl console_char(int c) { return put_character(c, &standard_files[1]); }
int __cdecl console_line(const char *p) {
    return put_string(p, &standard_files[1]) < 0 ? -1 : console_char('\n');
}
void __cdecl rewind_file(File *f) {
    stream_seek(f, 0, 0);
    if (f)
        f->flags &= ~(END | ERR);
}
int __cdecl set_buffer(File *f, char *p, int mode, size_t size) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return -1;
    if (mode < 0 || mode > 2 || size > INT32_MAX)
        return fail(22);
    if (stream_flush(f) < 0)
        return -1;
    auto &s = streams[f];
    s.push = false;
    if (mode == 2) {
        f->bf = {f->nbuf, 1};
    } else if (p && size) {
        f->bf = {reinterpret_cast<unsigned char *>(p), static_cast<int>(size)};
    } else {
        s.buffer.resize(size ? size : 1024);
        f->bf = {s.buffer.data(), static_cast<int>(s.buffer.size())};
    }
    f->flags = static_cast<int16_t>((f->flags & ~(LBF | NBF)) | mode);
    f->p = f->bf.base;
    f->r = 0;
    f->w = (f->flags & WR) && mode == 0 ? f->bf.size : 0;
    f->lbfsize = mode == 1 ? -f->bf.size : 0;
    return 0;
}
int __cdecl file_number(File *f) { return valid(f) ? f->file : -1; }
int __cdecl end_of_file(File *f) { return valid(f) ? (f->flags & END) != 0 : 0; }
int __cdecl error_of_file(File *f) { return valid(f) ? (f->flags & ERR) != 0 : 0; }
void __cdecl clear_error(File *f) {
    if (valid(f))
        f->flags &= ~(END | ERR);
}
uint32_t __cdecl wide_get(File *f) {
    int c = stream_get(f);
    if (c < 0)
        return UINT32_MAX;
    if (c > 127) {
        fail(84);
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(c);
}
uint32_t __cdecl wide_put(uint32_t c, File *f) {
    if (c > 127) {
        fail(84);
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(put_character(c, f));
}
uint32_t __cdecl wide_unget(uint32_t c, File *f) {
    if (c > 127)
        return UINT32_MAX;
    return static_cast<uint32_t>(stream_unget(static_cast<int>(c), f));
}
} // namespace
void initialize_stdio() {
    std::lock_guard lock(streams_guard);
    if (!streams.empty())
        return;
    setup(&standard_files[0], 0, RD, false);
    setup(&standard_files[1], 1, WR, false);
    setup(&standard_files[2], 2, WR | NBF, false);
}
File *__cdecl stream_open(const char *path, const char *mode) {
    std::lock_guard lock(streams_guard);
    int flags;
    int access = mode_flags(mode, flags);
    if (access < 0)
        return nullptr;
    int fd = file_open(path, access, 0666);
    if (fd < 0)
        return nullptr;
    auto *f = new File{};
    setup(f, fd, flags, true);
    return f;
}
int __cdecl stream_close(File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return -1;
    int result = (f->flags & WR) ? flush_write(f) : 0;
    if (file_close(f->file) < 0)
        result = -1;
    bool owned = streams[f].owned;
    streams.erase(f);
    f->flags = 0;
    if (owned)
        delete f;
    return result;
}
int __cdecl stream_get(File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return -1;
    return --f->r < 0 ? refill(f) : *f->p++;
}
int __cdecl stream_unget(int c, File *f) {
    std::lock_guard lock(streams_guard);
    if (c < 0 || !valid(f) || read_mode(f) < 0)
        return -1;
    auto &s = streams[f];
    if (f->p > f->bf.base && f->p <= f->bf.base + f->bf.size && !s.push) {
        *--f->p = static_cast<unsigned char>(c);
        ++f->r;
    } else {
        if (!s.push) {
            f->up = f->p;
            f->ur = std::max(f->r, 0);
            f->p = f->ubuf + 3;
            f->r = 0;
            s.push = true;
        }
        if (f->p == f->ubuf)
            return -1;
        *--f->p = static_cast<unsigned char>(c);
        ++f->r;
    }
    f->flags &= ~END;
    return static_cast<unsigned char>(c);
}
size_t __cdecl stream_read(void *p, size_t size, size_t count, File *f) {
    std::lock_guard lock(streams_guard);
    if (!size || !count)
        return 0;
    if (count > SIZE_MAX / size) {
        fail(75);
        return 0;
    }
    auto *b = static_cast<unsigned char *>(p);
    size_t n = 0;
    for (; n < size * count; ++n) {
        int c = stream_get(f);
        if (c < 0)
            break;
        b[n] = static_cast<unsigned char>(c);
    }
    return n / size;
}
size_t __cdecl stream_write(const void *p, size_t size, size_t count, File *f) {
    std::lock_guard lock(streams_guard);
    if (!size || !count)
        return 0;
    if (count > SIZE_MAX / size) {
        fail(75);
        return 0;
    }
    auto *b = static_cast<const unsigned char *>(p);
    size_t n = 0;
    for (; n < size * count; ++n)
        if (put_character(b[n], f) < 0)
            break;
    return n / size;
}
int __cdecl stream_flush(File *f) {
    std::lock_guard lock(streams_guard);
    if (!f) {
        int result = 0;
        for (auto &[stream, state] : streams) {
            (void)state;
            if ((stream->flags & WR) && flush_write(stream) < 0)
                result = -1;
        }
        return result;
    }
    if (!valid(f))
        return -1;
    if (f->flags & WR)
        return flush_write(f);
    if (f->flags & RD) {
        reset_read(f);
        if (f->r > 0 && file_seek(f->file, -f->r, 1) < 0)
            return -1;
        f->r = 0;
        f->p = f->bf.base;
    }
    return 0;
}
int __cdecl stream_seek(File *f, int32_t o, int whence) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return -1;
    if (whence < 0 || whence > 2)
        return fail(22);
    if (f->flags & WR) {
        if (flush_write(f) < 0)
            return -1;
    } else {
        reset_read(f);
        if (whence == 1)
            o -= std::max(f->r, 0);
    }
    if (file_seek(f->file, o, whence) < 0)
        return -1;
    f->r = f->w = 0;
    f->p = f->bf.base;
    f->flags &= ~END;
    return 0;
}
int32_t __cdecl stream_tell(File *f) {
    std::lock_guard lock(streams_guard);
    if (!valid(f))
        return -1;
    int32_t n = file_seek(f->file, 0, 1);
    if (n < 0)
        return -1;
    if (f->flags & WR)
        return n + static_cast<int32_t>(f->p - f->bf.base);
    return n - std::max(f->r, 0) - (streams[f].push ? f->ur : 0);
}
void register_stdio(ImportRegistry &r) {
    initialize_stdio();
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("fopen", stream_open);
    B("fdopen", descriptor_open);
    B("freopen", reopen);
    B("tmpfile", temporary);
    B("tmpnam", temporary_name);
    B("fclose", stream_close);
    B("fread", stream_read);
    B("fwrite", stream_write);
    B("fgets", get_line);
    B("fputs", put_string);
    B("fputc", put_character);
    B("putc", put_character);
    B("__swbuf", put_character);
    B("getc", stream_get);
    B("fgetc", stream_get);
    B("__srget", refill);
    B("ungetc", stream_unget);
    B("fseek", stream_seek);
    B("ftell", stream_tell);
    B("rewind", rewind_file);
    B("fflush", stream_flush);
    B("setvbuf", set_buffer);
    B("putchar", console_char);
    B("puts", console_line);
    B("fileno", file_number);
    B("feof", end_of_file);
    B("ferror", error_of_file);
    B("clearerr", clear_error);
    B("getwc", wide_get);
    B("putwc", wide_put);
    B("ungetwc", wide_unget);
#undef B
}
} // namespace nova::bionic
