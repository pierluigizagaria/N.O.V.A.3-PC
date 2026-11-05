#pragma once
#include "menu/menu_ack_gate.h"
#include "menu/native_menu_observer.h"
#include <array>
#include <stdexcept>
namespace nova::diagnostic::t09h {
struct SnapshotResult { Snapshot state; int width=0,height=0; };
// Same field-decoding seam in production and synthetic host fixtures.
template<class Reader> SnapshotResult read_snapshot(Reader& r,uint64_t frame,bool gameplay,
    const menu::NativeMenuObserver::Snapshot& cpu,const menu::RttBackingCapture::Snapshot& backing) {
        SnapshotResult result;auto& s=result.state;s.frame=frame;
        // Complete callback-capable queries before acquiring any stack/Splash owner.
        const auto level=r.level();
        s.player_absent=!level || !r.player(level);
        const auto app=r.app();
        const auto index=r.template read<int32_t>(r.field(app,0x1876c));
        if(index<0 || index>12)throw std::runtime_error("menu-ack stack index");
        const auto current=r.template read<uint32_t>(r.field(app,0x18738+4*static_cast<uint32_t>(index)));
        const auto vptr=r.template read<uint32_t>(current);
        const auto state=[&](uint32_t va){return vptr==r.address(va);};
        s.state=state(0x1107b28)?State::logo:state(0x1107b88)?State::intro:
            state(0x1108428)?State::load_menu:state(0x11079c8)?State::splash:
            state(0x1108488)?State::menu:State::other;
        s.gameplay_absent=!gameplay;s.no_other_input=true; // CLI rejects all other input sources.
        if(s.state==State::splash) {
            s.splash_identity=current;
            if(index>0) {
                const auto below=r.template read<uint32_t>(r.field(app,0x18738+4*static_cast<uint32_t>(index-1)));
                s.underlying_menu=r.template read<uint32_t>(below)==r.address(0x1108488);
            }
            s.acknowledgement_clear=!r.template read<uint8_t>(r.field(current,0x28)) && !r.template read<uint8_t>(r.field(current,0x29));
            s.moga_clear=!r.template read<uint8_t>(r.address(0x115f838)) &&
                !r.template read<uint8_t>(r.address(0x115f839)) && !r.template read<uint8_t>(r.address(0x115f83a));
            const auto touch=r.template read<uint32_t>(r.address(0x11568c4,4));
            if(r.template read<uint32_t>(touch)!=r.address(0x1107e28))throw std::runtime_error("menu-ack touch owner");
            const auto bounds=r.template read<std::array<int16_t,8>>(r.field(touch,8));
            result.width=bounds[2];result.height=bounds[3];
            s.viewport_stable=!bounds[0] && !bounds[1] && result.width>0 && result.height>0 &&
                bounds[6]==result.width && bounds[7]==result.height;
        }
        
        s.cpu_complete=cpu.enabled && cpu.complete;
        const auto& a=backing.targets[0];const auto& b=backing.targets[1];
        s.manager_identity=a.manager;s.generation=a.generation;
        s.two_targets_registered=backing.enabled && a.registered && b.registered &&
            a.receiver && b.receiver && a.receiver!=b.receiver && a.manager==b.manager && a.generation==b.generation;
        s.targets_healthy=!a.invalid && !b.invalid && !a.retired && !b.retired;
        s.any_begin=a.begun || b.begun;
        s.any_return_with_events=(a.begun && a.returned && a.count) || (b.begun && b.returned && b.count);
        s.valid=true;return result;
}
template<class Receipt> bool balanced_pair(const Receipt& before,const Receipt& after,unsigned count) noexcept {
    return count==2 && !before.held && after.down==before.down+1 && after.up==before.up+1 && !after.held;
}
}
