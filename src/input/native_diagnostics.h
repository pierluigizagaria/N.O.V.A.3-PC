#pragma once
#include "loader/elf_image.h"
#include <cstdio>

namespace nova::input {
// Diagnostic scheduling only; callers retain their operational frame work.
class NativeDiagnostics {
  public:
    explicit NativeDiagnostics(bool enabled = false) : enabled_(enabled) {}
    template<class Sample> void end_frame(uint64_t frame, bool gameplay, bool observed, Sample sample) {
        if (!enabled_) return;
        if (observed) response_until_ = frame + 180;
        if (gameplay && (observed || frame % 120 == 0 ||
            (frame <= response_until_ && frame % 30 == 0))) sample(observed ? "response" : "periodic");
    }
  private:
    bool enabled_;
    uint64_t response_until_ = 0;
};

// The native snapshot is shared by the real frame callback and synthetic ELF
// regression. The owning adapter resolves addresses before calling it.
struct NativeSnapshot {
    const ElfImage &image;
    uint32_t player, normal, scheme;
    uint32_t power_ammo, can_use_power, power_type;
    uint64_t frame;
    float rush_heading;
};
void sample_native_controls(const NativeSnapshot &snapshot, const char *reason, FILE *output = stdout);
}
