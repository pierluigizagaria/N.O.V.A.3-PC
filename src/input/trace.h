#pragma once
#include "input/controls.h"
#include <cstdio>

namespace nova::input {
// One instance per producing thread. Script runs retain detailed motion traces;
// ordinary sessions must not perform synchronous I/O at the mouse polling rate.
class Trace {
  public:
    explicit Trace(bool detailed_motion = false, FILE *output = stdout)
        : detailed_motion_(detailed_motion), output_(output) {}
    void dispatch(const SDL_Event &event, uint64_t frame, bool script_source, bool native);
    bool look_begin(uint64_t frame, const FrameInput &input, Look delta, float limit,
                    float yaw, float pitch);
    void look_end(uint64_t frame, float yaw, float pitch);
  private:
    bool detailed_motion_, look_logged_ = false;
    FILE *output_;
    float before_yaw_ = 0, before_pitch_ = 0;
};
}
