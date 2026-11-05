#include "input/native_controls.h"
#include "bionic/runtime.h"
#include "fixtures/mech_contract.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
void close_to(float a,float b,const char *message) { check(std::isfinite(a) && std::abs(a-b)<.0001f,message); }
uint32_t address(auto &value) { return reinterpret_cast<uint32_t>(&value); }
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
SDL_Event key(Uint32 type,SDL_Keycode code) { SDL_Event e{}; e.type=type; e.key.key=code; return e; }
struct Harness;
Harness *stop_harness=nullptr;
uint32_t stop_original=0;
struct Harness {
    Controls controls{ControlsConfig{}};
    MechContract normal{},mech{},other{};
    InputTarget target;
    MechActions actions;
    FrameInput frame;
    uint32_t consumer=0,event=0,reset=0,stop=0;
    uint32_t owned_weapon=0;
    unsigned prepares=0,readbacks=0,releases=0;
    bool pending=false,rotate=true,enabled=true,cinematic=false;
    Harness(const nova::ElfImage &image) {
        stop_harness=this;
        consumer=nova::abi::require_function(image,"mech_consume");
        event=nova::abi::require_function(image,"mech_event");
        reset=nova::abi::require_function(image,"mech_reset");
        stop=nova::abi::require_function(image,"mech_stop");
        mech.allowed[0]=mech.allowed[1]=1;
        target={TargetKind::mech,address(mech)}; controls.gameplay=true;
    }
    ~Harness() { if(stop_harness==this) stop_harness=nullptr; }
    void motion(float x,float y) {
        SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=x; e.motion.yrel=y; controls.handle(e);
    }
    void button(Uint32 type,Uint8 code) {
        SDL_Event e{}; e.type=type; e.button.button=code; controls.handle(e);
    }
    void drain(MechControls gates={true,true,true}) {
        frame=controls.active() ? controls.take() : FrameInput{};
        owned_weapon=target.component;
        apply_mech_actions(frame,gates,actions,[&](uint32_t type,float x,float y) {
            invoke(event,{target.component,type,bits(x),bits(y)});
        });
        pending=frame.dx!=0 || frame.dy!=0;
    }
    void boundary(MechContract &object,unsigned dt,TargetKind kind=TargetKind::mech) {
        mouse_look_boundary(pending,[&] {
            return controls.active() && look_target_matches(target,kind,address(object),enabled) &&
                !cinematic && rotate && !object.seen.locked;
        },[&] {
            auto delta=look_delta(frame.dx,frame.dy,object.actor.pitch,20,controls.config);
            auto &look=*reinterpret_cast<GuestLookInput *>(&object.look);
            queue_mouse_look(look,object.actor.yaw,object.actor.pitch,delta); ++prepares;
        },[&] { invoke(consumer,{address(object),dt}); },[&] { ++readbacks; });
    }
    void cleanup(bool native_enabled=true) {
        ++releases;
        release_mech_actions(actions,native_enabled,[&] { invoke(reset,{address(mech)}); },
            [&](uint32_t channel) { invoke(stop,{address(mech),channel,1}); });
        owned_weapon=0;
    }
    void change(InputTarget next,uint32_t old_level=1,uint32_t new_level=1,uint32_t fresh_mech=0) {
        if (!fresh_mech) fresh_mech=address(mech);
        change_input_target(target,next,[&](InputTarget old) {
            check(target==old,"old identity retained during cleanup");
            if (live_input_target(old,old_level,new_level,address(normal),fresh_mech)) cleanup(enabled);
        },[&] { controls.clear(); frame={}; pending=false; actions={}; owned_weapon=0; });
        controls.gameplay=bool(next);
    }
};
void __cdecl stop_observer(uint32_t weapon,uint32_t channel,uint32_t messages) {
    if(!stop_harness) { invoke(stop_original,{weapon,channel,messages}); return; }
    auto &h=*stop_harness;
    observe_mech_stop(h.actions,h.owned_weapon,weapon,channel,[&] { invoke(stop_original,{weapon,channel,messages}); });
}
Harness *hook_harness=nullptr;
uint32_t trampoline=0;
void __cdecl replacement(uint32_t object,uint32_t dt) {
    auto &h=*hook_harness;
    auto saved=h.consumer; h.consumer=trampoline;
    h.boundary(*reinterpret_cast<MechContract *>(object),dt);
    h.consumer=saved;
}
void selection(bool baseline) {
    TargetCandidate normal{11,true,true,true},mech{22,false,true,true};
    check(select_input_target(true,normal,mech)==InputTarget{TargetKind::normal,11},"on-foot selection unchanged");
    normal.enabled=false; mech.enabled=true;
    auto selected=baseline ? InputTarget{} : select_input_target(true,normal,mech);
    check(selected==InputTarget{TargetKind::mech,22},"enabled mech must capture when normal player is disabled (R2 regression)");
    for (unsigned mask=0;mask<32;++mask) {
        normal.enabled=(mask&1)!=0; mech.enabled=(mask&2)!=0;
        mech.alive=(mask&4)!=0; mech.interactive=(mask&8)!=0;
        bool context=(mask&16)!=0;
        selected=select_input_target(context,normal,mech);
        bool expected=context && normal.enabled!=mech.enabled &&
            (normal.enabled || (mech.alive && mech.interactive));
        check(bool(selected)==expected,"exclusive selection / disabled / death / cinematic / pause gates");
        if (selected) check(selected.component==(normal.enabled ? 11u : 22u),"exactly one selected component");
    }
    check(!select_input_target(true,{11,false,true,true},{22,false,true,true}),"unsupported special mode fallback");
    check(!live_input_target({TargetKind::mech,22},1,2,11,22) &&
          !live_input_target({TargetKind::mech,22},1,1,11,23),"lifetime rejects changed level/component");
    std::puts("selection: exclusive normal/mech, 32 eligibility combinations, unsupported and stale identity passed");
}
void actions_and_cleanup(const nova::ElfImage &image,bool prior_cleanup) {
    for (unsigned channels=0;channels<4;++channels) {
        Harness h(image);
        h.controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); h.controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_D));
        if(channels&1) h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
        if(channels&2) h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
        h.motion(30,-20); h.drain(); h.boundary(h.mech,16);
        close_to(h.mech.movement[0],.70710678f,"normalized mech right input");
        close_to(h.mech.movement[1],-.70710678f,"native forward sign");
        check(h.mech.shots[0]==unsigned((channels&1)!=0) && h.mech.shots[1]==unsigned((channels&2)!=0),"native channel actions");
        h.drain(); check(h.mech.events[29]==unsigned((channels&1)!=0) && h.mech.events[31]==unsigned((channels&2)!=0),"held fire has one down edge");
        // Halt the consumer, then lose eligibility. Immediate cleanup must not
        // leave a release that emits another native stop/message upon resume.
        if(prior_cleanup && channels==3) {
            invoke(h.event,{address(h.mech),30,0,0}); invoke(h.event,{address(h.mech),32,0,0});
            invoke(h.stop,{address(h.mech),0,1}); invoke(h.stop,{address(h.mech),1,1});
            h.actions={};
        } else h.cleanup();
        h.cleanup();
        invoke(h.consumer,{address(h.mech),16});
        check(h.mech.messages[0]==unsigned((channels&1)!=0) && h.mech.messages[1]==unsigned((channels&2)!=0),"cleanup/resume must emit exactly one native stop per owned channel");
        check(!h.mech.firing[0] && !h.mech.firing[1] && !h.mech.released[0] && !h.mech.released[1],"no residual fire/release edges");
        if(!prior_cleanup || channels!=3) check(h.mech.resets==1,"reiterated refresh does not reset twice");
        check(!h.mech.look.yaw_pending && h.mech.look.pitch_delta==0 && h.mech.movement[0]==0,"native reset clears input without actor rewrite");
    }
    for (auto reason : {"focus","overlay","pause","cinematic","death","component","level"}) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
        h.controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); h.motion(100,80); h.drain();
        if(std::string(reason)=="focus") { h.controls.context(false,false); h.cleanup(); h.controls.context(true,false); }
        else if(std::string(reason)=="overlay") { h.controls.overlay(true); check(!h.controls.active(),"overlay gate releases mech input"); h.cleanup(); h.controls.overlay(false); }
        else if(std::string(reason)=="level") {
            h.change({},1,2); check(h.releases==0,"level teardown must not dereference old target"); continue;
        } else h.change({TargetKind::normal,address(h.normal)});
        h.controls.clear(); h.drain(); invoke(h.consumer,{address(h.mech),16});
        check(h.frame.held.none() && h.frame.pressed.empty() && !h.frame.dx && !h.frame.dy,"transition discards held/edge/counts");
        check(h.mech.messages[0]==1 && h.mech.messages[1]==1 && !h.mech.shots[0] && !h.mech.shots[1],"stopped consumer cannot fire on resume");
    }
    Harness disabled(image); disabled.actions={true,true,true}; disabled.enabled=false;
    disabled.change({}); check(disabled.mech.resets==0 && disabled.mech.messages[0]==0 && disabled.mech.messages[1]==0,"native disable cleanup is not repeated");
    Harness stale(image); stale.actions={true,true,true}; stale.change({},1,1,address(stale.other));
    check(stale.releases==0 && stale.mech.resets==0,"fresh getter mismatch skips stale component");
    Harness gated(image); gated.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); gated.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
    gated.controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); gated.drain({false,false,false}); gated.boundary(gated.mech,16);
    check(!gated.mech.events[13] && !gated.mech.events[29] && !gated.mech.events[31],"disabled action controls block dispatch");
    gated.drain(); gated.mech.allowed[1]=0; gated.boundary(gated.mech,16);
    check(gated.mech.shots[0]==1 && !gated.mech.shots[1],"native weapon eligibility remains authoritative");
    Harness click(image); click.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); click.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT);
    click.drain(); click.boundary(click.mech,16); click.drain(); click.boundary(click.mech,16);
    check(click.mech.events[31]==1 && click.mech.events[32]==1 && click.mech.shots[1]==1,"subframe secondary click has one native press/release");
    for(bool consumer_ran : {false,true}) {
        Harness release(image); release.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
        release.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); release.drain(); release.boundary(release.mech,16);
        release.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); release.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT); release.drain();
        if(consumer_ran) {
            release.boundary(release.mech,16);
        }
        release.cleanup(); invoke(release.consumer,{address(release.mech),16});
        check(release.mech.messages[0]==1 && release.mech.messages[1]==1 && !release.mech.firing[0] && !release.mech.firing[1],
              "release pending at eligibility loss stops now; consumed release never stops twice");
    }
    Harness reset_only(image);
    reset_only.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); reset_only.drain(); reset_only.boundary(reset_only.mech,16);
    reset_only.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); reset_only.drain();
    invoke(reset_only.reset,{address(reset_only.mech)});
    check(reset_only.actions.primary_release,"native reset without FireStop is not a consumption acknowledgement");
    reset_only.cleanup(); check(reset_only.mech.messages[0]==1,"native reset cannot lose the required stop");
    Harness acknowledgement(image); acknowledgement.owned_weapon=address(acknowledgement.mech);
    acknowledgement.actions.primary_release=acknowledgement.actions.secondary_release=true;
    const auto primary=nova::abi::require_function(image,"mech_primary_stop");
    const auto secondary=nova::abi::require_function(image,"mech_secondary_stop");
    invoke(primary,{address(acknowledgement.other),0,0});
    check(acknowledgement.actions.primary_release && !acknowledgement.other.messages[0],"other weapon forwards without acknowledgement");
    invoke(secondary,{address(acknowledgement.mech),9,0x12345678});
    check(acknowledgement.actions.primary_release && acknowledgement.actions.secondary_release &&
        acknowledgement.mech.last_channel==9 && acknowledgement.mech.last_message_argument==0x12345678,"unrelated channel and every argument forwarded unchanged");
    observe_mech_stop(acknowledgement.actions,address(acknowledgement.mech),address(acknowledgement.mech),0,[&] {
        check(acknowledgement.actions.primary_release,"acknowledgement must occur after original returns");
    });
    check(!acknowledgement.actions.primary_release && acknowledgement.actions.secondary_release,"ack affects only its channel");
    acknowledgement.actions.primary=true; acknowledgement.actions.primary_release=false;
    invoke(primary,{address(acknowledgement.mech),0,1});
    check(acknowledgement.actions.primary,"release then repress retains requested hold after old stop");
    acknowledgement.owned_weapon=0; invoke(secondary,{address(acknowledgement.mech),1,1});
    check(acknowledgement.actions.secondary_release,"no cached owner cannot acknowledge unrelated stop");
    bool edge=true; check(!mech_exit_edge(edge,false) && mech_exit_edge(edge,true) && !mech_exit_edge(edge,true),"exit edge obeys native enablement and consumes once");
    std::puts("actions/cleanup: channels, native permission, brief click, halted-consumer resume, lifetime, exit edge passed");
}
void look_and_hook(const nova::ElfImage &image) {
    for(auto kind : {TargetKind::normal,TargetKind::mech}) {
        Harness transition(image); transition.target={kind,address(transition.mech)};
        transition.motion(40,30); transition.drain(); transition.enabled=false;
        transition.boundary(transition.mech,16,kind);
        check(transition.prepares==0 && transition.mech.actor.yaw==0 && transition.mech.actor.pitch==0,
              "component disabled after selection cannot receive pending X/Y at its consumer");
        transition.change({}); transition.enabled=true; transition.pending=false;
        transition.boundary(transition.mech,16,kind);
        check(transition.prepares==0,"ownership transfer cannot replay disabled-component counts");
    }
    Harness h(image); hook_harness=&h;
    std::array<uint8_t,7> before{}; std::memcpy(before.data(),reinterpret_cast<void *>(h.consumer),before.size());
    {
        nova::abi::GuestHook hook(image,"mech_consume",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&replacement));
        trampoline=hook.original();
        for (auto counts : {std::pair{40.f,0.f},std::pair{0.f,-80.f},std::pair{20.f,50.f},std::pair{0.f,-1000.f},std::pair{0.f,1000.f}}) {
            auto old=h.mech.actor; unsigned n=h.prepares;
            h.motion(counts.first/2,counts.second/2); h.motion(counts.first/2,counts.second/2); h.drain();
            invoke(h.consumer,{address(h.other),16}); check(h.prepares==n && h.pending,"wrong target cannot consume counts");
            auto delta=look_delta(counts.first,counts.second,old.pitch,20,h.controls.config);
            invoke(h.consumer,{address(h.mech),16}); invoke(h.consumer,{address(h.mech),16});
            check(h.prepares==n+1 && h.readbacks==h.prepares && !h.pending,"shared X/Y applied exactly once through realigning detour");
            close_to(h.mech.actor.yaw,old.yaw+delta.yaw,"mech yaw retains count sensitivity/sign");
            close_to(h.mech.actor.pitch,old.pitch+delta.pitch,"mech native pitch range");
            h.drain(); old=h.mech.actor; invoke(h.consumer,{address(h.mech),33});
            close_to(h.mech.actor.yaw,old.yaw,"next no-motion frame cannot replay yaw"); close_to(h.mech.actor.pitch,old.pitch,"next no-motion frame cannot replay pitch");
        }
        h.controls.config.invert_y=true; h.mech.actor.pitch=0; h.motion(0,40); h.drain(); invoke(h.consumer,{address(h.mech),7}); close_to(h.mech.actor.pitch,4,"configured invert Y in mech");
        h.mech.actor.pitch=0;
        for(unsigned i=0;i<4;++i) { h.motion(0,10); h.drain(); invoke(h.consumer,{address(h.mech),1+i}); }
        close_to(h.mech.actor.pitch,4,"partitioned counts independent of dt");
        for(unsigned gate=0;gate<4;++gate) {
            h.enabled=gate!=0; h.cinematic=gate==1; h.rotate=gate!=2; h.mech.seen.locked=gate==3;
            h.motion(10,10); h.drain(); auto n=h.prepares; auto input=h.mech.look;
            invoke(h.consumer,{address(h.mech),16}); check(h.prepares==n,"native mech eligibility blocks look");
            check(!std::memcmp(&input,&h.mech.look,sizeof(input)),"ineligible mouse does not replace native pending fields");
            h.pending=false;
        }
    }
    check(!std::memcmp(before.data(),reinterpret_cast<void *>(h.consumer),before.size()),"realigning entry bytes restored");
    for(bool unsupported : {false,true}) {
        bool rejected=false;
        try { nova::abi::GuestHook bad(image,unsupported ? "unsupported_lea" : "mech_consume",
            {0x8d,0x4c,0x24,0x08,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&replacement)); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected,"mismatched/unsupported LEA rejected before detour execution");
    }
    h.mech.seen.locked=0; invoke(h.consumer,{address(h.mech),16});
    std::puts("look/hook: X/Y/mixed/clamps/invert/partition/guards, wrong/double consumer, MOVAPS, restoration/rejection passed");
}
void visibility(const nova::ElfImage &image) {
    auto setter=nova::abi::require_function(image,"mech_set_draw");
    for(bool scope : {false,true}) for(unsigned initial=0;initial<2;++initial) {
        MechDrawControl a{1,initial,1,0},b{0,1,0,0},information{1,1,0,0};
        std::map<uint32_t,unsigned> saved{{address(a),a.visible},{address(b),b.visible}};
        auto draw=[&] {
            check(a.visible==(scope ? 0u : initial) && b.visible==(scope ? 0u : 1u),"scoped touch graphics only");
            check(a.enabled==1 && b.enabled==0 && a.pressed==1 && information.visible==1,"native action gates and informative HUD untouched");
        };
        if(scope) with_hidden_controls(saved,[&](uint32_t control,bool visible) { invoke(setter,{control,visible}); },draw);
        else draw();
        check(a.visible==initial && b.visible==1,"each original drawing flag restored");
    }
    std::puts("visibility: active scope, original flags, native enabled/pressed and informative HUD passed");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    auto entry=nova::abi::require_function(image,"_ZN20CPlayerMechComponent15UpdateFromInputEi");
    std::array<uint8_t,7> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(entry),saved.size());
    const auto update=nova::abi::require_function(image,"_ZN20CPlayerMechComponent6UpdateEi");
    std::array<std::array<uint8_t,5>,2> stops{};
    unsigned i=0;
    for(uint32_t offset : {0xe71u,0xee1u}) std::memcpy(stops[i++].data(),reinterpret_cast<void *>(update+offset),5);
    { NativeControls native(image,controls); }
    check(!std::memcmp(saved.data(),reinterpret_cast<void *>(entry),saved.size()),"real mech entry restored");
    i=0;
    for(uint32_t offset : {0xe71u,0xee1u}) check(!std::memcmp(stops[i++].data(),reinterpret_cast<void *>(update+offset),5),"real release-consumer call sites restored");
    std::puts("real NativeControls constructor: all symbols/hooks installed/restored; no guest initializers or gameplay executed");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") {
            if(!std::filesystem::exists(argv[2])) return 77;
            real(argv[2]); return 0;
        }
        check(argc>=2,"fixture path required");
        const std::string mode=argc>2 ? argv[2] : "";
        selection(mode=="--r2-baseline");
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        stop_original=nova::abi::require_function(image,"mech_stop");
        std::vector<std::unique_ptr<nova::abi::GuestCall>> observers;
        std::vector<std::pair<uint32_t,std::array<uint8_t,5>>> saved_calls;
        for(auto name : {"primary","secondary"}) {
            std::string owner=std::string("mech_")+name+"_stop";
            auto entry=nova::abi::require_function(image,owner);
            auto site=nova::abi::require_function(image,owner+"_call");
            std::array<uint8_t,5> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(site),saved.size());
            saved_calls.emplace_back(site,saved);
            observers.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,site-entry,"mech_stop",reinterpret_cast<uint32_t>(&stop_observer)));
        }
        actions_and_cleanup(image,mode=="--prior-cleanup"); look_and_hook(image); visibility(image);
        observers.clear();
        for(auto &[site,saved] : saved_calls) check(!std::memcmp(saved.data(),reinterpret_cast<void *>(site),saved.size()),"both stop observer call sites restored");
        std::puts("mech adapter contracts passed"); return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"mech adapter test failed: %s\n",e.what()); return 1; }
}
