#include "native_camera_bob.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nova::movement_wobble {
namespace {
using camera::NativeWorldFov;
template<class T=uint32_t>T read(uint32_t p,uint32_t off=0){return *reinterpret_cast<const T*>(p+off);}
bool uid(uint32_t p,const char* name) {
    if(!p)return false;
    for(unsigned i=0;i<64;++i){const char c=read<char>(p,i);if(c!=name[i])return false;if(!c)return true;}return false;
}
CameraView view(uint32_t p) {
    CameraView v;
    std::memcpy(v.eye.data(),reinterpret_cast<void*>(p+0x40),12);
    std::memcpy(v.target.data(),reinterpret_cast<void*>(p+0x10c),12);
    std::memcpy(v.up.data(),reinterpret_cast<void*>(p+0x118),12);return v;
}
void put_view(uint32_t p,const CameraView& v) {
    std::memcpy(reinterpret_cast<void*>(p+0x40),v.eye.data(),12);
    std::memcpy(reinterpret_cast<void*>(p+0x10c),v.target.data(),12);
    std::memcpy(reinterpret_cast<void*>(p+0x118),v.up.data(),12);
}
bool locomotion(uint32_t weapon) {
    if(!weapon || read<uint8_t>(weapon,0x61))return false;
    const auto object=read(weapon,4);if(!object)return false;
    const auto animation=read(object,0x100);if(!animation || read(animation,4)!=object)return false;
    const auto index=read(weapon,0xf4),count=read(animation,0x14),base=read(animation,0x20);
    if(!base || index>=count || count>256)return false;
    const auto slot=base+56*index,timeline=read(slot,0x28);if(!timeline)return false;
    const auto clip=read(timeline,0x30);
    return (clip==56 || clip==101) && read(slot)==clip && (read(slot,0x34)&0x1000) &&
        (read<uint8_t>(weapon,0x64)!=0)==(clip==101);
}
}
NativeCameraBob* NativeCameraBob::instance_=nullptr;
NativeCameraBob::NativeCameraBob(const ElfImage& image,const RequestedStrength& request,bool fixture):requested_(request) {
    if(instance_)throw std::runtime_error("Only one camera bob adapter");
    node_table_=fixture ? image.symbol("fov_bob_node_table") : image.address(0x1120b90,4);
    const auto owner=fixture ? "fov_bob_registration" : "_ZN22CCustomCameraSceneNode27onRegisterSceneNodeInternalEPKv";
    const auto offset=fixture ? image.symbol("fov_bob_registration_call")-image.symbol(owner) : 0x1a5;
    registration_=std::make_unique<abi::GuestCall>(image,
        owner,offset,
        fixture ? "fov_bob_registration_base" : "_ZN6glitch5scene16CCameraSceneNode27onRegisterSceneNodeInternalEPKv",
        reinterpret_cast<uint32_t>(&register_camera));
    original_registration_=registration_->original();instance_=this;
}
NativeCameraBob::~NativeCameraBob(){registration_.reset();instance_=nullptr;}
void NativeCameraBob::bind_render_thread(){lane_=GetCurrentThreadId();}
void NativeCameraBob::prepare_step(){
    ready_node_=0;
    if(requested_.requested().percent()==100){capsule_={};reference_node_=0;return;}
    capsule_=NativeWorldFov::acquire_premix_owners();
}
void NativeCameraBob::finish_step() noexcept {
    ready_node_=0;
    const auto strength=requested_.requested().percent();const auto now=GetTickCount64();
    if(strength!=reported_strength_ || (strength<100 && now>=report_at_)) {
        std::printf("camera-bob strength=%d registrations=%llu admitted=%llu matrices=%llu\n",
            strength,counters_.registrations,counters_.admitted,counters_.matrices);
        reported_strength_=strength;report_at_=now+10000;
    }
}
void NativeCameraBob::capture(uint32_t p) {
    if(requested_.requested().percent()==100 || GetCurrentThreadId()!=lane_)return;
    ++counters_.registrations;
    if(p!=capsule_.camera_node_identity())return;
    ready_node_=0;
    const auto sample=NativeWorldFov::sample_premix_eligibility(capsule_);
    if(!sample.decision.permits(sample.boundary)){reference_node_=0;return;}
    const auto weapon=capsule_.weapon_identity();
    if(!locomotion(weapon)){reference_node_=0;return;}
    // Camera01's grandparent carries physical input and native damage/script
    // shake. Stabilize the view relative to that CURRENT parent, never world
    // space, so mouse look, translation, jumping and shake remain responsive.
    const auto parent=read(p,0xd0);if(!parent || read(parent)!=node_table_)return;
    const auto snode=read(parent,0x134);if(!snode || !uid(read(snode),"Camera01-node"))return;
    const auto basis=read(parent,0xd0);if(!basis || read(basis)!=node_table_)return;
    const auto metadata=read(basis,0x134);if(!metadata || !uid(read(metadata),"dummy_camera-node"))return;
    if(read(capsule_.game_camera_identity(),0x270)!=basis)return;
    ViewMatrix matrix;std::memcpy(matrix.data(),reinterpret_cast<void*>(basis+0x10),64);
    const auto inverse=inverse_view_basis(matrix);if(!inverse)return;
    const auto current=view(p);if(!finite_view(current))return;
    if(reference_node_!=p || reference_parent_!=basis || reference_weapon_!=weapon ||
       reference_generation_!=capsule_.camera_generation()) {
        reference_=transform_view(*inverse,current);reference_node_=p;reference_parent_=basis;
        reference_weapon_=weapon;reference_generation_=capsule_.camera_generation();
    }
    const auto reduced=reduce_camera_view(current,reference_,matrix,requested_.requested().percent());
    if(!finite_view(reduced))return;
    native_=current;reduced_=reduced;ready_node_=p;++counters_.admitted;
}
uint32_t __cdecl NativeCameraBob::register_camera(uint32_t p,uint32_t context) noexcept {
    try {auto& s=*instance_;s.capture(p);return abi::invoke(s.original_registration_,{p,context});}
    catch(...){std::fputs("Camera view registration failure\n",stderr);std::abort();}
}
void NativeCameraBob::recalculate(uint32_t original,uint32_t p) {
    auto* s=instance_;
    if(!s || GetCurrentThreadId()!=s->lane_ || s->depth_ || s->ready_node_!=p ||
       s->requested_.requested().percent()==100 || !NativeWorldFov::premix_camera_alive(s->capsule_) || view(p)!=s->native_) {
        abi::invoke(original,{p});return;
    }
    const auto saved=s->native_,reduced=s->reduced_;
    put_view(p,reduced);++s->depth_;++s->counters_.matrices;
    auto restore=[&] {
        --s->depth_;
        if(NativeWorldFov::premix_camera_alive(s->capsule_) && view(p)==reduced)put_view(p,saved);
    };
    try {abi::invoke(original,{p});}catch(...){restore();throw;}restore();
}
}
