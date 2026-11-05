#pragma once
#include "menu/menu_ack_snapshot.h"
#include "menu/native_menu_observer.h"
#include "abi/guest_hook.h"
#include "platform/native_device_profile.h"
#include <windows.h>
#include <cstring>
#include <stdexcept>
namespace nova::diagnostic::t09h {
// Main GL lane only; original getters use the aligned bridge. No guest callbacks.
class NativeSnapshotReader {
    const ElfImage& image_;
    uint32_t app_get_,level_get_,player_get_;
public:
    template<class T> T read(uint32_t p) const {
        MEMORY_BASIC_INFORMATION m{};
        if(!p || uint64_t(p)+sizeof(T)>UINT32_MAX ||
           !VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof m) || m.State!=MEM_COMMIT ||
           (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
           uint64_t(p)+sizeof(T)>uint64_t(reinterpret_cast<uintptr_t>(m.BaseAddress))+m.RegionSize)
            throw std::runtime_error("menu-ack unreadable native field");
        T out{};std::memcpy(&out,reinterpret_cast<const void*>(p),sizeof out);return out;
    }
    uint32_t field(uint32_t p,uint32_t offset) const {
        if(!p || uint64_t(p)+offset>UINT32_MAX) throw std::runtime_error("menu-ack pointer overflow");
        return p+offset;
    }
public:
    uint32_t address(uint32_t va,size_t bytes=1) const {return image_.address(va,bytes);}
    uint32_t app() const {return abi::invoke(app_get_);}
    uint32_t level() const {return abi::invoke(level_get_);}
    uint32_t player(uint32_t owner) const {return abi::invoke(player_get_,{owner});}
    NativeSnapshotReader(const ElfImage& image,const platform::NativeDeviceProfile& admitted)
      :image_(image),app_get_(abi::require_function(image,"_ZN11Application11GetInstanceEv")),
       level_get_(abi::require_function(image,"_ZN6CLevel8GetLevelEv")),
       player_get_(abi::require_function(image,"_ZN6CLevel18GetPlayerComponentEv")) {
        if(!admitted.applied())throw std::runtime_error("menu-ack requires authenticated native image");
    }
    SnapshotResult snapshot(uint64_t frame,bool gameplay,const menu::NativeMenuObserver& observer) const {
        return read_snapshot(*this,frame,gameplay,observer.snapshot(),observer.backing_snapshot());
    }
};
}
