#pragma once
#include "abi/guest_hook.h"
#include "input/controls.h"
#include <atomic>
#include <map>
#include <memory>

namespace nova::input {
// Installed at the ONE ordinary Application::_Update CALL to UpdateEvents.
// Never runs inside PlayerComponent::UpdateFromInput and never queues a packet.
class NativeGamepadPostDrain {
  public:
    NativeGamepadPostDrain(const ElfImage &,Controls &);
    ~NativeGamepadPostDrain();
    void begin_frame();
    void end_frame();
    // Existing native writer hooks call these before forwarding their original.
    // They inspect no player storage and do not erase physical held input.
    void native_event(uint32_t control,uint32_t event);
    void native_reset(uint32_t control);
    // Existing refresh supplies its already-read identity before role take().
    // Revocation only: never admits input or calls the guest from refresh.
    void target_observed(uint32_t level,uint32_t player,uint32_t normal,uint32_t scheme,bool other_role_enabled);
  private:
    struct Owner {
        uint32_t level=0,player=0,actor=0,normal=0,scheme=0,input_template=0;
        bool supported=false;
        bool operator==(const Owner &) const = default;
    };
    struct Admission { Owner owner; bool valid=false,live=false,move=false,look=false; };
    static NativeGamepadPostDrain *instance_;
    static uint32_t __cdecl drain(uint32_t,uint32_t);
    static uint32_t __cdecl ctor1(uint32_t,uint32_t);
    static uint32_t __cdecl ctor2(uint32_t,uint32_t);
    static uint32_t __cdecl dtor0(uint32_t);
    static uint32_t __cdecl dtor1(uint32_t);
    static uint32_t __cdecl dtor2(uint32_t);
    static uint32_t __cdecl reset(uint32_t);
    static uint32_t __cdecl enable(uint32_t,uint32_t);
    uint32_t fn(const char *) const;
    Admission acquire();
    void observe(const Owner &);
    void retire(uint32_t);
    void deliver();
    void emit(uint32_t,uint32_t,float,float);
    Controls &input_;
    std::map<std::string,uint32_t> functions_;
    uint32_t event_manager_=0,event_move_=0,event_release_=0,player_table_=0,ingame_table_=0;
    std::atomic<uint32_t> lane_{0},watched_{0};
    std::atomic<uint64_t> serial_{1},epoch_{1},writer_{1};
    uint64_t seen_epoch_=0;
    Owner owner_{};
    bool armed_=false,look_owned_=false,move_owned_=false;
    unsigned depth_=0;
    uint32_t host_packet_=0;
    std::unique_ptr<abi::GuestCall> drain_;
    std::unique_ptr<abi::GuestHook> ctor1_,ctor2_,dtor0_,dtor1_,dtor2_,reset_,enable_;
};
}
