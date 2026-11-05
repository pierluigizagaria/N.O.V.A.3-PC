#include "ui/native_pixel_projection.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
namespace nova::ui {
namespace {
uint32_t address(const void* p){return reinterpret_cast<uint32_t>(p);}
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,reinterpret_cast<void*>(p),4);return v;}
PixelMatrix matrix(uint32_t p){PixelMatrix m;std::memcpy(m.data(),reinterpret_cast<void*>(p),sizeof m);return m;}
constexpr auto name="_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj";
}
struct NativePixelProjection::Scope::State {
    State* previous=nullptr;
    std::unordered_map<uint32_t,uint32_t> pivots;
    std::unordered_map<uint32_t,std::array<float,2>> anchors;
    uint32_t driver=0;
    PixelMatrix projection{};
    float factor=1;
    bool changed=false;
};
struct NativePixelProjection::Impl {
    static Impl* active;
    static thread_local Scope::State* current;
    std::unique_ptr<abi::GuestHook> hook;
    uint32_t original=0;
    explicit Impl(const ElfImage& image) {
        if(active || abi::require_function(image,name)!=image.address(0x89e650))
            throw std::runtime_error("GUI pixel projection identity mismatch");
        hook=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x83,0xec,0x10},address(reinterpret_cast<void*>(&set)));
        original=hook->original();active=this;
    }
    ~Impl(){hook.reset();active=nullptr;}
    static void publish(uint32_t driver,const PixelMatrix& projection) {
        // Native batch submission must observe the previous projection first.
        abi::invoke(word(word(driver)+0xb8),{driver});
        abi::invoke(active->original,{driver,1,address(projection.data()),0});
    }
    static uint64_t __cdecl set(uint32_t driver,uint32_t mode,uint32_t supplied,uint32_t flags) noexcept {
        try {
            const uint32_t args[]{driver,mode,supplied,flags};
            auto* s=current;
            if(s && mode==1) {
                s->projection=matrix(supplied);s->driver=driver;s->changed=false;
            } else if(s && mode==2) {
                if(!s->driver){s->driver=driver;s->projection=matrix(driver+0x1ac);}
                if(s->driver==driver) {
                    auto wanted=s->projection;
                    const auto found=s->pivots.find(supplied);
                    if(found!=s->pivots.end()) {
                        const auto world=matrix(found->second+0x10);
                        const PixelVector pivot{world[12],world[13],world[14],1};
                        const auto view=matrix(driver+0x16c);
                        const auto adjusted=pixel_projection(s->projection,view,pivot,s->factor);
                        if(!adjusted)throw std::runtime_error("GUI pivot projection invalid");
                        const auto clip=project_vector(s->projection,project_vector(view,pivot));
                        s->anchors[found->second]={clip[0]/clip[3],clip[1]/clip[3]};
                        wanted=*adjusted;
                    }
                    if(matrix(driver+0x1ac)!=wanted)publish(driver,wanted);
                    s->changed=wanted!=s->projection;
                }
            }
            return abi::call_guest_u64(active->original,args,4);
        } catch(...) {std::fputs("GUI pixel projection boundary failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
};
NativePixelProjection::Impl* NativePixelProjection::Impl::active=nullptr;
thread_local NativePixelProjection::Scope::State* NativePixelProjection::Impl::current=nullptr;
NativePixelProjection::NativePixelProjection(const ElfImage& image):impl_(std::make_unique<Impl>(image)){}
NativePixelProjection::~NativePixelProjection()=default;
NativePixelProjection::Scope::Scope(const std::vector<uint32_t>& groups,float factor,const std::unordered_map<uint32_t,uint32_t>& shared_pivots) {
    if(!Impl::active || factor==1.f)return;
    state_=std::make_unique<State>();auto& s=*state_;s.factor=factor;
    std::unordered_set<uint32_t> seen;
    for(auto group:groups) {
        const auto explicit_pivot=shared_pivots.find(group);
        const auto pivot=explicit_pivot==shared_pivots.end()?group:explicit_pivot->second;
        std::vector<uint32_t> pending{group};
        while(!pending.empty()) {
            const auto node=pending.back();pending.pop_back();
            if(!node || !seen.insert(node).second)continue;
            if(seen.size()>8192)throw std::runtime_error("GUI scene exceeds bounded traversal");
            s.pivots.emplace(node+0x10,pivot);
            const auto sentinel=node+0xd4;
            unsigned count=0;
            for(auto link=word(sentinel);link!=sentinel;link=word(link)) {
                if(!link || ++count>8192)throw std::runtime_error("GUI sibling list invalid");
                pending.push_back(link-4);
            }
        }
    }
    s.previous=Impl::current;Impl::current=&s;
}
NativePixelProjection::Scope::~Scope() {
    if(!state_)return;
    auto& s=*state_;
    if(s.changed && s.driver)Impl::publish(s.driver,s.projection);
    Impl::current=s.previous;
}
std::optional<std::array<float,2>> NativePixelProjection::Scope::anchor(uint32_t group) const {
    if(!state_)return {};
    const auto found=state_->anchors.find(group);
    if(found==state_->anchors.end())return {};
    return found->second;
}
}
