#pragma once
#include "input/controls.h"
namespace nova::input {
// Existing NativeControls::apply keyboard path, factored unchanged so the
// post-drain fixture exercises the actual later queued writer. No pad axes.
template<class Enabled,class Event>
void queue_normal_keyboard_movement(const FrameInput &input,bool &moving,bool &observed,Enabled enabled,Event event) {
    float x=static_cast<float>(input.down(Action::right))-static_cast<float>(input.down(Action::left));
    float y=static_cast<float>(input.down(Action::back))-static_cast<float>(input.down(Action::forward));
    if(x && y) { x*=0.70710678118f; y*=0.70710678118f; }
    if((x || y) && enabled()) { event(13,x,y); moving=true; }
    else if(moving) { event(14,0.f,0.f); moving=false; observed=true; }
}
}
