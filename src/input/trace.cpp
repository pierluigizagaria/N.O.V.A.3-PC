#include "input/trace.h"

namespace nova::input {
void Trace::dispatch(const SDL_Event &event, uint64_t frame, bool script_source, bool native) {
    if (event.type == SDL_EVENT_MOUSE_MOTION && !script_source && !detailed_motion_)
        return;
    if (input_event(event.type) || script_source ||
        event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        std::fprintf(output_,"input-dispatch frame=%llu source=%s type=%u native=%d\n",
            frame,script_source ? "script" : "physical",event.type,native);
}
bool Trace::look_begin(uint64_t frame, const FrameInput &input, Look delta, float limit,
                       float yaw, float pitch) {
    // Even infrequent synchronous output can stall a frame. Normal gameplay
    // consumes every delta without optional look/response diagnostics.
    look_logged_ = detailed_motion_;
    if (look_logged_) {
        before_yaw_ = yaw; before_pitch_ = pitch;
        std::fprintf(output_,"native-look frame=%llu counts=%.2f,%.2f delta=%.3f,%.3f limit=%.1f before=%.3f,%.3f\n",
            frame,input.dx,input.dy,delta.yaw,delta.pitch,limit,yaw,pitch);
    }
    return look_logged_;
}
void Trace::look_end(uint64_t frame, float yaw, float pitch) {
    if (look_logged_)
        std::fprintf(output_,"native-look-readback frame=%llu yaw=%.3f pitch=%.3f change=%.3f,%.3f\n",
            frame,yaw,pitch,yaw-before_yaw_,pitch-before_pitch_);
}
}
