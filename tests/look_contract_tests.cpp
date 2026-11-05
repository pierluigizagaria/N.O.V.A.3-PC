#include "input/native_adapter.h"
#include "input/native_ads.h"
#include "abi/guest_hook.h"
#include "fixtures/look_contract.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace nova::input;
namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void near(float actual, float expected, const char *message) {
    if (!std::isfinite(actual) || std::abs(actual-expected) > .0001f) {
        std::fprintf(stderr,"actual=%.9f expected=%.9f: ",actual,expected);
        throw std::runtime_error(message);
    }
}
bool same(const GuestLookInput &a, const GuestLookInput &b) {
    return std::memcmp(&a,&b,sizeof(a)) == 0;
}
static_assert(sizeof(ContractInput) == sizeof(GuestLookInput));
static_assert(offsetof(ContractInput,pitch_override) == offsetof(GuestLookInput,pitch_override));
struct Harness {
    explicit Harness(uint32_t consumer) : consumer(consumer), controls(ControlsConfig{}) {
        controls.gameplay = true;
    }
    uint32_t consumer;
    Controls controls;
    GuestLookInput guest{}, dispatched{};
    ContractActor actor{30,10}, readback_actor{};
    ContractObservation seen{};
    FrameInput frame_input;
    NativeAimScale aim;
    bool pending = false, enabled = true, locked = false, selected_player = true;
    unsigned frame = 0, prepared_frame = 0, prepares = 0, readbacks = 0, gate_calls = 0;
    void begin(unsigned id) { frame = id; pending = false; }
    void motion(float x, float y) {
        SDL_Event event{}; event.type=SDL_EVENT_MOUSE_MOTION;
        event.motion.xrel=x; event.motion.yrel=y;
        controls.handle(event);
    }
    void drain() {
        frame_input = controls.active() ? controls.take() : FrameInput{};
        pending = frame_input.dx != 0 || frame_input.dy != 0;
    }
    void consume(unsigned dt = 7, float limit = 60) {
        // This is the production boundary, not a copy of its pending/ordering
        // policy. Object lookups are replaced with explicit synthetic state.
        mouse_look_boundary(pending,[&] {
            ++gate_calls;
            return controls.active() && selected_player && enabled && !locked;
        },[&] {
            auto delta = look_delta(frame_input.dx,frame_input.dy,actor.pitch,limit,
                controls.look_config(aim.factor,aim.aiming));
            queue_mouse_look(guest,actor.yaw,actor.pitch,delta);
            prepared_frame=frame; ++prepares;
        },[&] {
            dispatched=guest;
            seen.frame=frame; seen.locked=locked; seen.milliseconds=dt; seen.limit=limit;
            nova::abi::invoke(consumer,{reinterpret_cast<uint32_t>(&guest),
                reinterpret_cast<uint32_t>(&actor),reinterpret_cast<uint32_t>(&seen)});
        },[&] { readback_actor=actor; ++readbacks; });
    }
};
GuestLookInput sentinel() {
    GuestLookInput result{4,6,1,100,1,{0x11,0x22,0x33},40,3,2};
    return result;
}
void guest_branches(uint32_t consumer) {
    Harness h(consumer); h.actor={0,0}; h.guest=sentinel();
    h.guest.pitch_override=0; h.guest.pitch_delta=0;
    h.begin(1); h.consume(10);
    near(h.actor.yaw,99,"native absolute yaw then evolving rate");
    near(h.guest.yaw_rate,5,"native yaw rate step");
    check(!h.guest.yaw_pending,"native absolute target consumed");
    h.guest={}; h.guest.yaw_rate=1; h.guest.yaw_rate_target=1.0005f;
    h.consume(); near(h.guest.yaw_rate,1.0005f,"native near-target rate snap");
    for (float sign : {-1.f,1.f}) {
        h.actor={0,0}; h.guest={}; h.guest.pitch_target=5*sign; h.guest.pitch_delta=2*sign;
        for (int i=0;i<3;++i) h.consume();
        near(h.actor.pitch,5*sign,"native target overshoot corrected in either direction");
        check(h.guest.pitch_delta == 0 && h.seen.nonzero_pitch_calls == 2,
              "overshoot correction clears native delta, independent of primitive call count");
        h.consume(); near(h.seen.effect_pitch_change,0,"completed native target does not replay");
        h.actor.pitch=0; h.guest.pitch_target=-5*sign; h.guest.pitch_delta=2*sign;
        h.consume(); near(h.actor.pitch,0,"opposite target does not consume signed delta");
        near(h.guest.pitch_delta,2*sign,"opposite native target leaves native input intact");
        h.actor.pitch=59*sign; h.guest.pitch_override=2*sign;
        h.consume(); near(h.actor.pitch,60*sign,"persistent priority pitch obeys native limit");
        near(h.guest.pitch_override,2*sign,"priority input persists after consumption");
        h.consume(); near(h.actor.pitch,60*sign,"repeated priority at limit stays clamped");
    }
    h.guest=sentinel(); h.actor={7,8}; h.locked=true;
    auto before=h.guest; h.consume();
    check(same(h.guest,before),"native lock preserves all pending input fields");
    near(h.actor.yaw,7,"native lock prevents yaw"); near(h.actor.pitch,8,"native lock prevents pitch");
}
void shared_samples(uint32_t consumer) {
    struct Case { float dx,dy,yaw,pitch; bool invert; };
    for (const auto &c : {Case{8,0,29,10,false},Case{-8,0,31,10,false},
                         Case{0,8,30,9,false},Case{0,-8,30,11,false},
                         Case{8,-8,29,11,false},Case{-8,8,31,9,false},
                         Case{8,-8,29,9,true},Case{-8,8,31,11,true}}) {
        Harness h(consumer); h.controls.replace_sensitivity({.125f,1.f});
        h.controls.config.invert_y=c.invert; h.guest=sentinel();
        h.begin(57); h.motion(c.dx/4,c.dy/4); h.motion(c.dx/4,c.dy/4); h.motion(c.dx/2,c.dy/2); h.drain();
        h.consume();
        check(h.prepares==1 && h.readbacks==1 && h.prepared_frame==57 && h.seen.frame==57 && !h.pending,
              "one summed sample prepared and consumed in the intended frame");
        near(h.seen.entry_yaw,30,"shared phase starts before host yaw");
        near(h.seen.entry_pitch,10,"shared phase starts before host pitch");
        near(h.seen.velocity_yaw,c.yaw,"velocity observes same-frame yaw");
        near(h.seen.velocity_pitch,c.pitch,"velocity observes same-frame pitch");
        near(h.readback_actor.yaw,c.yaw,"readback runs after native yaw consumption");
        near(h.readback_actor.pitch,c.pitch,"readback runs after native pitch consumption");
        near(h.seen.effect_yaw_change,c.yaw-30,"effect observation includes host yaw");
        near(h.seen.effect_pitch_change,c.pitch-10,"effect observation includes host pitch");
        check(h.dispatched.yaw_rate==0 && h.dispatched.yaw_rate_target==0 &&
              h.dispatched.yaw_rate_step==0 && h.dispatched.pitch_override==0,
              "eligible mouse sample owns both axes, including X-only and Y-only");
        check(h.dispatched.padding[0]==0x11 && h.dispatched.padding[2]==0x33,
              "queue writes stay within known fields");
        check(h.guest.pitch_delta==0 && !h.guest.yaw_pending,"native sample reaches target in this call");
        auto after=h.guest;
        h.consume(); // Repeated hook callback in the same frame.
        check(h.prepares==1 && h.readbacks==1 && h.gate_calls==1,"second callback does not requeue or reread eligibility");
        near(h.actor.yaw,c.yaw,"same-frame callback does not replay yaw");
        near(h.actor.pitch,c.pitch,"same-frame callback does not replay pitch");
        h.begin(58); h.drain(); h.consume();
        check(same(h.dispatched,after),"following no-motion frame preserves native input fields");
        near(h.actor.yaw,c.yaw,"following frame has no stale yaw");
        near(h.actor.pitch,c.pitch,"following frame has no stale pitch");
    }
}
void native_aim_scaling(uint32_t consumer) {
    Harness h(consumer); h.controls.replace_sensitivity({.2f,1.f});
    // Authored native-layout storage, not original Aim execution. Strict reads
    // connect the production resolver and multiplier to the ELF look consumer.
    float factor=1; uint8_t pending=0,active=0;
    const auto read32=[](uint32_t p,uint32_t o) -> uint32_t {
        if(p==0x1000 && o==0x170) return 0x2000;
        if(p==0x2000 && o==0x160) return 0;
        if(p==0x2000 && o==0x148) return 0x3000;
        if(p==0x2000 && o==0x14c) return 0x3018;
        if(p==0x3000 && o==12) return 0x4000;
        throw std::runtime_error("unexpected native factor pointer read");
    };
    auto step=[&](unsigned frame,float expected) {
        h.aim=current_native_aim_scale(0x1000,read32,
            [&](uint32_t p,uint32_t o) -> uint8_t {
                check(p==0x4000 && (o==0x60 || o==0x61),"current weapon flags only");
                return o==0x60 ? pending : active;
            },[&](uint32_t p,uint32_t o) {
                check(p==0x1000 && o==0x2c,"native published sensitivity field"); return factor;
            });
        h.actor={0,0}; h.begin(frame); h.motion(10,20); h.drain(); h.consume();
        near(h.actor.yaw,-10*expected,"native consumer receives scaled yaw once");
        near(h.actor.pitch,-20*expected,"native consumer receives scaled pitch once");
        h.consume(31);
        near(h.actor.yaw,-10*expected,"repeated consumer does not compound aim factor");
    };
    step(1,.2f);                        // Hip.
    factor=.5f; pending=1; step(2,.1f); // Aim has published before active+61.
    pending=0; active=1; step(3,.1f);
    factor=.25f; step(4,.05f);         // Live sniper zoom update.
    h.controls.replace_sensitivity({.2f,1.5f}); step(5,.075f);
    h.controls.replace_sensitivity({.4f,1.5f}); step(6,.15f);
    h.controls.replace_sensitivity({.4f,0.f}); step(7,0.f);
    factor=1; active=0; step(8,.4f);   // Original exit/reset restores hip.
}
void native_ownership(uint32_t consumer) {
    for (int gate=0;gate<7;++gate) {
        Harness h(consumer); h.guest=sentinel();
        h.begin(90); h.motion(8,-8); h.drain();
        if (gate==0) h.enabled=false;
        if (gate==1) h.locked=true;
        if (gate==2) h.selected_player=false;
        if (gate==3) h.controls.context(false,false);
        if (gate==4) h.controls.context(true,true);
        if (gate==5) h.controls.gameplay=false;
        if (gate==6) h.begin(91); // No native consumer occurred before the next frame.
        auto before=h.guest; h.consume();
        check(h.prepares==0 && h.readbacks==0 && same(h.dispatched,before),
              "disabled/locked/inactive/wrong-player/expired samples do not overwrite native input");
        h.enabled=true; h.locked=false; h.selected_player=true;
        h.controls.gameplay=true; h.controls.context(true,false);
        h.begin(92); h.drain(); auto remaining=h.guest; h.consume();
        check(h.prepares==0 && same(h.dispatched,remaining),"old host sample is not delayed to a later eligible frame");
    }
    Harness h(consumer); h.guest=sentinel(); h.begin(100);
    h.motion(4,-7); h.motion(-4,7); h.drain(); h.consume();
    check(h.prepares==0 && same(h.dispatched,sentinel()),"cancelling counts leave both native axes alone");
    near(h.actor.pitch,12,"no host sample still permits independent native priority pitch");
    h.begin(101); h.drain(); h.consume();
    near(h.actor.pitch,14,"persistent native priority is not mistaken for host replay");
    h.guest={}; h.actor.pitch=60; h.begin(102); h.motion(0,-5); h.drain(); h.consume();
    check(h.prepares==1,"outward motion at the clamp is a sample, not an empty frame");
    near(h.actor.pitch,60,"outward mouse sample stays clamped");
}
void numeric_baseline(uint32_t consumer) {
    Harness h(consumer); h.actor.pitch=0;
    const float counts[]={-1,-3,-5,-9,-10,-9,-6,-2,1};
    const float expected[]={.1f,.4f,.9f,1.8f,2.8f,3.7f,4.3f,4.5f,4.4f};
    for (unsigned i=0;i<9;++i) {
        h.begin(20263+i); h.motion(0,counts[i]); h.drain(); h.consume(7,88);
        near(h.actor.pitch,expected[i],"original user's vertical sequence has the same numeric result");
    }
    for (bool inverted : {false,true}) for (unsigned parts : {1u,4u,100u}) {
        Harness split(consumer); split.controls.config.invert_y=inverted;
        for (unsigned i=0;i<parts;++i) {
            split.begin(i+1); split.motion(120.f/parts,-600.f/parts); split.drain();
            split.consume(i%2 ? 3 : 29);
        }
        near(split.actor.yaw,18,"mouse yaw is independent of monotonic partition and dt");
        near(split.actor.pitch,inverted ? -50.f : 60.f,"pitch retains sign, inversion, clamp and monotonic partition");
    }
    for (float initial : {-59.f,59.f}) {
        Harness limit(consumer); limit.actor.pitch=initial;
        limit.begin(1); limit.motion(0,initial>0 ? -100.f : 100.f); limit.drain(); limit.consume();
        near(limit.actor.pitch,initial>0 ? 60.f : -60.f,"both mouse pitch limits");
        limit.begin(2); limit.motion(0,initial>0 ? 10.f : -10.f); limit.drain(); limit.consume();
        near(limit.actor.pitch,initial,"reversal leaves clamp immediately");
    }
}
}
int main(int argc, char **argv) {
    try {
        check(argc==2,"synthetic fixture path required");
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        auto consumer=nova::abi::require_function(image,"guest_consume_look_contract");
        guest_branches(consumer);
        shared_samples(consumer);
        native_aim_scaling(consumer);
        native_ownership(consumer);
        numeric_baseline(consumer);
        // Known ordering hazards are negative controls for the richer observer.
        Harness old_yaw(consumer); old_yaw.actor.yaw+=12;
        old_yaw.guest.yaw_target=30; old_yaw.guest.yaw_pending=1; old_yaw.consume();
        near(old_yaw.actor.yaw,30,"fixture detects the old early-yaw overwrite");
        Harness split_pitch(consumer); split_pitch.actor.pitch-=4; split_pitch.consume();
        near(split_pitch.seen.entry_pitch,6,"fixture detects split early pitch at native entry");
        near(split_pitch.seen.effect_pitch_change,0,"early pitch has a different within-consumer observation");
        std::puts("coherent look contract passed: same-frame axes, once-only samples, native branches, gates, original pitch sequence");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"look contract test: %s\n",error.what()); return 1;
    }
}
