#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <stdexcept>
namespace nova::ui {
struct MenuSurface {
    uint32_t owner=0,generation=0,receiver=0,target=0,driver=0;
    unsigned density=1;
    int width=0,height=0,drawn_percent=100;
};
// Scalar identities only. Release the lock before any guest call.
class MenuSurfaces {
public:
    static void publish(MenuSurface surface) {
        std::lock_guard guard(mutex_);
        for(auto& value:values_) if(!value.receiver || value.receiver==surface.receiver) { value=surface; return; }
        throw std::runtime_error("Initial menu surface registry is full");
    }
    static MenuSurface target(uint32_t driver,uint32_t target) {
        std::lock_guard guard(mutex_);
        for(const auto& value:values_) if(value.target==target && value.driver==driver) return value;
        return {};
    }
    static MenuSurface receiver(uint32_t receiver) {
        std::lock_guard guard(mutex_);
        for(const auto& value:values_) if(value.receiver==receiver) return value;
        return {};
    }
    static void drawn(uint32_t receiver,int percent) {
        std::lock_guard guard(mutex_);
        for(auto& value:values_) if(value.receiver==receiver) value.drawn_percent=percent;
    }
    static void retire(uint32_t receiver) {
        std::lock_guard guard(mutex_);
        for(auto& value:values_) if(value.receiver==receiver) value={};
    }
    static void retire(uint32_t owner,uint32_t generation) {
        std::lock_guard guard(mutex_);
        for(auto& value:values_) if(value.owner==owner && value.generation==generation) value={};
    }
private:
    inline static std::mutex mutex_;
    inline static std::array<MenuSurface,2> values_{};
};
}
