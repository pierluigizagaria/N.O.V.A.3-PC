#include "bionic/runtime.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <malloc.h>
namespace nova::bionic {
namespace {
int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int __cdecl compare_case(const char *a, const char *b) {
    for (;; ++a, ++b) {
        int x = lower(static_cast<unsigned char>(*a)), y = lower(static_cast<unsigned char>(*b));
        if (x != y || !x)
            return x - y;
    }
}
int __cdecl compare_ncase(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        int x = lower(static_cast<unsigned char>(a[i])), y = lower(static_cast<unsigned char>(b[i]));
        if (x != y || !x)
            return x - y;
    }
    return 0;
}
size_t __cdecl copy_limit(char *d, const char *s, size_t n) {
    size_t len = std::strlen(s);
    if (n) {
        size_t k = std::min(len, n - 1);
        std::memcpy(d, s, k);
        d[k] = 0;
    }
    return len;
}
size_t __cdecl transform(char *d, const char *s, size_t n) {
    size_t len = std::strlen(s);
    if (n)
        std::strncpy(d, s, n);
    return len;
}
char *__cdecl duplicate(const char *s) {
    size_t n = std::strlen(s) + 1;
    char *p = static_cast<char *>(heap_allocate(n));
    if (p)
        std::memcpy(p, s, n);
    else
        guest_errno = 12;
    return p;
}
const char *__cdecl error_string(int e) {
    switch (e) {
    case 0:
        return "Success";
    case 1:
        return "Operation not permitted";
    case 2:
        return "No such file or directory";
    case 3:
        return "No such process";
    case 4:
        return "Interrupted system call";
    case 5:
        return "Input/output error";
    case 9:
        return "Bad file descriptor";
    case 11:
        return "Resource temporarily unavailable";
    case 12:
        return "Cannot allocate memory";
    case 13:
        return "Permission denied";
    case 14:
        return "Bad address";
    case 16:
        return "Device or resource busy";
    case 17:
        return "File exists";
    case 20:
        return "Not a directory";
    case 21:
        return "Is a directory";
    case 22:
        return "Invalid argument";
    case 24:
        return "Too many open files";
    case 25:
        return "Inappropriate ioctl for device";
    case 28:
        return "No space left on device";
    case 29:
        return "Illegal seek";
    case 30:
        return "Read-only file system";
    case 32:
        return "Broken pipe";
    case 33:
        return "Numerical argument out of domain";
    case 34:
        return "Numerical result out of range";
    case 35:
        return "Resource deadlock avoided";
    case 36:
        return "File name too long";
    case 38:
        return "Function not implemented";
    case 39:
        return "Directory not empty";
    case 75:
        return "Value too large for defined data type";
    case 84:
        return "Invalid or incomplete multibyte or wide character";
    case 95:
        return "Operation not supported";
    case 110:
        return "Connection timed out";
    default:
        return "Unknown error";
    }
}
_locale_t c_locale() {
    static _locale_t loc = _create_locale(LC_ALL, "C");
    return loc;
}
int32_t __cdecl to_long(const char *s, char **end, int base) {
    errno = 0;
    long n = _strtol_l(s, end, base, c_locale());
    if (errno)
        guest_errno = crt_error(errno);
    return n;
}
uint32_t __cdecl to_ulong(const char *s, char **end, int base) {
    errno = 0;
    auto n = _strtoul_l(s, end, base, c_locale());
    if (errno)
        guest_errno = crt_error(errno);
    return n;
}
double __cdecl to_double(const char *s, char **end) {
    errno = 0;
    double n = _strtod_l(s, end, c_locale());
    if (errno)
        guest_errno = crt_error(errno);
    return n;
}
int32_t __cdecl to_int(const char *s) { return to_long(s, nullptr, 10); }
char *__cdecl locale(int, const char *) {
    static char name[] = "C";
    return name;
}
#define CLASSIFY(name, mask)                                                                               \
    int __cdecl name(int c) { return classify(c) & mask; }
CLASSIFY(alnum, 7)
CLASSIFY(alpha, 3)
CLASSIFY(cntrl, 32)
CLASSIFY(digit, 4)
CLASSIFY(graph, 23) CLASSIFY(low, 2) CLASSIFY(print, 151) CLASSIFY(punct, 16) CLASSIFY(space, 8)
    CLASSIFY(upper, 1) CLASSIFY(xdigit, 68) int __cdecl upcase(int c) {
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}
} // namespace
void register_string(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(f))
#define S(n) B(#n, std::n)
    S(memcmp);
    S(memmove);
    S(memcpy);
    S(memset);
    S(strlen);
    S(strcmp);
    S(strcat);
    S(strcpy);
    S(strcspn);
    S(strncat);
    S(strncmp);
    S(strncpy);
    S(strspn);
    S(strtok);
    B("memchr", static_cast<void *(*)(void *, int, size_t)>(&std::memchr));
    B("strchr", static_cast<char *(*)(char *, int)>(&std::strchr));
    B("strrchr", static_cast<char *(*)(char *, int)>(&std::strrchr));
    B("strpbrk", static_cast<char *(*)(char *, const char *)>(&std::strpbrk));
    B("strstr", static_cast<char *(*)(char *, const char *)>(&std::strstr));
    B("strcasecmp", compare_case);
    B("strncasecmp", compare_ncase);
    B("strlcpy", copy_limit);
    B("strdup", duplicate);
    B("strerror", error_string);
    B("strtol", to_long);
    B("strtoul", to_ulong);
    B("strtod", to_double);
    B("atoi", to_int);
    B("atol", to_int);
    B("setlocale", locale);
    B("strcoll", std::strcmp);
    B("strxfrm", transform);
    B("isalnum", alnum);
    B("isalpha", alpha);
    B("iscntrl", cntrl);
    B("isdigit", digit);
    B("isgraph", graph);
    B("islower", low);
    B("isprint", print);
    B("ispunct", punct);
    B("isspace", space);
    B("isupper", upper);
    B("isxdigit", xdigit);
    B("toupper", upcase);
    B("tolower", lower);
#undef S
#undef B
}
} // namespace nova::bionic
