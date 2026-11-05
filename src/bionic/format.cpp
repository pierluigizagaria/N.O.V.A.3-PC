#include "bionic/runtime.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>
namespace nova::bionic {
namespace {
struct Args {
    char *p;
    char *base = p;
    std::vector<size_t> offsets;
    template <class T> T next(size_t index = 0) {
        T v;
        std::memcpy(&v, index ? base + offsets.at(index) : p, sizeof v);
        if (!index)
            p += (sizeof(T) + 3) & ~size_t(3);
        return v;
    }
};
struct FormatError {};
int number(const char *&p) {
    int n = 0;
    while (*p >= '0' && *p <= '9') {
        if (n > 100000)
            throw FormatError{};
        n = n * 10 + (*p++ - '0');
    }
    return n;
}
std::string length(const char *&p) {
    std::string n;
    if (std::strchr("hljztLq", *p) && *p) {
        n += *p++;
        if ((n == "h" && *p == 'h') || (n == "l" && *p == 'l'))
            n += *p++;
    }
    return n;
}
uint64_t integer(Args &a, const std::string &len, bool sign, size_t index) {
    if (len == "ll" || len == "j" || len == "q")
        return a.next<uint64_t>(index);
    uint32_t v = a.next<uint32_t>(index);
    if (len == "hh")
        return sign ? static_cast<uint64_t>(static_cast<int8_t>(v)) : static_cast<uint8_t>(v);
    if (len == "h")
        return sign ? static_cast<uint64_t>(static_cast<int16_t>(v)) : static_cast<uint16_t>(v);
    return sign ? static_cast<uint64_t>(static_cast<int32_t>(v)) : v;
}
void store_number(void *p, const std::string &len, uint64_t v) {
    if (len == "hh")
        *static_cast<uint8_t *>(p) = static_cast<uint8_t>(v);
    else if (len == "h")
        *static_cast<uint16_t *>(p) = static_cast<uint16_t>(v);
    else if (len == "ll" || len == "j" || len == "q")
        std::memcpy(p, &v, 8);
    else
        *static_cast<uint32_t *>(p) = static_cast<uint32_t>(v);
}
size_t argument_index(const char *&p) {
    const char *start = p;
    int n = number(p);
    if (*p == '$') {
        ++p;
        if (!n || n > 4096)
            throw FormatError{};
        return static_cast<size_t>(n);
    }
    p = start;
    return 0;
}
std::vector<size_t> positional_offsets(const char *fmt) {
    bool positional = false;
    for (const char *q = fmt; *q; ++q)
        if (*q == '%') {
            ++q;
            while (*q && !std::strchr("diuoxXpfFeEgGaAcsn%", *q)) {
                if (*q == '$')
                    positional = true;
                ++q;
            }
            if (!*q)
                break;
        }
    if (!positional)
        return {};
    std::vector<size_t> sizes(1);
    auto add = [&](size_t index, size_t bytes) {
        if (!index)
            throw FormatError{};
        if (sizes.size() <= index)
            sizes.resize(index + 1);
        if (sizes[index] && sizes[index] != bytes)
            throw FormatError{};
        sizes[index] = bytes;
    };
    for (const char *p = fmt; *p;) {
        if (*p++ != '%')
            continue;
        if (*p == '%') {
            ++p;
            continue;
        }
        size_t index = argument_index(p);
        while (*p && std::strchr("-+ #0", *p))
            ++p;
        if (*p == '*') {
            ++p;
            add(argument_index(p), 4);
        } else
            number(p);
        if (*p == '.') {
            ++p;
            if (*p == '*') {
                ++p;
                add(argument_index(p), 4);
            } else
                number(p);
        }
        auto len = length(p);
        char spec = *p++;
        if (!spec)
            throw FormatError{};
        bool wide = (std::strchr("diuoxX", spec) && (len == "ll" || len == "j" || len == "q")) ||
                    std::strchr("fFeEgGaA", spec);
        add(index, wide ? 8 : 4);
    }
    size_t offset = 0;
    for (size_t i = 1; i < sizes.size(); ++i) {
        if (!sizes[i])
            throw FormatError{};
        auto bytes = sizes[i];
        sizes[i] = offset;
        offset += bytes;
    }
    return sizes;
}
bool space(int c) { return c == ' ' || (c >= 9 && c <= 13); }
} // namespace
std::string format(const char *fmt, char *args) {
    Args a{args, args, positional_offsets(fmt)};
    std::string out;
    for (const char *p = fmt; *p;) {
        if (*p != '%') {
            out += *p++;
            continue;
        }
        ++p;
        if (*p == '%') {
            out += *p++;
            continue;
        }
        size_t index = argument_index(p);
        bool left = false, plus = false, blank = false, alt = false, zero = false;
        for (bool more = true; more;) {
            switch (*p) {
            case '-':
                left = true;
                break;
            case '+':
                plus = true;
                break;
            case ' ':
                blank = true;
                break;
            case '#':
                alt = true;
                break;
            case '0':
                zero = true;
                break;
            default:
                more = false;
                continue;
            }
            ++p;
        }
        int width = *p == '*' ? (++p, a.next<int>(argument_index(p))) : number(p);
        if (width < 0) {
            if (width == INT32_MIN)
                throw FormatError{};
            left = true;
            width = -width;
        }
        int precision = -1;
        if (*p == '.') {
            ++p;
            precision = *p == '*' ? (++p, a.next<int>(argument_index(p))) : number(p);
        }
        if (width > 1000000 || precision > 1000000)
            throw FormatError{};
        std::string len = length(p);
        char c = *p++;
        if (!c)
            throw FormatError{};
        std::string value, prefix;
        bool numeric = false;
        if (std::strchr("diuoxXp", c)) {
            bool signed_value = c == 'd' || c == 'i';
            uint64_t v = c == 'p' ? a.next<uint32_t>(index) : integer(a, len, signed_value, index);
            numeric = true;
            if (signed_value && static_cast<int64_t>(v) < 0) {
                prefix = "-";
                v = 0 - v;
            } else if (signed_value && plus)
                prefix = "+";
            else if (signed_value && blank)
                prefix = " ";
            unsigned base = c == 'o' ? 8 : c == 'x' || c == 'X' || c == 'p' ? 16 : 10;
            char buf[65];
            auto end = std::to_chars(buf, buf + 65, v, base);
            value.assign(buf, end.ptr);
            if (precision == 0 && v == 0)
                value.clear();
            if (precision > static_cast<int>(value.size()))
                value.insert(0, precision - value.size(), '0');
            if (c == 'p' || ((c == 'x' || c == 'X') && alt && v))
                prefix += (c == 'X' ? "0X" : "0x");
            if (c == 'o' && alt && (value.empty() || value[0] != '0'))
                value.insert(0, 1, '0');
            if (c == 'X')
                for (auto &ch : value)
                    if (ch >= 'a' && ch <= 'f')
                        ch -= 32;
            if (precision >= 0)
                zero = false;
        } else if (c == 's' || c == 'c') {
            zero = false;
            if (c == 'c') {
                uint32_t v = a.next<uint32_t>();
                if (len == "l" && v > 127) {
                    guest_errno = 84;
                    throw FormatError{};
                }
                value += static_cast<char>(v);
            } else if (len == "l") {
                auto *s = a.next<const uint32_t *>(index);
                if (!s)
                    value = "(null)";
                else
                    for (; *s && (precision < 0 || static_cast<int>(value.size()) < precision); ++s) {
                        if (*s > 127) {
                            guest_errno = 84;
                            throw FormatError{};
                        }
                        value += static_cast<char>(*s);
                    }
            } else {
                auto *s = a.next<const char *>(index);
                if (!s)
                    s = "(null)";
                size_t n = 0;
                while (s[n] && (precision < 0 || n < static_cast<size_t>(precision)))
                    ++n;
                value.assign(s, n);
            }
            if (precision >= 0 && c == 's' && value.size() > static_cast<size_t>(precision))
                value.resize(precision);
        } else if (c == 'n') {
            store_number(a.next<void *>(index), len, out.size());
            continue;
        } else if (std::strchr("fFeEgGaA", c)) {
            // Android x86 long double is binary64, including the L modifier.
            double v = a.next<double>(index);
            numeric = true;
            if (std::signbit(v)) {
                prefix = "-";
                v = -v;
            } else if (plus)
                prefix = "+";
            else if (blank)
                prefix = " ";
            char lower = static_cast<char>(c | 32);
            std::chars_format style = lower == 'f'   ? std::chars_format::fixed
                                      : lower == 'e' ? std::chars_format::scientific
                                      : lower == 'a' ? std::chars_format::hex
                                                     : std::chars_format::general;
            int digits = precision < 0 ? 6 : precision;
            if (lower == 'g' && digits == 0)
                digits = 1;
            std::vector<char> buf(static_cast<size_t>(digits) + 1024);
            auto result = lower == 'a' && precision < 0
                              ? std::to_chars(buf.data(), buf.data() + buf.size(), v, style)
                              : std::to_chars(buf.data(), buf.data() + buf.size(), v, style, digits);
            if (result.ec != std::errc{})
                throw FormatError{};
            value.assign(buf.data(), result.ptr);
            if (lower == 'a' && std::isfinite(v))
                prefix += "0x";
            if (alt && std::isfinite(v)) {
                size_t exp = value.find_first_of("ep");
                if (exp == std::string::npos)
                    exp = value.size();
                if (value.find('.') == std::string::npos) {
                    value.insert(exp, 1, '.');
                    ++exp;
                }
                if (lower == 'g') {
                    int significant = 0;
                    bool started = false;
                    for (size_t i = 0; i < exp; ++i)
                        if (value[i] >= '0' && value[i] <= '9') {
                            if (value[i] != '0')
                                started = true;
                            if (started)
                                ++significant;
                        }
                    if (!started)
                        significant = 1;
                    if (significant < digits)
                        value.insert(exp, digits - significant, '0');
                }
            }
            if (c >= 'A' && c <= 'Z') {
                for (auto &ch : value)
                    if (ch >= 'a' && ch <= 'z')
                        ch -= 32;
                for (auto &ch : prefix)
                    if (ch == 'x')
                        ch = 'X';
            }
            if (!std::isfinite(v))
                zero = false;
        } else {
            log_once(std::string("unsupported printf conversion: ") + c);
            guest_errno = 22;
            throw FormatError{};
        }
        size_t total = prefix.size() + value.size(),
               padding = width > static_cast<int>(total) ? width - total : 0;
        if (!left && !(zero && numeric))
            out.append(padding, ' ');
        out += prefix;
        if (!left && zero && numeric)
            out.append(padding, '0');
        out += value;
        if (left)
            out.append(padding, ' ');
    }
    return out;
}
namespace {
int __cdecl vprint_buffer(char *dest, size_t cap, const char *fmt, char *args) {
    try {
        auto value = format(fmt, args);
        if (cap) {
            size_t n = std::min(value.size(), cap - 1);
            std::memcpy(dest, value.data(), n);
            dest[n] = 0;
        }
        return static_cast<int>(value.size());
    } catch (...) {
        if (cap && dest)
            dest[0] = 0;
        if (!guest_errno)
            guest_errno = 22;
        return -1;
    }
}
int __cdecl print_buffer(char *dest, size_t cap, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vprint_buffer(dest, cap, fmt, a);
    va_end(a);
    return n;
}
int __cdecl vprint_unbounded(char *dest, const char *fmt, char *args) {
    return vprint_buffer(dest, SIZE_MAX, fmt, args);
}
int __cdecl print_unbounded(char *dest, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vprint_unbounded(dest, fmt, a);
    va_end(a);
    return n;
}
int __cdecl vprint_file(File *f, const char *fmt, char *args) {
    try {
        auto s = format(fmt, args);
        return stream_write(s.data(), 1, s.size(), f) == s.size() ? static_cast<int>(s.size()) : -1;
    } catch (...) {
        if (!guest_errno)
            guest_errno = 22;
        return -1;
    }
}
int __cdecl print_file(File *f, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vprint_file(f, fmt, a);
    va_end(a);
    return n;
}
int __cdecl print_console(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vprint_file(&standard_files[1], fmt, a);
    va_end(a);
    return n;
}
struct Input {
    std::function<int()> take;
    std::function<void(int)> undo;
    size_t n = 0;
    int get() {
        int c = take();
        if (c >= 0)
            ++n;
        return c;
    }
    void back(int c) {
        if (c >= 0) {
            undo(c);
            --n;
        }
    }
};
int scan_input(Input &in, const char *format, char *arguments) {
    Args args{arguments, arguments, {}};
    int assigned = 0;
    bool converted = false;
    auto failure = [&](bool eof) { return eof && !converted ? -1 : assigned; };
    for (const char *f = format; *f;) {
        if (space(*f)) {
            while (space(*f))
                ++f;
            int c;
            do {
                c = in.get();
            } while (space(c));
            in.back(c);
            continue;
        }
        if (*f != '%') {
            int c = in.get();
            if (c != *f) {
                in.back(c);
                return failure(c < 0);
            }
            ++f;
            continue;
        }
        ++f;
        if (*f == '%') {
            int c = in.get();
            if (c != '%') {
                in.back(c);
                return failure(c < 0);
            }
            ++f;
            continue;
        }
        bool suppress = *f == '*';
        if (suppress)
            ++f;
        int width = number(f);
        if (!width)
            width = INT32_MAX;
        auto len = length(f);
        char spec = *f++;
        if (!spec)
            return assigned;
        if (spec == 'n') {
            if (!suppress)
                store_number(args.next<void *>(), len, in.n);
            converted = true;
            continue;
        }
        if (spec != 'c' && spec != '[') {
            int c;
            do {
                c = in.get();
            } while (space(c));
            in.back(c);
        }
        bool accepted[256]{};
        bool invert = false;
        if (spec == '[') {
            invert = *f == '^';
            if (invert)
                ++f;
            if (*f == ']')
                accepted[static_cast<unsigned char>(*f++)] = true;
            while (*f && *f != ']') {
                unsigned char first = *f++;
                if (*f == '-' && f[1] && f[1] != ']') {
                    ++f;
                    unsigned char last = *f++;
                    for (unsigned v = first; v <= last; ++v)
                        accepted[v] = true;
                } else
                    accepted[first] = true;
            }
            if (*f == ']')
                ++f;
        }
        if (spec == 'c' || spec == 's' || spec == '[') {
            if (spec == 'c' && width == INT32_MAX)
                width = 1;
            std::string token;
            int c = -1;
            while (static_cast<int>(token.size()) < width) {
                c = in.get();
                if (c < 0)
                    break;
                if ((spec == 's' && space(c)) ||
                    (spec == '[' && (accepted[static_cast<uint8_t>(c)] == invert))) {
                    in.back(c);
                    break;
                }
                token += static_cast<char>(c);
            }
            if (token.empty())
                return failure(c < 0);
            if (!suppress) {
                void *ptr = args.next<void *>();
                if (len == "l") {
                    auto *out = static_cast<uint32_t *>(ptr);
                    for (size_t i = 0; i < token.size(); ++i) {
                        if (static_cast<unsigned char>(token[i]) > 127) {
                            guest_errno = 84;
                            return failure(false);
                        }
                        out[i] = static_cast<unsigned char>(token[i]);
                    }
                    if (spec != 'c')
                        out[token.size()] = 0;
                } else {
                    std::memcpy(ptr, token.data(), token.size());
                    if (spec != 'c')
                        static_cast<char *>(ptr)[token.size()] = 0;
                }
                ++assigned;
            }
            converted = true;
            continue;
        }
        bool floating = std::strchr("aAeEfFgG", spec) != nullptr;
        if (!floating && !std::strchr("diuoxXp", spec)) {
            log_once("unsupported scanf conversion");
            return assigned;
        }
        std::string token;
        int c = -1;
        while (static_cast<int>(token.size()) < width) {
            c = in.get();
            if (c < 0)
                break;
            bool allowed =
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == 'x' ||
                c == 'X' || c == '+' || c == '-' ||
                (floating && (c == '.' || c == 'p' || c == 'P' || c == 'i' || c == 'I' || c == 'n' ||
                              c == 'N' || c == 't' || c == 'T' || c == 'y' || c == 'Y'));
            if (!allowed) {
                in.back(c);
                break;
            }
            token += static_cast<char>(c);
        }
        if (token.empty())
            return failure(c < 0);
        const char *first = token.data();
        const char *end = first + token.size();
        bool negative = false;
        if (first != end && (*first == '+' || *first == '-')) {
            negative = *first == '-';
            ++first;
        }
        size_t used = 0;
        uint64_t value = 0;
        double real = 0;
        if (floating) {
            auto style = std::chars_format::general;
            if (end - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X')) {
                first += 2;
                style = std::chars_format::hex;
            }
            auto result = std::from_chars(first, end, real, style);
            if (result.ec == std::errc::invalid_argument)
                return failure(false);
            used = static_cast<size_t>(result.ptr - token.data());
            if (negative)
                real = -real;
        } else {
            int base = spec == 'o' ? 8 : spec == 'x' || spec == 'X' || spec == 'p' ? 16 : 10;
            if (spec == 'i' && first != end && *first == '0')
                base = 8;
            if (end - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X') &&
                (spec == 'i' || base == 16)) {
                base = 16;
                first += 2;
            }
            auto result = std::from_chars(first, end, value, base);
            if (result.ec == std::errc::invalid_argument) {
                for (auto it = token.rbegin(); it != token.rend(); ++it)
                    in.back(static_cast<unsigned char>(*it));
                return failure(false);
            }
            used = static_cast<size_t>(result.ptr - token.data());
            if (negative)
                value = 0 - value;
        }
        for (size_t i = token.size(); i > used; --i)
            in.back(static_cast<unsigned char>(token[i - 1]));
        if (!suppress) {
            auto *ptr = args.next<void *>();
            if (floating) {
                if (len == "l" || len == "L")
                    *static_cast<double *>(ptr) = real;
                else
                    *static_cast<float *>(ptr) = static_cast<float>(real);
            } else
                store_number(ptr, len, value);
            ++assigned;
        }
        converted = true;
    }
    return assigned;
}
int __cdecl scan_string(const char *s, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int n = scan(s, f, a);
    va_end(a);
    return n;
}
int __cdecl scan_file(File *stream, const char *f, ...) {
    va_list a;
    va_start(a, f);
    Input in{[&] { return stream_get(stream); }, [&](int c) { stream_unget(c, stream); }};
    int n;
    try {
        n = scan_input(in, f, a);
    } catch (...) {
        n = fail(22);
    }
    va_end(a);
    return n;
}
} // namespace
int scan(const char *text, const char *fmt, char *a, size_t *consumed) {
    size_t pos = 0;
    Input in{[&] { return text[pos] ? static_cast<unsigned char>(text[pos++]) : -1; }, [&](int) { --pos; }};
    int n;
    try {
        n = scan_input(in, fmt, a);
    } catch (...) {
        n = fail(22);
    }
    if (consumed)
        *consumed = in.n;
    return n;
}
void register_format(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("printf", print_console);
    B("fprintf", print_file);
    B("sprintf", print_unbounded);
    B("snprintf", print_buffer);
    B("vsprintf", vprint_unbounded);
    B("vsnprintf", vprint_buffer);
    B("vfprintf", vprint_file);
    B("sscanf", scan_string);
    B("fscanf", scan_file);
#undef B
}
} // namespace nova::bionic
