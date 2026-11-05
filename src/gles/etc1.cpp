#include "gles/runtime.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nova::gles {
std::vector<uint8_t> decode_etc1(int width, int height, std::span<const uint8_t> blocks) {
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        throw std::invalid_argument("Invalid ETC1 dimensions");
    const size_t bw = (static_cast<size_t>(width) + 3) / 4;
    const size_t bh = (static_cast<size_t>(height) + 3) / 4;
    if (blocks.size() != bw * bh * 8)
        throw std::invalid_argument("Invalid ETC1 block count");
    // ETC1 selectors use a magnitude bit and a separate sign bit.
    constexpr int modifiers[8][4] = {{2, 8, -2, -8},       {5, 17, -5, -17},    {9, 29, -9, -29},
                                     {13, 42, -13, -42},   {18, 60, -18, -60},  {24, 80, -24, -80},
                                     {33, 106, -33, -106}, {47, 183, -47, -183}};
    auto be32 = [](const uint8_t *p) {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    auto signed3 = [](unsigned v) { return (v & 4) ? static_cast<int>(v) - 8 : static_cast<int>(v); };
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t by = 0; by < bh; ++by) {
        for (size_t bx = 0; bx < bw; ++bx) {
            auto p = blocks.data() + (by * bw + bx) * 8;
            uint32_t high = be32(p), low = be32(p + 4);
            int color[2][3]{};
            for (unsigned c = 0; c < 3; ++c) {
                unsigned shift = 24 - c * 8;
                if (high & 2) {
                    int first = static_cast<int>((high >> (shift + 3)) & 31);
                    int second = std::clamp(first + signed3((high >> shift) & 7), 0, 31);
                    color[0][c] = (first << 3) | (first >> 2);
                    color[1][c] = (second << 3) | (second >> 2);
                } else {
                    color[0][c] = static_cast<int>((high >> (shift + 4)) & 15) * 17;
                    color[1][c] = static_cast<int>((high >> shift) & 15) * 17;
                }
            }
            unsigned tables[] = {(high >> 5) & 7, (high >> 2) & 7};
            for (unsigned y = 0; y < 4; ++y) {
                for (unsigned x = 0; x < 4; ++x) {
                    size_t px = bx * 4 + x, py = by * 4 + y;
                    if (px >= static_cast<size_t>(width) || py >= static_cast<size_t>(height))
                        continue;
                    unsigned k = x * 4 + y;
                    unsigned selector = ((low >> k) & 1) | (((low >> (k + 16)) & 1) << 1);
                    unsigned half = (high & 1) ? y / 2 : x / 2;
                    int delta = modifiers[tables[half]][selector];
                    for (unsigned c = 0; c < 3; ++c)
                        rgb[(py * width + px) * 3 + c] =
                            static_cast<uint8_t>(std::clamp(color[half][c] + delta, 0, 255));
                }
            }
        }
    }
    return rgb;
}
} // namespace nova::gles
