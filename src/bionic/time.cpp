#include "bionic/runtime.h"
#include <Windows.h>
#include <cstring>
#include <ctime>

namespace nova::bionic {
namespace {
// bionic clock_t is signed 32-bit; CLOCKS_PER_SEC is 1,000,000.
// Windows CRT clock() instead measures wall time in milliseconds.
struct Timeval {
    int32_t seconds, microseconds;
};
struct Timezone {
    int32_t minutes_west, dst;
};
static_assert(sizeof(Timeval) == 8 && sizeof(Timezone) == 8);
int __cdecl time_of_day(Timeval *value, Timezone *zone) {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    const uint64_t ticks = ((uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime) - 116444736000000000ULL;
    if (value) {
        value->seconds = static_cast<int32_t>(ticks / 10000000);
        value->microseconds = static_cast<int32_t>((ticks / 10) % 1000000);
    }
    // Android kernel timezone policy is UTC; localtime uses the configured Windows zone.
    if (zone)
        *zone = {};
    return 0;
}
int32_t __cdecl process_clock() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return -1;
    const uint64_t kernel_ticks = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
    const uint64_t user_ticks = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    return static_cast<int32_t>((kernel_ticks + user_ticks) / 10);
}

int32_t __cdecl epoch_time(int32_t *out) {
    Timeval now{};
    time_of_day(&now, nullptr);
    if (out)
        *out = now.seconds;
    return now.seconds;
}
Tm *broken_time(const int32_t *t, Tm *out, bool local) {
    if (!t || !out) {
        fail(14);
        return nullptr;
    }
    __time64_t value = *t;
    std::tm host{};
    int error = local ? _localtime64_s(&host, &value) : _gmtime64_s(&host, &value);
    if (error) {
        fail(crt_error(error));
        return nullptr;
    }
    std::memcpy(out, &host, 36);
    out->gmtoff = 0;
    out->zone = "UTC";
    if (local) {
        _tzset();
        long west = 0, dst = 0;
        _get_timezone(&west);
        _get_dstbias(&dst);
        out->gmtoff = -west - (host.tm_isdst > 0 ? dst : 0);
        thread_local char zone[128];
        size_t count = 0;
        _get_tzname(&count, zone, sizeof zone, host.tm_isdst > 0 ? 1 : 0);
        out->zone = zone;
    }
    return out;
}
Tm *__cdecl utc_r(const int32_t *t, Tm *out) { return broken_time(t, out, false); }
Tm *__cdecl local_r(const int32_t *t, Tm *out) { return broken_time(t, out, true); }
thread_local Tm shared_tm{};
Tm *__cdecl utc(const int32_t *t) { return utc_r(t, &shared_tm); }
Tm *__cdecl local(const int32_t *t) { return local_r(t, &shared_tm); }
int32_t __cdecl make_time(Tm *t) {
    if (!t)
        return fail(14);
    std::tm host{};
    std::memcpy(&host, t, 36);
    auto value = _mktime64(&host);
    if (value < INT32_MIN || value > INT32_MAX)
        return fail(75);
    int32_t v = static_cast<int32_t>(value);
    if (value != -1)
        local_r(&v, t);
    return v;
}
double __cdecl difference(int32_t a, int32_t b) { return static_cast<double>(a) - b; }
size_t __cdecl format_time(char *out, size_t size, const char *fmt, const Tm *t) {
    if (!t || !fmt) {
        fail(22);
        return 0;
    }
    std::tm host{};
    std::memcpy(&host, t, 36);
    return std::strftime(out, size, fmt, &host);
}
} // namespace
void register_time(ImportRegistry &registry) {
#define B(n, f) registry.function(n, reinterpret_cast<const void *>(&f))
    B("time", epoch_time);
    B("gmtime", utc);
    B("gmtime_r", utc_r);
    B("localtime", local);
    B("localtime_r", local_r);
    B("mktime", make_time);
    B("difftime", difference);
    B("strftime", format_time);
#undef B
    registry.function("gettimeofday", reinterpret_cast<const void *>(&time_of_day));
    registry.function("clock", reinterpret_cast<const void *>(&process_clock));
}
} // namespace nova::bionic
