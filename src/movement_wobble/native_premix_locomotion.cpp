#include "native_premix_locomotion.h"
#include "camera/native_world_fov.h"
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <cmath>
namespace nova::movement_wobble {
namespace {
using camera::NativeWorldFov;
template<class T=uint32_t>T rd(uint32_t p,uint32_t o=0){return *reinterpret_cast<const T*>(p+o);}
uint32_t ptr(const void* p){return reinterpret_cast<uint32_t>(p);}
uint32_t bits(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}
bool permitted(const camera::viewmodel_eligibility::NativeSample& s){return s.decision.permits(s.boundary);}
struct Retain {
    uint32_t p,release;
    Retain(uint32_t base,uint32_t fn):p(base),release(fn){_InterlockedIncrement(reinterpret_cast<volatile long*>(p+4));}
    ~Retain() noexcept {try{abi::invoke(release,{p});}catch(...){std::fputs("O08h retained native release failure\n",stderr);std::abort();}}
};
struct Reference {
    uint32_t block=0,drop;
    explicit Reference(uint32_t fn):drop(fn){}
    void close(){if(block){const auto p=block;block=0;abi::invoke(drop,{p});}}
    ~Reference() noexcept {try{close();}catch(...){std::fputs("O08h independent reference drop failure\n",stderr);std::abort();}}
};
struct Input {
    uint32_t output=0,target=0,record=0,track=0,size=0,type=0;
    bool operator==(const Input&)const=default;
};
struct Owner {
    uint32_t weapon=0,object=0,animation=0,slot=0,timeline=0,set=0,library=0,root=0,clip=0,start=0,end=0;
    bool operator==(const Owner&)const=default;
};
// Native fields, never an independent ordinary-FPS predicate. Call only after
// the current O09 sample and recheck after every callback-capable operation.
Owner owner(uint32_t w,uint32_t j,uint32_t main_table,uint32_t ktable,uint32_t stable_set) {
    Owner o; if(!w || rd(j)!=main_table || rd(j,0x28)!=stable_set || rd<uint8_t>(w,0x61))return o;
    const auto object=rd(w,4);if(!object)return o;
    const auto a=rd(object,0x100);if(!a || rd(a,4)!=object || rd(a,0x1c)!=stable_set)return o;
    const auto index=rd(w,0xf4),count=rd(a,0x14),base=rd(a,0x20);
    if(!base || index>=count || count>256)return o;
    const auto slot=base+56*index,k=rd(slot,0x28);
    if(rd(slot,0x24)!=j || !k || rd(k)!=ktable || rd(j,0xc)!=k || !(rd(slot,0x34)&0x1000))return o;
    const auto clip=rd(k,0x30);const bool rush=rd<uint8_t>(w,0x64)!=0;
    if((clip!=56 && clip!=101) || rush!=(clip==101) || rd(slot)!=clip)return o;
    const uint32_t start=rush?12166:6366,end=rush?12699:7166;
    if(rd(slot,8)!=start || rd(slot,12)!=end || rd(slot,4)!=end-start)return o;
    const auto libs=rd(stable_set,0x44),libend=rd(stable_set,0x48),li=rd(j,0x3c);
    if(!libs || libend<libs || (libend-libs)%20 || li>=(libend-libs)/20)return o;
    const auto library=libs+20*li,file=rd(library);if(!file)return o;
    const auto loaded=rd(file,0x10);if(!loaded)return o;
    const auto root=rd(loaded,0x20);if(!root || rd(root,0x24)!=138 || rd(root,0x38)!=531)return o;
    const auto clips=rd(root,0x3c);if(!clips || rd(clips,12*clip+4)!=start || rd(clips,12*clip+8)!=end)return o;
    return {w,object,a,slot,k,stable_set,library,root,clips+12*clip,start,end};
}
bool below(uint32_t target,uint32_t root){
    for(unsigned n=0;target && n<32;++n){if(target==root)return true;target=rd(target,0xd0);}return false;
}
bool same_uid(uint32_t a,uint32_t b){
    if(!a || !b)return false;
    for(unsigned i=0;i<96;++i){const auto x=rd<uint8_t>(a,i);if(x!=rd<uint8_t>(b,i))return false;if(!x)return true;}return false;
}
struct Inputs {
    std::array<Input,138> values{};unsigned count=0;bool valid=false;
    bool operator==(const Inputs&)const=default;
};
Inputs inputs(const Owner& o,uint32_t j,uint32_t cookie_ref,uint32_t unit,uint32_t rotation_table,uint32_t position_table,uint32_t node_table) {
    Inputs out;
    const auto cookie=rd(cookie_ref),buffer=rd(unit);if(!cookie || !buffer || rd(cookie,0x24)!=o.set)return out;
    // Match computeAnimationValuesEx AND the native blend consumer: mode1
    // selects +1c; mode2 selects +20 when +10 is set, otherwise +18.
    // The old fixture-only mode2/+10==0 restriction rejected live cookies.
    const auto mode=rd(cookie,0x2c);
    const auto list=rd(cookie,mode==1 ? 0x1c : mode==2 && rd(cookie,0x10) ? 0x20 : 0x18);
    const auto layout=rd(buffer,4),count=rd(buffer,12);
    if(!list || !layout || !count || rd(unit,4)>=count)return out;
    const auto begin=rd(list,8),end=rd(list,12),tracks=rd(o.set,0x18),trackend=rd(o.set,0x1c);
    if(!begin || end<begin || (end-begin)%2 || (end-begin)/2>4096 || !tracks || trackend<tracks)return out;
    const auto targets=rd(cookie,0x3c),map=rd(o.set,0x50),records=rd(o.root,0x28),root=rd(o.object,0x98);
    const auto offsetref=rd(layout,0x38),sizeref=rd(layout,0x34),cooksize=rd(cookie,0x34);
    if(!targets || !map || !records || !root || !offsetref || !sizeref || !cooksize)return out;
    const auto offsets=rd(offsetref),sizes=rd(sizeref),szend=rd(sizeref,4),cs=rd(cooksize),cend=rd(cooksize,4),data=rd(buffer,8);
    if(!offsets || !sizes || !cs || !data || szend<sizes || cend<cs)return out;
    for(auto p=begin;p<end;p+=2){
        const auto i=rd<uint16_t>(p);
        if(i>=(trackend-tracks)/4 || i>=szend-sizes || i>=cend-cs)return {};
        const auto target=rd(targets,4*i);if(!target)continue;
        const auto mask=rd(cookie,0xc),maskbits=mask?rd(mask,8):0;
        if(maskbits && !(rd(maskbits,4*(i/32))&(1u<<(i%32))))continue;
        const auto e=map+8*(rd(j,0x38)+i),kind=rd(e);
        if(kind==1)continue; // Native constant input: identical at every time.
        if(kind!=2)continue; // Native skips unbound mapping entries.
        const auto b=rd(e,4);if(b<records || b-records>=138*32 || (b-records)%32)return {};
        const auto channel=rd(b,0x10);if(!channel)return {};
        const auto type=rd(channel,4);if(type!=1 && type!=5)continue; // Other properties remain native.
        // Only the authored camera branch. Arms, hands and weapon ancestors
        // retain their exact native Main samples. Names are joined again to
        // the current SNode and ancestry below; a name alone grants no write.
        const auto uid=rd(channel);
        if(!same_uid(uid,ptr("Camera01-node")) && !same_uid(uid,ptr("dummy_camera-node")) &&
           !same_uid(uid,ptr("dummy_camera_PIVOT-node")) && !same_uid(uid,ptr("Camera01_Target-node")))continue;
        const uint32_t size=type==1?12u:16u,table=type==1?position_table:rotation_table;
        // Authenticate layout before any target metadata/ancestry read. Mesh nodes
        // have a different allocation/layout and are deliberately not admitted.
        if(rd(target)!=node_table)return {};
        const auto track=rd(b,0x14),descriptor=rd(b,8),snode=rd(target,0x134);
        if(!track || rd(track)!=table || rd(tracks,4*i)!=track || rd(b,0x1c)!=0 || !descriptor ||
           rd<uint8_t>(sizes,i)!=size || rd<uint8_t>(cs,i)!=size || !below(target,root) || !snode || !same_uid(rd(channel),rd(snode)))return {};
        if(rd(descriptor,4)!=3 || rd(descriptor,0x1c)!=(size<<16) || out.count==out.values.size())return {};
        const uint64_t offset=uint64_t(rd<uint16_t>(offsets,2*i))*count+uint64_t(size)*rd(unit,4);
        if(offset>UINT32_MAX-data)return {};
        Input value{data+static_cast<uint32_t>(offset),target,b,track,size,type};
        for(unsigned k=0;k<out.count;++k)if(out.values[k].output==value.output)return {};
        out.values[out.count++]=value;
    }
    out.valid=out.count!=0;return out;
}
}
struct NativePremixLocomotion::Prepared {
    NativeWorldFov::PreMixOwnerCapsule capsule;
    Owner owner;
    uint32_t main,blocks=0;
    Retain main_lease,set_lease;
    Reference reference;
    Prepared(NativeWorldFov::PreMixOwnerCapsule c,Owner o,uint32_t j,uint32_t release,uint32_t drop):
        capsule(c),owner(o),main(j),main_lease(j+0x50,release),set_lease(o.set,release),reference(drop){}
};
NativePremixLocomotion* NativePremixLocomotion::instance_=nullptr;
thread_local unsigned NativePremixLocomotion::depth_=0;
NativePremixLocomotion::NativePremixLocomotion(const ElfImage& image,const RequestedStrength& request):image_(image),requested_(request){
    if(instance_)throw std::runtime_error("Only one pre-mix locomotion adapter");
    node_table_=image.address(0x1120b90,4);
    main_table_=image.address(0x1100d0c,0xd0);set_table_=image.address(0x11103e8,0x18);
    timeline_table_=image.address(0x10f92ec,0x40);track_table_=image.address(0x1127168,0x50);position_table_=image.address(0x11273a8,0x50);
    if(rd(main_table_-12)!=80 || rd(track_table_,0x44)!=image.address(0x811110,1) || rd(track_table_,0x10)!=image.address(0x92b3d0,1) || rd(position_table_,0x44)!=image.address(0x811110,1) ||
       rd(position_table_,0x2c)!=image.address(0x918fc0,1) || rd(position_table_,0x30)!=image.address(0x8f9f70,1))
        throw std::runtime_error("Unsupported native locomotion ABI");
    manager_slot_=image.symbol("_ZN6glitch7collada26CAnimationStreamingManager8InstanceE");
    if(!manager_slot_)throw std::runtime_error("Missing native stream manager");
    get_block_=abi::require_function(image,"_ZN6glitch7collada26CAnimationStreamingManager17getAnimationBlockERKNS0_24SAnimationBlockSearchKeyERN5boost13intrusive_ptrINS0_15CAnimationBlockEEE");
    drop_=abi::require_function(image,"_ZN6glitch7collada15CAnimationBlock4dropEv");
    release_=abi::require_function(image,"_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE");
    sample_=rd(track_table_,0x44);blend_=rd(track_table_,0x10);
    call_=std::make_unique<abi::GuestCall>(image,
        "_ZN6glitch7collada21CSceneNodeAnimatorSet22computeAnimationValuesEfRN5boost13intrusive_ptrINS0_20CAnimationTreeCookieEEERNS0_13CBlendingUnitE",0xf6,
        "_ZN6glitch7collada21CSceneNodeAnimatorSet24computeAnimationValuesExEfRN5boost13intrusive_ptrINS0_20CAnimationTreeCookieEEERNS0_13CBlendingUnitE",ptr(reinterpret_cast<const void*>(&compute)));
    original_=call_->original();instance_=this;
}
NativePremixLocomotion::~NativePremixLocomotion(){call_.reset();prepared_.reset();instance_=nullptr;}
void NativePremixLocomotion::bind_render_thread(){lane_=GetCurrentThreadId();}
void NativePremixLocomotion::finish_step() noexcept {
    try { prepared_.reset(); } catch(...) { std::fputs("O08h1 host lease cleanup failure\n",stderr);std::abort(); }
}
void NativePremixLocomotion::prepare_step(){
    if(GetCurrentThreadId()!=lane_)throw std::runtime_error("Pre-mix preparation must use the bound host lane");
    prepared_.reset(); // Native drop/releases occur at the HOST boundary only.
    if(requested_.requested().percent()==100)return;
    ++counters_.acquisitions;
    const auto capsule=NativeWorldFov::acquire_premix_owners();
    if(!permitted(NativeWorldFov::sample_premix_eligibility(capsule)))return;
    const auto w=capsule.weapon_identity(),object=rd(w,4);if(!object)return;
    const auto a=rd(object,0x100);if(!a || rd(a,4)!=object)return;
    const auto index=rd(w,0xf4),base=rd(a,0x20),count=rd(a,0x14);
    if(!base || index>=count || count>256)return;
    const auto j=rd(base+56*index,0x24);if(!j || rd(j)!=main_table_)return;
    const auto h=rd(j,0x28);if(!h || rd(h)!=set_table_)return;
    const auto o=owner(w,j,main_table_,timeline_table_,h);if(!o.weapon)return;
    auto pending=std::make_unique<Prepared>(capsule,o,j,release_,drop_);
    const auto manager=rd(manager_slot_);if(!manager)return;
    const std::array<uint32_t,3> search{o.library,o.clip,o.start};
    ++counters_.leases;abi::invoke(get_block_,{manager,ptr(search.data()),ptr(&pending->reference.block)});
    if(!pending->reference.block)return;
    const auto loaded=rd(pending->reference.block,0x20);if(!loaded)return;
    pending->blocks=rd(loaded,0x10);if(!pending->blocks)return;
    if(!permitted(NativeWorldFov::sample_premix_eligibility(capsule)))return;
    if(owner(w,j,main_table_,timeline_table_,h)!=o)return;
    prepared_=std::move(pending);
}
void __cdecl NativePremixLocomotion::compute(uint32_t j,uint32_t time,uint32_t cookie,uint32_t unit) noexcept {
    try{instance_->execute(j,time,cookie,unit);}catch(...){std::fputs("O08h native pre-mix callback failure\n",stderr);std::abort();}
}
void NativePremixLocomotion::execute(uint32_t j,uint32_t time,uint32_t cookie,uint32_t unit){
    const auto strength=requested_.requested();
    // The actual wrapper bypass: no owner reads, references, O09 or sampling.
    if(strength.percent()==100){abi::invoke(original_,{j,time,cookie,unit});return;}
    if(GetCurrentThreadId()!=lane_ || depth_ || rd(j)!=main_table_){abi::invoke(original_,{j,time,cookie,unit});return;}
    struct Depth{Depth(){++depth_;}~Depth(){--depth_;}} depth;
    abi::invoke(original_,{j,time,cookie,unit});
    if(!prepared_ || prepared_->main!=j)return;
    const auto &capsule=prepared_->capsule;const auto h=prepared_->owner.set;
    if(!permitted(NativeWorldFov::sample_premix_eligibility(capsule)))return;
    const auto o=owner(capsule.weapon_identity(),j,main_table_,timeline_table_,h);
    if(!o.weapon || o!=prepared_->owner || rd(o.timeline,4)!=time)return;
    float t;std::memcpy(&t,&time,4);if(!(t>=static_cast<float>(o.start) && t<static_cast<float>(o.end)))return;
    const auto selected=inputs(o,j,cookie,unit,track_table_,position_table_,node_table_);
    if(!selected.valid)return;
    const auto live=rd(j,0x4c),cookie_value=rd(cookie),buffer=rd(unit);
    std::array<Quaternion,138> original{},reduced{};
    for(unsigned n=0;n<selected.count;++n)std::memcpy(original[n].data(),reinterpret_cast<void*>(selected.values[n].output),selected.values[n].size);
    const auto blocks=prepared_->blocks; // Independent lease held until next HOST boundary.
    for(unsigned n=0;n<selected.count;++n){
        std::array<uint32_t,4> cache{0,0xff7fffff,0,0};reinterpret_cast<uint8_t*>(cache.data())[13]=1;
        uint32_t key=0;const std::array<uint32_t,3> accessor{selected.values[n].record,blocks,ptr(cache.data())};Quaternion start{};
        abi::invoke(sample_,{selected.values[n].track,ptr(accessor.data()),bits(static_cast<float>(o.start)),ptr(start.data()),ptr(&key),1});
        for(unsigned k=0;k<selected.values[n].size/4;++k)if(!std::isfinite(original[n][k]) || !std::isfinite(start[k]))return;
        if(selected.values[n].type==1){
            // O08c proved float3 linear interpolation in original918fc0.
            // Exact endpoint copies; no unit conversion, weight or final pose edit.
            reduced[n]=start;
            if(strength.percent()!=0){const float p=static_cast<float>(strength.percent())/100.f;
                for(unsigned k=0;k<3;++k)reduced[n][k]=(1-p)*start[k]+p*original[n][k];}
            continue;
        }
        reduced[n]=reduce_sample(original[n],&start,strength,[&](const Quaternion& a,const Quaternion& b,float amount){
            const std::array<Quaternion,2> values{a,b};const std::array<float,2> weights{1-amount,amount};Quaternion value{};
            abi::invoke(blend_,{selected.values[n].track,ptr(values.data()),ptr(weights.data()),2,ptr(value.data())});return value;
        });
    }
    // Reference sampler/blend are the proved typed pure cohort. No owner/root
    // callback, streaming acquisition or release occurs inside this native caller.
    if(!permitted(NativeWorldFov::sample_premix_eligibility(capsule)))return;
    if(owner(capsule.weapon_identity(),j,main_table_,timeline_table_,h)!=o || rd(o.timeline,4)!=time ||
       rd(j,0x4c)!=live || rd(cookie)!=cookie_value || rd(unit)!=buffer || inputs(o,j,cookie,unit,track_table_,position_table_,node_table_)!=selected)return;
    for(unsigned n=0;n<selected.count;++n)if(std::memcmp(reinterpret_cast<void*>(selected.values[n].output),original[n].data(),selected.values[n].size))return;
    // Authenticated locomotion camera inputs only, before native layer mixing.
    // No weapon sample, weight, clock, physical parent, Main2, recoil or final
    // camera matrix is changed. Other camera contributors remain native.
    for(unsigned n=0;n<selected.count;++n)std::memcpy(reinterpret_cast<void*>(selected.values[n].output),reduced[n].data(),selected.values[n].size);
    ++counters_.commits;
}
}
