#include "bionic/runtime.h"
#include <cerrno>
#include <cmath>
namespace nova::bionic {
namespace {
// Both ABIs return float/double in x87 ST(0) on i386. Synthetic ELF tests
// consume the return in guest code; no 64-bit/SSE ABI assumptions are used.
#define UNARY(name)                                                                                          \
    double __cdecl m_##name(double v) {                                                                      \
        errno = 0;                                                                                           \
        double r = std::name(v);                                                                             \
        if (errno)                                                                                           \
            guest_errno = crt_error(errno);                                                                  \
        return r;                                                                                            \
    }
#define BINARY(name)                                                                                         \
    double __cdecl m_##name(double a, double b) {                                                            \
        errno = 0;                                                                                           \
        double r = std::name(a, b);                                                                          \
        if (errno)                                                                                           \
            guest_errno = crt_error(errno);                                                                  \
        return r;                                                                                            \
    }
// clang-format off
UNARY(acos)
UNARY(asin)
UNARY(atan)
UNARY(ceil)
UNARY(cos)
UNARY(cosh)
UNARY(exp)
UNARY(floor)
UNARY(log)
UNARY(log10)
UNARY(sin)
UNARY(sinh)
UNARY(sqrt)
UNARY(tan)
UNARY(tanh)
BINARY(atan2)
BINARY(fmod)
BINARY(pow)
// clang-format on
double __cdecl m_frexp(double v, int *exponent) { return std::frexp(v, exponent); }
double __cdecl m_ldexp(double v, int exponent) {
    errno = 0;
    double r = std::ldexp(v, exponent);
    if (errno)
        guest_errno = crt_error(errno);
    return r;
}
double __cdecl m_modf(double v, double *integral) { return std::modf(v, integral); }
} // namespace
void register_math(ImportRegistry &r) {
#define M(n) r.function(#n, reinterpret_cast<const void *>(&m_##n))
    M(acos);
    M(asin);
    M(atan);
    M(atan2);
    M(ceil);
    M(cos);
    M(cosh);
    M(exp);
    M(floor);
    M(fmod);
    M(frexp);
    M(ldexp);
    M(log);
    M(log10);
    M(modf);
    M(pow);
    M(sin);
    M(sinh);
    M(sqrt);
    M(tan);
    M(tanh);
#undef M
}
} // namespace nova::bionic
