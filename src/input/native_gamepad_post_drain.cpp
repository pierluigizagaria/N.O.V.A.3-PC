#include "input/native_gamepad_post_drain.h"
#include "input/native_intents.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <windows.h>

namespace nova::input {
namespace {
using abi::invoke;
using abi::invoke_bool;
template<class T=uint32_t> T read(uint32_t p,uint32_t offset=0) {
    return *reinterpret_cast<const T *>(p+offset);
}
struct Stick { float x=0,y=0; };
struct Event { uint32_t table,type; float x,y; };
static_assert(sizeof(Event)==16);
Stick bounded(float x,float y) {
    if(!std::isfinite(x) || !std::isfinite(y)) return {};
    return {std::clamp(x,-1.f,1.f),std::clamp(y,-1.f,1.f)};
}
Stick capped(Stick value) {
    const auto length=std::sqrt(value.x*value.x+value.y*value.y);
    if(length>1.f) { value.x/=length; value.y/=length; }
    return value;
}
constexpr auto get_level="_ZN6CLevel8GetLevelEv";
constexpr auto get_player="_ZN6CLevel18GetPlayerComponentEv";
constexpr auto get_app="_ZN11Application11GetInstanceEv";
constexpr auto get_state="_ZN12gxStateStack12CurrentStateEv";
constexpr auto is_dead="_ZNK11CGameObject6IsDeadEv";
constexpr auto cinematic="_ZN15PlayerComponent19IsWatchingCinematicEv";
constexpr auto move_getter="_ZN14ControlScheme414GetMoveControlEv";
constexpr auto look_getter="_ZN14ControlScheme416GetRotateControlEv";
constexpr auto immediate="_ZN12EventManager10raiseAsyncERK6IEvent";
}
NativeGamepadPostDrain *NativeGamepadPostDrain::instance_=nullptr;
uint32_t NativeGamepadPostDrain::fn(const char *name) const { return functions_.at(name); }
NativeGamepadPostDrain::NativeGamepadPostDrain(const ElfImage &image,Controls &input)
    : input_(input) {
    if(instance_) throw std::runtime_error("Only one post-drain gamepad adapter is supported");
    for(auto name : {get_level,get_player,get_app,get_state,is_dead,cinematic,immediate,move_getter,look_getter,
        "_ZN6CLevel22GetPlayerMechComponentEv","_ZN6CLevel28GetPlayerMachineGunComponentEv",
        "_ZN6CLevel22GetPlayerJeepComponentEv","_ZN6CLevel22GetPlayerBikeComponentEv"})
        functions_[name]=abi::require_function(image,name);
    auto symbol=[&](const char *name) {
        const auto p=image.symbol(name);
        if(!p || !image.contains(p)) throw std::runtime_error(std::string("Missing gamepad symbol: ")+name);
        return p;
    };
    event_manager_=symbol("_ZN18GlobalEventManager9SingletonE");
    event_move_=symbol("_ZTV11EvStickMove")+8;
    event_release_=symbol("_ZTV15EvStickReleased")+8;
    player_table_=symbol("_ZTV15PlayerComponent")+8;
    ingame_table_=symbol("_ZTV9GS_InGame")+8;
    ctor1_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponentC1EP11CGameObject",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&ctor1));
    if(image.symbol("_ZN15PlayerComponentC2EP11CGameObject")!=image.symbol("_ZN15PlayerComponentC1EP11CGameObject"))
        ctor2_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponentC2EP11CGameObject",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&ctor2));
    dtor0_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponentD0Ev",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&dtor0));
    dtor1_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponentD1Ev",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&dtor1));
    if(image.symbol("_ZN15PlayerComponentD2Ev")!=image.symbol("_ZN15PlayerComponentD1Ev"))
        dtor2_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponentD2Ev",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&dtor2));
    reset_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponent14ResetComponentEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&reset));
    enable_=std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponent6EnableEb",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&enable));
    // 0x6c5180 + 0x1d2 = CALL 0x6c5352; ordinary return is 0x6c5357.
    // GuestCall checks E8 and the exact UpdateEvents destination. Other drains
    // (replay/special paths) are untouched. No caller stack/register rewrite.
    drain_=std::make_unique<abi::GuestCall>(image,"_ZN11Application7_UpdateEii",0x1d2,
        "_ZN12EventManager12UpdateEventsEi",reinterpret_cast<uint32_t>(&drain));
    input_.bind_gamepad_receiver(false,true);
    instance_=this;
}
NativeGamepadPostDrain::~NativeGamepadPostDrain() {
    drain_.reset(); enable_.reset(); reset_.reset(); dtor2_.reset(); dtor1_.reset(); dtor0_.reset(); ctor2_.reset(); ctor1_.reset();
    if(instance_==this) instance_=nullptr;
}
void NativeGamepadPostDrain::begin_frame() { lane_.store(GetCurrentThreadId()); armed_=true; }
void NativeGamepadPostDrain::end_frame() { armed_=false; }
uint32_t __cdecl NativeGamepadPostDrain::drain(uint32_t manager,uint32_t dt) {
    auto &s=*instance_;
    if(GetCurrentThreadId()!=s.lane_.load()) return invoke(s.drain_->original(),{manager,dt});
    NativeInputInvocation invocation(s.depth_);
    const auto result=invoke(s.drain_->original(),{manager,dt});
    if(invocation.outer() && s.armed_) s.deliver();
    return result; // exact original result and native dt, including zero/high bit
}
void NativeGamepadPostDrain::retire(uint32_t player) {
    ++serial_; // Cancels acquisition even if retirement precedes watch publication.
    if(!player || watched_.load()!=player) return;
    ++epoch_; ++writer_;
    input_.bind_gamepad_receiver(false,true);
}
void NativeGamepadPostDrain::target_observed(uint32_t level,uint32_t player,uint32_t normal,uint32_t scheme,bool other_role_enabled) {
    if(!owner_.player) return;
    if(!other_role_enabled && level==owner_.level && player==owner_.player &&
       normal==owner_.normal && scheme==owner_.scheme) return;
    ++serial_; ++epoch_; ++writer_;
    owner_={}; watched_.store(0); seen_epoch_=epoch_.load();
    look_owned_=move_owned_=false;
    input_.bind_gamepad_receiver(false,true);
    // Native handover owns receiver cleanup. A future normal admission must
    // observe its identity and then receive a NEW neutral report.
}
void NativeGamepadPostDrain::native_reset(uint32_t control) {
    const auto player=watched_.load();
    if(player && control==player+0x10) {
        ++writer_;
        if(GetCurrentThreadId()==lane_.load()) look_owned_=move_owned_=false;
    }
}
void NativeGamepadPostDrain::native_event(uint32_t control,uint32_t event) {
    const auto player=watched_.load();
    if(!player || !event || control!=player+0x10) return;
    if(GetCurrentThreadId()==lane_.load() && event==host_packet_) return;
    const auto type=read(event,4);
    if((type>=9 && type<=14) || type==58) {
        ++writer_;
        if(GetCurrentThreadId()==lane_.load()) {
            if(type<=12 || type==58) look_owned_=false;
            if(type==13 || type==14 || type==58) move_owned_=false;
        }
    }
}
NativeGamepadPostDrain::Admission NativeGamepadPostDrain::acquire() {
    Admission out;
    const auto serial=serial_.load();
    auto current=[&] { return serial==serial_.load(); };
    const auto level=invoke(fn(get_level));
    if(!current()) return out;
    if(!level) { out.valid=true; return out; }
    const auto player=invoke(fn(get_player),{level});
    if(!current()) return out;
    if(!player) { out.valid=true; return out; }
    watched_.store(player);
    uint32_t competing=0;
    for(auto name : {"_ZN6CLevel22GetPlayerMechComponentEv","_ZN6CLevel28GetPlayerMachineGunComponentEv",
        "_ZN6CLevel22GetPlayerJeepComponentEv","_ZN6CLevel22GetPlayerBikeComponentEv"}) {
        const auto role=invoke(fn(name),{level});
        if(!current()) return out;
        if(role && read<uint8_t>(role,0xd)) ++competing;
    }
    const auto normal=read(level,0x17c),scheme=normal ? read(normal,0xc) : 0;
    if(!normal || !scheme || read(player)!=player_table_) { out.valid=true; return out; }
    // Bound this new permission-query cohort to the two proved leaf getters.
    // Unknown schemes forward their existing keyboard/native path unchanged.
    if(read(read(scheme),0x28)!=fn(move_getter) || read(read(scheme),0x2c)!=fn(look_getter)) { out.valid=true; return out; }
    const auto actor=read(player,4),input_template=read(player,0xfc);
    if(!actor || !input_template || !read(player,0x170)) { out.valid=true; return out; }
    const bool bound=read(player,0x100)==actor+0x68 && read(player,0x104)==actor+0x60;
    const bool dead=invoke_bool(fn(is_dead),{actor});
    if(!current()) return out;
    const bool watching=invoke_bool(fn(cinematic),{player});
    if(!current()) return out;
    const auto app=invoke(fn(get_app));
    if(!current()) return out;
    const auto state=app ? invoke(fn(get_state),{app+0x18738}) : 0;
    if(!current()) return out;
    const auto move=invoke(read(read(scheme),0x28),{scheme});
    if(!current()) return out;
    const auto look=invoke(read(read(scheme),0x2c),{scheme});
    if(!current()) return out;
    // LAST authoritative owner query, then no guest calls before scalar admission.
    if(invoke(fn(get_level))!=level || !current()) return out;
    if(invoke(fn(get_player),{level})!=player || !current()) return out;
    if(read(player,4)!=actor || read(player,0xfc)!=input_template ||
       read(level,0x17c)!=normal || read(normal,0xc)!=scheme) return out;
    out.owner={level,player,actor,normal,scheme,input_template,
        bound && !dead && !competing && !read<uint8_t>(input_template,0x30) &&
        read<uint8_t>(input_template,0x5d) && !read<uint8_t>(input_template,0x5c)};
    out.valid=true;
    out.live=state && read(state)==ingame_table_ && read(level,0x178)==normal &&
        read<uint8_t>(player,0xd) && !watching && !dead;
    out.move=out.live && move && read<uint8_t>(move,0x1d);
    out.look=out.live && look && read<uint8_t>(look,0x1d) && !read<uint8_t>(player,0x139);
    return out;
}
void NativeGamepadPostDrain::observe(const Owner &next) {
    if(next==owner_ && seen_epoch_==epoch_.load()) return;
    owner_=next; seen_epoch_=epoch_.load(); watched_.store(next.player);
    look_owned_=move_owned_=false;
    input_.bind_gamepad_receiver(next.supported,true);
}
void NativeGamepadPostDrain::emit(uint32_t manager,uint32_t type,float x,float y) {
    const Event packet{(type==9 || type==13) ? event_move_ : event_release_,type,x,y};
    const auto previous=host_packet_;
    host_packet_=reinterpret_cast<uint32_t>(&packet);
    try { invoke(fn(immediate),{manager,host_packet_}); }
    catch(...) { host_packet_=previous; throw; }
    host_packet_=previous;
    // No receiver read here. Packet lifetime ends only after immediate dispatch.
}
void NativeGamepadPostDrain::deliver() {
    const auto initial=input_.continuous_gamepad();
    if(!initial.source_present && !look_owned_ && !move_owned_) return;
    // A second event ALWAYS reacquires: no first-event owner/permission snapshot.
    for(unsigned channel=0;channel<2;++channel) {
        const auto before_query_writer=writer_.load();
        const auto acquired=acquire();
        if(!acquired.valid || before_query_writer!=writer_.load()) return;
        observe(acquired.owner);
        if(!owner_.supported) return;
        const auto source=input_.continuous_gamepad();
        const bool admitted=source.admitted && input_.gamepad_current(source.revision);
        const auto serial=serial_.load(),writer=writer_.load();
        const auto manager=read(event_manager_);
        if(!manager) return;
        if(channel==0) {
            const auto stick=admitted ? bounded(source.sample.axes.look_x,source.sample.axes.look_y) : Stick{};
            if(acquired.look && (stick.x || stick.y)) {
                look_owned_=true; emit(manager,9,stick.x,stick.y);
            } else if(look_owned_) {
                look_owned_=false;
                if(acquired.live) emit(manager,10,0,0);
            }
        } else {
            const auto pad=admitted ? bounded(source.sample.axes.move_x,source.sample.axes.move_y) : Stick{};
            const bool pad_move=pad.x || pad.y;
            if(pad_move || move_owned_) {
                const bool previously_owned=move_owned_;
                auto down=[&](Action a) { return source.keyboard_held.test(static_cast<size_t>(a)); };
                const auto keyboard=capped({float(down(Action::right))-float(down(Action::left)),
                    float(down(Action::back))-float(down(Action::forward))});
                const auto combined=capped({keyboard.x+pad.x,keyboard.y+pad.y});
                move_owned_=pad_move && acquired.move;
                if(acquired.move && input_.active()) emit(manager,(combined.x || combined.y) ? 13u : 14u,combined.x,combined.y);
                else if(previously_owned && acquired.live) emit(manager,14,0,0);
            }
        }
        // Reentrant reset/Enable/retirement owns its later native writes. Never
        // restore this step's sample over it, even if permission returns true.
        if(serial!=serial_.load() || writer!=writer_.load()) return;
    }
}
uint32_t __cdecl NativeGamepadPostDrain::ctor1(uint32_t p,uint32_t a) {
    auto &s=*instance_; s.retire(p); return invoke(s.ctor1_->original(),{p,a});
}
uint32_t __cdecl NativeGamepadPostDrain::ctor2(uint32_t p,uint32_t a) {
    auto &s=*instance_; s.retire(p); return invoke(s.ctor2_->original(),{p,a});
}
uint32_t __cdecl NativeGamepadPostDrain::dtor0(uint32_t p) {
    auto &s=*instance_; s.retire(p); return invoke(s.dtor0_->original(),{p});
}
uint32_t __cdecl NativeGamepadPostDrain::dtor1(uint32_t p) {
    auto &s=*instance_; s.retire(p); return invoke(s.dtor1_->original(),{p});
}
uint32_t __cdecl NativeGamepadPostDrain::dtor2(uint32_t p) {
    auto &s=*instance_; s.retire(p); return invoke(s.dtor2_->original(),{p});
}
uint32_t __cdecl NativeGamepadPostDrain::reset(uint32_t p) {
    auto &s=*instance_; s.retire(p); return invoke(s.reset_->original(),{p});
}
uint32_t __cdecl NativeGamepadPostDrain::enable(uint32_t p,uint32_t value) {
    auto &s=*instance_; s.native_reset(p+0x10); return invoke(s.enable_->original(),{p,value});
}
}
