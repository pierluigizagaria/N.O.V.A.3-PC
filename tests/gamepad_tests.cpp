#include "input/gamepad.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace nova::input;
namespace {
void check(bool value,const char *why) { if(!value) throw std::runtime_error(why); }
SDL_Event key(Uint32 type,SDL_Keycode code) { SDL_Event e{}; e.type=type; e.key.key=code; return e; }
struct VirtualPad {
    SDL_JoystickID id=0;
    SDL_Joystick *joystick=nullptr;
    VirtualPad() {
        SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
        desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        desc.name="NOVA isolated virtual gamepad";
        id=SDL_AttachVirtualJoystick(&desc); check(id!=0,SDL_GetError());
        joystick=SDL_OpenJoystick(id); check(joystick!=nullptr,SDL_GetError());
        // Lower-level virtual joystick triggers use the FULL signed range;
        // SDL_GetGamepadAxis remaps released -32768 to gamepad trigger zero.
        set_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
        set_axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    }
    ~VirtualPad() { if(joystick) SDL_CloseJoystick(joystick); if(id) SDL_DetachVirtualJoystick(id); }
    void set_axis(SDL_GamepadAxis axis,Sint16 value) {
        check(SDL_SetJoystickVirtualAxis(joystick,axis,value),SDL_GetError());
    }
    void button(SDL_GamepadButton button,bool value) {
        check(SDL_SetJoystickVirtualButton(joystick,button,value),SDL_GetError());
    }
};
void pump(Gamepad &pad) {
    SDL_UpdateJoysticks(); SDL_Event e;
    while(SDL_PollEvent(&e)) pad.handle(e);
    pad.poll();
}
}
int main() {
    try {
        check(SDL_Init(SDL_INIT_GAMEPAD),SDL_GetError());
        {
            VirtualPad source,foreign;
            Controls controls(ControlsConfig{}); controls.gameplay=true;
            Gamepad pad(controls,source.id); pad.enabled(true); pump(pad);
            check(pad.device()==source.id,"explicit virtual device identity, never open physical hardware in this harness");
            auto original=controls.snapshot_config();
            foreign.button(SDL_GAMEPAD_BUTTON_SOUTH,true); pump(pad);
            check(!controls.take().down(Action::jump),"a different controller cannot own the selected source");
            source.button(SDL_GAMEPAD_BUTTON_SOUTH,true); pump(pad);
            auto frame=controls.take();
            check(frame.down(Action::jump) && frame.edge(Action::jump),"SDL virtual face button reaches common native action layer");
            frame=controls.take(); check(frame.down(Action::jump) && frame.pressed.empty(),"held pad is persistent, not a per-render pulse");
            controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_SPACE));
            check(controls.take().pressed.empty(),"keyboard joining the same held action cannot manufacture a new edge");
            source.button(SDL_GAMEPAD_BUTTON_SOUTH,false); pump(pad);
            check(controls.take().down(Action::jump),"pad release does not release keyboard ownership");
            controls.handle(key(SDL_EVENT_KEY_UP,SDLK_SPACE));
            check(!controls.take().down(Action::jump),"last independent owner releases the action");
            // Queue-order button edges survive even with no consumer between.
            source.button(SDL_GAMEPAD_BUTTON_WEST,true); pump(pad);
            source.button(SDL_GAMEPAD_BUTTON_WEST,false); pump(pad);
            frame=controls.take(); check(!frame.down(Action::reload) && frame.edge(Action::reload),"sub-consumer reload tap keeps exactly its own edge");
            check(!controls.take().edge(Action::reload),"one-shot pad edge drains exactly once");
            source.set_axis(SDL_GAMEPAD_AXIS_LEFTX,32767);
            source.set_axis(SDL_GAMEPAD_AXIS_LEFTY,-32768);
            source.set_axis(SDL_GAMEPAD_AXIS_RIGHTX,16384); pump(pad);
            frame=controls.take();
            check(frame.gamepad.move_x>0 && frame.gamepad.move_y<0 &&
                  std::abs(std::hypot(frame.gamepad.move_x,frame.gamepad.move_y)-1)<.001f && frame.dx==0 && frame.dy==0,
                  "radial normalization preserves X/Y signs and never creates mouse counts");
            const auto look=frame.gamepad.look_x;
            for(unsigned consumer=0;consumer<120;++consumer)
                check(controls.take().gamepad.look_x==look,"zero/two/many consumer frames sample positions without integrating render time");
            controls.replace_sensitivity({.2f,.7f});
            check(controls.take().gamepad.look_x==look && controls.snapshot_config().keys==original.keys,
                  "mouse normal/ADS sensitivity and remapping are independent of stick samples");
            source.set_axis(SDL_GAMEPAD_AXIS_LEFTX,0); source.set_axis(SDL_GAMEPAD_AXIS_LEFTY,0);
            source.set_axis(SDL_GAMEPAD_AXIS_RIGHTX,0); source.set_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767); pump(pad);
            check(controls.take().down(Action::aim),"trigger supplies physical held desire, never native ADS acknowledgement");
            controls.gameplay=false; controls.context(true,false); pump(pad);
            check(!controls.take().down(Action::aim),"temporary gameplay permission blocks delivery");
            controls.gameplay=true; pump(pad);
            check(controls.take().down(Action::aim),"temporary native refusal does not erase physical held intent");
            controls.context(false,false); pump(pad); controls.context(true,false); pump(pad);
            check(!controls.take().down(Action::aim),"focus loss requires neutral rearm instead of stale held aim");
            source.set_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768); pump(pad);
            source.set_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767); pump(pad);
            check(controls.take().down(Action::aim),"neutral then fresh trigger rearms after focus loss");
            controls.overlay(true); pump(pad); controls.overlay(false); pump(pad);
            check(!controls.take().down(Action::aim),"settings capture cannot leak a held trigger on close");
            source.set_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768); pump(pad);
            controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_SPACE));
            source.button(SDL_GAMEPAD_BUTTON_WEST,true); pump(pad);
            SDL_Event remap{}; remap.type=SDL_EVENT_GAMEPAD_REMAPPED; remap.gdevice.which=source.id; pad.handle(remap);
            frame=controls.take(); check(frame.down(Action::jump) && !frame.requested(Action::reload),"mapping generation revokes only pad ownership and pending edges");
            check(SDL_DetachVirtualJoystick(source.id),SDL_GetError()); source.id=0; pump(pad);
            check(!pad.device() && controls.take().down(Action::jump),"disconnect clears pad without releasing keyboard or adopting another device");
            check(!permit_input(true,false,false,SDL_EVENT_GAMEPAD_BUTTON_DOWN) &&
                  permit_input(true,false,false,SDL_EVENT_GAMEPAD_REMOVED),"script gate rejects physical actions while lifecycle cleanup remains permitted");
        }
        SDL_Quit(); std::puts("isolated SDL virtual gamepad ownership passed");
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); SDL_Quit(); return 1; }
}
