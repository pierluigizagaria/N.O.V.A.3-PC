#pragma once
#include "input/play_gamepad.h"
// Included after the accepted post-drain harness callbacks. It shares their
// installed adapter, later writers and actual original consumer/caller gate.
namespace play_gamepad_fixture {
inline unsigned factory_calls=0;
inline std::unique_ptr<Gamepad> unavailable(Controls &,SDL_JoystickID) {
    ++factory_calls; throw std::runtime_error("controlled SDL gamepad initialization failure");
}
struct Sdl {
    Sdl() { check(SDL_Init(SDL_INIT_GAMEPAD),SDL_GetError()); }
    ~Sdl() { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); }
};
struct VirtualPad {
    SDL_JoystickID id=0;
    SDL_Joystick *joystick=nullptr;
    VirtualPad() {
        SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
        desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        desc.name="NOVA T13c owned virtual source";
        id=SDL_AttachVirtualJoystick(&desc); check(id!=0,SDL_GetError());
        joystick=SDL_OpenJoystick(id); check(joystick!=nullptr,SDL_GetError());
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
        axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    }
    ~VirtualPad() { if(joystick) SDL_CloseJoystick(joystick); if(id) SDL_DetachVirtualJoystick(id); }
    void axis(SDL_GamepadAxis a,Sint16 value) { check(SDL_SetJoystickVirtualAxis(joystick,a,value),SDL_GetError()); }
    void button(SDL_GamepadButton b,bool value) { check(SDL_SetJoystickVirtualButton(joystick,b,value),SDL_GetError()); }
    void sticks(bool held) {
        axis(SDL_GAMEPAD_AXIS_LEFTX,held ? 32767 : 0);
        axis(SDL_GAMEPAD_AXIS_LEFTY,0);
        axis(SDL_GAMEPAD_AXIS_RIGHTX,held ? 16384 : 0);
        axis(SDL_GAMEPAD_AXIS_RIGHTY,held ? 16384 : 0);
    }
    void detach() { check(SDL_DetachVirtualJoystick(id),SDL_GetError()); id=0; }
};
template<class Step>
void run(Controls &input,NativeGamepadPostDrain &native,Step step,uint32_t p,uint32_t actor,
         uint32_t move,uint32_t look,bool omit_poll,bool omit_gate) {
    PlayGamepadContext permitted{true,true};
    // No SDL call at all for scripts, explicit diagnostics, bounded or observer runs,
    // even if the keyboard script policy allows physical input.
    Controls fallback(ControlsConfig{}); fallback.gameplay=true;
    for(auto mode : {0,1,2,3}) {
        PlayGamepad isolated(fallback,PlayGamepad::physical_session(mode==0,mode==1,mode==2,mode==3),0,&unavailable);
        SDL_Event e{}; e.type=SDL_EVENT_GAMEPAD_ADDED;
        isolated.handle(e,permitted); isolated.poll(permitted);
        check(!isolated.available() && !isolated.failed() && factory_calls==0,"diagnostic path never initializes a controller");
    }
    {
        PlayGamepad failed(fallback,true,0,&unavailable);
        failed.poll(permitted); failed.poll(permitted);
        fallback.handle(key(SDL_EVENT_KEY_DOWN,SDLK_SPACE));
        SDL_Event mouse{}; mouse.type=SDL_EVENT_MOUSE_MOTION; mouse.motion.xrel=3; mouse.motion.yrel=-2;
        fallback.handle(mouse); const auto frame=fallback.take();
        check(failed.failed() && !failed.available() && factory_calls==1 && frame.down(Action::jump) &&
            frame.dx==3 && frame.dy==-2 && !fallback.continuous_gamepad().source_present,
            "optional initialization failure is bounded once and preserves keyboard/mouse");
    }
    Sdl sdl;
    VirtualPad source,foreign;
    PlayGamepad route(input,true,source.id); // Never select/open physical or foreign hardware.
    auto pump=[&](PlayGamepadContext state) {
        route.context(omit_gate ? permitted : state);
        SDL_UpdateJoysticks(); SDL_Event event{};
        while(SDL_PollEvent(&event)) route.handle(event,omit_gate ? permitted : state);
        if(!omit_poll) route.poll(omit_gate ? permitted : state);
    };
    source.sticks(false); pump(permitted); step(); // Observe owner, terminally rearm.
    pump(permitted); // Subsequent fresh neutral.
    source.sticks(true); pump(permitted);
    const auto axes=input.continuous_gamepad().sample.axes;
    const auto empty_mouse=input.take();
    const auto yaw=at<float>(actor,0x68),pitch=at<float>(actor,0x60);
    const auto consumed=reinterpret_cast<PostDrainPlayer *>(p)->consumers;
    step(20);
    check(axes.move_x>.99f && axes.look_x>.4f && axes.look_y>.4f && empty_mouse.dx==0 && empty_mouse.dy==0 &&
        nearly_equal(at<float>(p,0x1c),axes.move_x*1.5f) && nearly_equal(at<float>(p,0x24),0) &&
        nearly_equal(at<float>(actor,0x68),yaw-axes.look_x*2.f*20.f*.02f) &&
        nearly_equal(at<float>(actor,0x60),pitch-axes.look_y*2.f),"PLAY_ROUTE_CONSUMER_AXES_MISSING");
    check(route.device()==source.id && reinterpret_cast<PostDrainPlayer *>(p)->consumers==consumed+1 &&
        world->queue_count==0,"UI source reaches one native consumer without queued analog");
    if(world->native_consumer_entry) check(world->velocity_samples>0 && world->velocity_dt==20 &&
        nearly_equal(world->consumed_move_x,axes.move_x*1.5f) && nearly_equal(world->consumed_move_y,0),
        "play routing reaches original ComputeControlVelocity input CALL after original later writers");
    const auto zero_yaw=at<float>(actor,0x68),zero_pitch=at<float>(actor,0x60);
    step(0);
    check(nearly_equal(at<float>(actor,0x68),zero_yaw) && nearly_equal(at<float>(actor,0x60),zero_pitch-axes.look_y*2.f),
        "play preserves native dt zero and per-invocation pitch");
    input.replace_sensitivity({8.f,1.9f}); pump(permitted); step(0);
    check(nearly_equal(at<float>(p,0x48),axes.look_x*2.f),"mouse sensitivity cannot scale gamepad units");
    foreign.button(SDL_GAMEPAD_BUTTON_SOUTH,true); pump(permitted);
    check(!input.take().down(Action::jump),"foreign virtual controller cannot own mapped actions");
    source.button(SDL_GAMEPAD_BUTTON_WEST,true); SDL_UpdateJoysticks();
    source.button(SDL_GAMEPAD_BUTTON_WEST,false); SDL_UpdateJoysticks();
    pump(permitted); // Both queued edges, final polled button already released.
    check(input.take().edge(Action::reload) && !input.take().edge(Action::reload),"queue-order tap survives exactly once");
    // Native later authority remains authoritative at actual consumption.
    callback_mode=10; const auto reset_yaw=at<float>(actor,0x68); step();
    check(nearly_equal(at<float>(actor,0x68),reset_yaw) && at<float>(p,0x1c)==0 && at<float>(p,0x48)==0,
        "later native reset defeats UI-delivered movement/look");
    callback_mode=0;
    at<uint8_t>(move,0x1d)=at<uint8_t>(look,0x1d)=0; step();
    check(at<float>(p,0x1c)==0 && at<float>(p,0x48)==0,"tutorial/native permission refusal remains authoritative");
    at<uint8_t>(move,0x1d)=at<uint8_t>(look,0x1d)=1; step();
    at<uint8_t>(p,0x139)=1; const auto locked_yaw=at<float>(actor,0x68); step();
    check(nearly_equal(at<float>(actor,0x68),locked_yaw) && at<float>(p,0x48)==0,
        "native rotation lock defeats delivered look at the consumer");
    at<uint8_t>(p,0x139)=0; step();
    source.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767); pump(permitted);
    const auto physical_revision=input.gamepad_revision();
    invoke(enable_fn,{p,0}); input.gameplay=false; input.context(true,false); pump(permitted);
    native.target_observed(world->level,p,at(world->level,0x17c),at(at(world->level,0x17c),0xc),false);
    check(input.gamepad_revision()==physical_revision,"temporary native permission is not a UI ownership transition");
    invoke(enable_fn,{p,1}); input.gameplay=true; pump(permitted); step();
    check(input.take().down(Action::aim) && at<float>(p,0x48)>0,"held native ADS desire survives temporary refusal");
    source.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768); pump(permitted);
    callback_mode=12; const auto mouse_yaw=at<float>(actor,0x68),mouse_pitch=at<float>(actor,0x60); step();
    check(nearly_equal(at<float>(actor,0x68),mouse_yaw-.2f) && nearly_equal(at<float>(actor,0x60),mouse_pitch+.3f),
        "later production O07 mouse wins both axes");
    callback_mode=0;
    // Each UI gate clears only the pad and needs a fresh neutral after returning.
    for(unsigned gate=0;gate<7;++gate) {
        auto blocked=permitted;
        if(gate==0) blocked.focused=false; // Includes minimized/hidden production context.
        if(gate==1) blocked.panel=true;
        if(gate==2) blocked.binding_capture=true;
        if(gate==3) blocked.text=true;
        if(gate==4) blocked.transition=true;
        if(gate==5) blocked.quitting=true;
        if(gate==6) blocked.ready=false;
        input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_SPACE));
        pump(blocked); step();
        check(!input.continuous_gamepad().admitted && at<float>(p,0x1c)==0 && at<float>(p,0x48)==0 &&
            world->queue_count==0,"PLAY_ROUTE_GATE_LEFT_NATIVE_ANALOG");
        check(input.take().down(Action::jump),"UI gamepad revocation preserves independent keyboard ownership");
        pump(permitted); step();
        check(!input.continuous_gamepad().admitted && at<float>(p,0x1c)==0,"held controller cannot rearm on focus/capture return");
        source.sticks(false); pump(permitted); source.sticks(true); pump(permitted); step();
        check(at<float>(p,0x1c)>1.f,"fresh neutral then held sample rearms after UI gate");
        input.handle(key(SDL_EVENT_KEY_UP,SDLK_SPACE));
    }
    // Exact-address native epoch revocation uses the installed lifecycle hook.
    invoke(construct,{p,actor}); pump(permitted); step();
    check(!input.continuous_gamepad().admitted && at<float>(p,0x1c)==0,"native reconstruction cannot inherit a held UI sample");
    source.sticks(false); pump(permitted); source.sticks(true); pump(permitted); step();
    check(at<float>(p,0x1c)>1.f,"native epoch requires a subsequent neutral");
    source.detach(); pump(permitted); step();
    check(route.device()==0 && at<float>(p,0x1c)==0 && at<float>(p,0x48)==0 && world->queue_count==0,
        "owned disconnect removes native axes and cannot adopt the foreign device");
    // Absent pad keeps original keyboard queue latency and explicit mouse writer.
    const auto sends=world->immediate,queries=world->acquisitions;
    step(); check(world->acquisitions==queries+1 && world->immediate==sends,"absent pad adds no native acquisition");
    input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); step();
    check(world->queue_count==1 && at<float>(p,0x24)==0,"absent pad preserves keyboard queue latency");
    step(); check(nearly_equal(at<float>(p,0x24),2.5f) && world->immediate==sends,"absent pad keyboard reaches native consumer unchanged");
    input.handle(key(SDL_EVENT_KEY_UP,SDLK_W)); step(); step();
    callback_mode=12; const auto absent_yaw=at<float>(actor,0x68); step(); callback_mode=0;
    check(nearly_equal(at<float>(actor,0x68),absent_yaw-.2f) && world->immediate==sends,"absent pad leaves native mouse writer unchanged");
    route.shutdown();
    // A reconnected instance has a NEW SDL identity; constrain its selection too.
    VirtualPad reconnected; reconnected.sticks(true);
    PlayGamepad reconnect(input,true,reconnected.id);
    auto reconnect_pump=[&] {
        SDL_UpdateJoysticks(); SDL_Event event{};
        while(SDL_PollEvent(&event)) reconnect.handle(event,permitted);
        reconnect.poll(permitted);
    };
    reconnect_pump(); step();
    check(!input.continuous_gamepad().admitted && at<float>(p,0x1c)==0,"reconnection held state cannot bypass neutral rearm");
    reconnected.sticks(false); reconnect_pump(); reconnected.sticks(true); reconnect_pump(); step();
    check(at<float>(p,0x1c)>1.f,"reconnected owned virtual source reaches actual native consumer");
    reconnect.shutdown(); step();
    check(!input.continuous_gamepad().source_present && at<float>(p,0x1c)==0 && at<float>(p,0x48)==0,
        "explicit UI shutdown precedes SDL/Controls teardown and revokes native contribution");
    std::puts("play routing -> owned SDL virtual Gamepad -> Controls -> post-drain/native consumer: passed");
}
}
