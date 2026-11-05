#include "bionic/runtime.h"
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

namespace nova::bionic {
namespace {
// Old-bionic C-locale conversion behavior. wchar_t/wint_t
// are 32 bits in this guest; the Windows 16-bit wchar_t CRT cannot be forwarded.
// This conversion has no mutable host locale dependency.
uint32_t __cdecl character_class(const char *name) {
    struct Class {
        const char *name;
        uint32_t mask;
    };
    static constexpr Class classes[] = {{"alnum", 7},  {"alpha", 3},  {"blank", 128}, {"cntrl", 32},
                                        {"digit", 4},  {"graph", 23}, {"lower", 2},   {"print", 151},
                                        {"punct", 16}, {"space", 8},  {"upper", 1},   {"xdigit", 68}};
    for (const auto &item : classes)
        if (std::strcmp(name, item.name) == 0)
            return item.mask;
    return 0;
}
int __cdecl wide_to_byte(uint32_t character) { return character < 128 ? static_cast<int>(character) : -1; }
uint32_t __cdecl byte_to_wide(int character) {
    return character >= 0 && character < 128 ? static_cast<uint32_t>(character) : UINT32_MAX;
}

size_t __cdecl wide_length(const uint32_t *p) {
    size_t n = 0;
    while (p[n])
        ++n;
    return n;
}
uint32_t *__cdecl wide_copy(uint32_t *d, const uint32_t *s) {
    size_t n = wide_length(s);
    std::memcpy(d, s, (n + 1) * 4);
    return d;
}
int __cdecl wide_compare(const uint32_t *a, const uint32_t *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a < *b ? -1 : *a > *b ? 1 : 0;
}
size_t __cdecl wide_transform(uint32_t *d, const uint32_t *s, size_t n) {
    size_t len = wide_length(s);
    if (n) {
        size_t k = std::min(n, len + 1);
        std::memcpy(d, s, k * 4);
    }
    return len;
}
uint32_t *__cdecl wide_find(const uint32_t *p, uint32_t c, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i] == c)
            return const_cast<uint32_t *>(p + i);
    return nullptr;
}
int __cdecl wide_memcompare(const uint32_t *a, const uint32_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}
uint32_t *__cdecl wide_memcopy(uint32_t *d, const uint32_t *s, size_t n) {
    std::memcpy(d, s, n * 4);
    return d;
}
uint32_t *__cdecl wide_memmove(uint32_t *d, const uint32_t *s, size_t n) {
    std::memmove(d, s, n * 4);
    return d;
}
uint32_t *__cdecl wide_memset(uint32_t *d, uint32_t c, size_t n) {
    for (size_t i = 0; i < n; ++i)
        d[i] = c;
    return d;
}
int __cdecl wide_class(uint32_t c, uint32_t mask) {
    return c < 256 ? classify(static_cast<int>(c)) & mask : 0;
}
uint32_t __cdecl wide_lower(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
uint32_t __cdecl wide_upper(uint32_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
size_t __cdecl to_wide(uint32_t *out, const char *s, size_t n, void *) {
    if (!s)
        return 0;
    if (!n)
        return SIZE_MAX - 1;
    uint8_t c = static_cast<uint8_t>(*s);
    if (c > 127) {
        guest_errno = 84;
        return SIZE_MAX;
    }
    if (out)
        *out = c;
    return c ? 1 : 0;
}
size_t __cdecl from_wide(char *out, uint32_t c, void *) {
    if (!out)
        return 1;
    if (c > 127) {
        guest_errno = 84;
        return SIZE_MAX;
    }
    *out = static_cast<char>(c);
    return 1;
}
size_t __cdecl wide_time(uint32_t *out, size_t n, const uint32_t *fmt, const Tm *t) {
    std::string format;
    while (*fmt) {
        if (*fmt > 127)
            return 0;
        format += static_cast<char>(*fmt++);
    }
    std::vector<char> buffer(n);
    std::tm host{};
    std::memcpy(&host, t, 36);
    size_t count = std::strftime(buffer.data(), n, format.c_str(), &host);
    if (count)
        for (size_t i = 0; i <= count; ++i)
            out[i] = static_cast<unsigned char>(buffer[i]);
    return count;
}
} // namespace
void register_wchar(ImportRegistry &registry) {
#define B(n, f) registry.function(n, reinterpret_cast<const void *>(&f))
    B("wcslen", wide_length);
    B("wcscpy", wide_copy);
    B("wcscmp", wide_compare);
    B("wcscoll", wide_compare);
    B("wcsxfrm", wide_transform);
    B("wmemchr", wide_find);
    B("wmemcmp", wide_memcompare);
    B("wmemcpy", wide_memcopy);
    B("wmemmove", wide_memmove);
    B("wmemset", wide_memset);
    B("iswctype", wide_class);
    B("towlower", wide_lower);
    B("towupper", wide_upper);
    B("mbrtowc", to_wide);
    B("wcrtomb", from_wide);
    B("wcsftime", wide_time);
#undef B
    registry.function("wctype", reinterpret_cast<const void *>(&character_class));
    registry.function("wctob", reinterpret_cast<const void *>(&wide_to_byte));
    registry.function("btowc", reinterpret_cast<const void *>(&byte_to_wide));
}
} // namespace nova::bionic
