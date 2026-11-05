#include "camera/viewmodel_eligibility_native.h"
#include <stdexcept>
#include <mutex>
#include <cmath>
#include <windows.h>

namespace nova::camera::viewmodel_eligibility {
namespace {
template<class T=uint32_t> T read(uint32_t p,uint32_t offset=0) { return *reinterpret_cast<const T *>(p+offset); }
constexpr auto actor_dead="_ZNK11CGameObject6IsDeadEv";
constexpr auto health_dead="_ZNK16CHealthComponent6IsDeadEv";
}
NativeProducer *NativeProducer::instance_=nullptr;
thread_local NativeProducer::Receipt *NativeProducer::receipt_=nullptr;
NativeProducer::NativeProducer(const ElfImage &image,bool fixture) {
    if(instance_) throw std::runtime_error("Only one viewmodel eligibility producer is supported");
    auto data=[&](const char *name) {
        const auto p=image.symbol(name);
        if(!p || !image.contains(p) || !image.contains(p+3))
            throw std::runtime_error(std::string("Missing eligibility data: ")+name);
        return p;
    };
    mp_slot_=data("_ZN13GameMpManager9SingletonE");
    app_slot_=data("_ZZN11Application11GetInstanceEvE3app");
    level_slot_=data("_ZN6CLevel10m_instanceE");
    ingame_vtable_=data("_ZTV9GS_InGame")+8;
    current_state_=abi::require_function(image,"_ZN12gxStateStack12CurrentStateEv");
    is_mp_=abi::require_function(image,"_ZN9MpManager17IsMultiplayerGameEv");
    is_dead_=abi::require_function(image,actor_dead);
    is_cinematic_=abi::require_function(image,"_ZN15PlayerComponent19IsWatchingCinematicEv");
    uint32_t offset=0x24;
    if(fixture) {
        const auto call=image.symbol("fov_eligibility_health_call");
        if(call<is_dead_) throw std::runtime_error("Missing fixture health CALL");
        offset=call-is_dead_;
    }
    health_call_=std::make_unique<abi::GuestCall>(image,actor_dead,offset,health_dead,reinterpret_cast<uint32_t>(&health));
    original_health_=health_call_->original();
    auto hook=[&](const char *name,std::initializer_list<uint8_t> bytes,auto replacement) {
        auto h=std::make_unique<abi::GuestHook>(image,name,bytes,reinterpret_cast<uint32_t>(replacement));
        const auto original=h->original(); lifecycle_.push_back(std::move(h)); return original;
    };
    original_ctor1_=hook("_ZN13GameMpManagerC1Ev",{0x55,0x89,0xe5,0x56,0x53},&ctor1);
    original_ctor2_=image.symbol("_ZN13GameMpManagerC2Ev")==image.symbol("_ZN13GameMpManagerC1Ev") ? original_ctor1_ :
        hook("_ZN13GameMpManagerC2Ev",{0x55,0x89,0xe5,0x56,0x53},&ctor2);
    original_dtor1_=hook("_ZN13GameMpManagerD1Ev",{0x55,0x89,0xe5,0x57,0x56,0x53},&dtor1);
    original_dtor2_=image.symbol("_ZN13GameMpManagerD2Ev")==image.symbol("_ZN13GameMpManagerD1Ev") ? original_dtor1_ :
        hook("_ZN13GameMpManagerD2Ev",{0x55,0x89,0xe5,0x57,0x56,0x53},&dtor2);
    // Installed before guest initializers (play.cpp). An already existing
    // unobserved manager deliberately remains unproved; never adopt an address.
    instance_=this;
}
NativeProducer::~NativeProducer() { lifecycle_.clear(); health_call_.reset(); instance_=nullptr; }
void NativeProducer::constructed(uint32_t p,uint32_t original) {
    {
        std::unique_lock lock(manager_lifetime_);
        ready_manager_.store(0,std::memory_order_release); ++manager_epoch_;
    }
    abi::invoke(original,{p});
    // Only the original completed constructor can publish a live manager.
    std::unique_lock lock(manager_lifetime_);
    if(read(mp_slot_)==p) ready_manager_.store(p,std::memory_order_release);
    ++manager_epoch_;
}
void NativeProducer::destroyed(uint32_t p,uint32_t original) {
    {
        // Wait only for a proved pure reader to finish BEFORE native teardown
        // can reclaim the object. No native destructor/callback runs locked.
        std::unique_lock lock(manager_lifetime_);
        ready_manager_.store(0,std::memory_order_release); ++manager_epoch_;
    }
    abi::invoke(original,{p});
}
void __cdecl NativeProducer::ctor1(uint32_t p) { instance_->constructed(p,instance_->original_ctor1_); }
void __cdecl NativeProducer::ctor2(uint32_t p) { instance_->constructed(p,instance_->original_ctor2_); }
void __cdecl NativeProducer::dtor1(uint32_t p) { instance_->destroyed(p,instance_->original_dtor1_); }
void __cdecl NativeProducer::dtor2(uint32_t p) { instance_->destroyed(p,instance_->original_dtor2_); }
uint32_t __cdecl NativeProducer::health(uint32_t p) {
    auto &s=*instance_;
    auto *receipt=receipt_;
    if(receipt && receipt->owner==&s) {
        ++receipt->calls; receipt->health=p;
        const auto manager=s.ready_manager_.load(std::memory_order_acquire);
        // A host-issued probe may decline an unsafe branch. Ordinary native
        // IsDead callers always forward once with their original policy intact.
        if(!p || !manager || read(s.mp_slot_)!=manager || read(manager,4)!=0) {
            receipt->safe=false; return 0;
        }
    }
    return abi::invoke(s.original_health_,{p}); // Preserve all EAX bits; AL is authoritative.
}
NativeSample NativeProducer::sample(const NativeOwners &o) {
    NativeSample result;
    if(!lane_ || GetCurrentThreadId()!=lane_) return result; // Never query foreign objects from foreign callbacks.
    rejection_="native-fps-manager-busy";
    std::shared_lock lifetime(manager_lifetime_,std::try_to_lock);
    if(!lifetime.owns_lock()) return result;
    rejection_="native-fps-owner-incomplete";
    if(consumption_==UINT64_MAX) return result;
    const auto serial=++consumption_;
    if(!serial || !o.level || !o.player || !o.actor || !o.game_camera || !o.node ||
       !o.weapon_manager || !o.weapon || !o.camera_generation) return result;
    const auto manager=ready_manager_.load(std::memory_order_acquire);
    const auto epoch=manager_epoch_.load(std::memory_order_acquire);
    const auto app=read(app_slot_);
    rejection_=!manager ? "native-fps-manager-unready" : "native-fps-global-owner";
    if(!manager || read(mp_slot_)!=manager || !app || read(level_slot_)!=o.level) return result;
    const auto multiplayer=read(manager,4);
    // GetPlayerComponent uses level+24c only in campaign. In multiplayer it
    // resolves GetMyMpPlayer: index+3a04, pointer vector+3bdc/3be0, row->actor.
    // Resolve that same local owner without component virtuals in a consumer.
    auto local_actor=[&]() -> uint32_t {
        if(read(manager,4)!=multiplayer) return 0;
        if(!multiplayer) return read(o.level,0x24c);
        const auto index=read<int32_t>(manager,0x3a04);
        const auto begin=read(manager,0x3bdc),end=read(manager,0x3be0);
        if(index<0 || index>12 || !begin || end<begin || (end-begin)%4 ||
           uint32_t(index)>=(end-begin)/4) return 0;
        const auto row=read(begin,uint32_t(index)*4);
        return row ? read(row) : 0;
    };
    // Native CurrentState is a proved leaf. Its owned array precedes index+0x34.
    const auto stack=app+0x18738;
    const auto index=read<int32_t>(stack,0x34);
    rejection_="native-fps-state-stack";
    if(index<0 || index>=13) return result;
    const auto state=abi::invoke(current_state_,{stack});
    if(!state) return result;
    auto match=[&](bool value,const char *reason) { if(!value) rejection_=reason; return value; };
    auto fields=[&] {
        // Check roots BEFORE following current object links; no helper queries.
        return match(ready_manager_.load(std::memory_order_acquire)==manager &&
            manager_epoch_.load(std::memory_order_acquire)==epoch && read(mp_slot_)==manager &&
            read(app_slot_)==app && read(level_slot_)==o.level,"native-fps-owner-changed") &&
            match(
            read<int32_t>(stack,0x34)==index && read(stack,uint32_t(index)*4)==state &&
            read(state)==ingame_vtable_ && read(state,0x28)==o.level,"native-fps-game-state") &&
            match(local_actor()==o.actor && read(o.player,4)==o.actor,"native-fps-player-owner") &&
            match(read(o.player,0x178)==o.game_camera && read(o.game_camera,0x26c)==o.node &&
            read(o.game_camera,0x290)==o.actor,"native-fps-camera-owner") &&
            match(read<uint8_t>(o.player,0xd)!=0,"native-fps-player-disabled") &&
            match(read(o.level,0x17c)!=0 && read(o.level,0x178)==read(o.level,0x17c),"native-fps-control-mode") &&
            match(read(o.player,0x130)<=1,"native-fps-player-state") &&
            match(read(o.player,0x170)==o.weapon_manager &&
            read(o.weapon_manager,0x148)==o.inventory && read(o.weapon_manager,0x14c)==o.end &&
            read(o.weapon_manager,0x160)==o.index && o.inventory && o.end>=o.inventory &&
            (o.end-o.inventory)%24==0 && o.index<(o.end-o.inventory)/24 &&
            read(o.inventory,o.index*24+12)==o.weapon,"native-fps-inventory");
    };
    if(!fields() || !match(abi::invoke_bool(is_mp_,{manager})==bool(multiplayer),"native-fps-multiplayer-changed") || !fields()) return result;
    if(!match(!abi::invoke_bool(is_cinematic_,{o.player}),"native-fps-cinematic") || !fields()) return result;
    if(multiplayer) {
        // Original Health::IsDead first resolves the actor's PlayerComponent
        // (type 0x2a), hence object type0. For that already resolved local player,
        // freeze mode2 returns health+0xe; other modes compare health+0x14 <= 0.
        // Do not call its GetComponent virtual chain inside a borrowed camera
        // continuation. This read-only branch has the same native semantics;
        // ordinary guest IsDead calls still forward unchanged through health().
        const auto health=read(o.actor,0x118),mode=read(manager,0x3c00);
        if(!match(health && read(health,4)==o.actor,"native-fps-health-owner")) return result;
        const float amount=read<float>(health,0x14);
        const auto frozen=read<uint8_t>(health,0xe);
        if(!match(mode==2 ? !frozen : std::isfinite(amount) && amount>0,"native-fps-health")) return result;
        if(!fields() || read(o.actor,0x118)!=health || read(health,4)!=o.actor ||
           read(manager,0x3c00)!=mode || read<uint8_t>(health,0xe)!=frozen ||
           read<float>(health,0x14)!=amount) return result;
        result.boundary={serial,epoch,{serial,serial,serial,o.camera_generation,serial}};
        result.decision=Decision(result.boundary);
        rejection_="native-fps-eligible";
        return result;
    }
    // All virtual root/component queries were completed by the caller FIRST.
    // This final original SP query has no owner-mutating callback on its proved
    // cohort. Nested health receipt distinguishes absent health from alive.
    Receipt receipt{this}; auto *previous=receipt_; receipt_=&receipt;
    bool dead;
    try { dead=abi::invoke_bool(is_dead_,{o.actor}); }
    catch(...) { receipt_=previous; throw; }
    receipt_=previous;
    if(!match(!dead && receipt.safe && receipt.calls==1 && receipt.health,"native-fps-health") || !fields() ||
       read(manager,4)!=0 || read(o.actor,0x118)!=receipt.health) return result;
    // These serials certify THIS fresh observation, not unknown allocation
    // generations. Only camera_generation is an existing native lifecycle token.
    result.boundary={serial,epoch,{serial,serial,serial,o.camera_generation,serial}};
    result.decision=Decision(result.boundary);
    rejection_="native-fps-eligible";
    return result;
}
} // namespace nova::camera::viewmodel_eligibility
