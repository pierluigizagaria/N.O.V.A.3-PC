#include "bionic/runtime.h"
#include <array>
namespace nova::bionic {
File standard_files[3]{};
namespace {
// Derived from the classification rules in AOSP libc/stdlib/ctype_.c and
// tolower_.c (android-2.3.7_r1), including the BSD high-byte classifications.
constexpr uint8_t bits(int c) {
    if (c < 0)
        return 0;
    if (c < 32)
        return static_cast<uint8_t>(32 | ((c >= 9 && c <= 13) ? 8 : 0));
    if (c == 32)
        return 8 | 128;
    if (c >= 127 && c < 160)
        return 32;
    if (c >= '0' && c <= '9')
        return 4;
    if (c >= 'A' && c <= 'Z')
        return static_cast<uint8_t>(1 | (c <= 'F' ? 64 : 0));
    if (c >= 'a' && c <= 'z')
        return static_cast<uint8_t>(2 | (c <= 'f' ? 64 : 0));
    return 16;
}
constexpr auto ctype_table = [] {
    std::array<uint8_t, 257> t{};
    for (int i = 0; i < 256; ++i)
        t[i + 1] = bits(i);
    return t;
}();
constexpr auto lower_table = [] {
    std::array<int16_t, 257> t{};
    t[0] = -1;
    for (int i = 0; i < 256; ++i)
        t[i + 1] = static_cast<int16_t>(i >= 'A' && i <= 'Z' ? i + 32 : i);
    return t;
}();
const uint8_t *ctype_pointer = ctype_table.data();
const int16_t *lower_pointer = lower_table.data();
struct InterfaceId {
    uint32_t low;
    uint16_t mid, high, sequence;
    uint8_t node[6];
};
InterfaceId engine_id{}, play_id{}, queue_id{};
const InterfaceId *engine_pointer = &engine_id;
const InterfaceId *play_pointer = &play_id;
const InterfaceId *queue_pointer = &queue_id;
} // namespace
uint8_t classify(int c) { return c == -1 ? 0 : ctype_table[static_cast<uint8_t>(c) + 1]; }
void register_data(ImportRegistry &r) {
    initialize_stdio();
    r.data("__sF", standard_files);
    r.data("_ctype_", &ctype_pointer);
    r.data("_tolower_tab_", &lower_pointer);
    r.data("SL_IID_ENGINE", &engine_pointer, true);
    r.data("SL_IID_PLAY", &play_pointer, true);
    r.data("SL_IID_BUFFERQUEUE", &queue_pointer, true);
}
} // namespace nova::bionic
