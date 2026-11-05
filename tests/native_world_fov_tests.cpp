#include "camera/native_world_fov.h"
#include "movement_wobble/native_camera_bob.h"
#include "abi/guest_call.h"
#include "input/native_controls.h"
#include "bionic/runtime.h"
#include "fixtures/world_fov_contract.h"
#include <array>
#include <cstdlib>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <windows.h>

namespace {
using namespace nova::camera;
using nova::abi::invoke;
constexpr double pi=std::numbers::pi;
void check(bool v,const char *m) { if(!v) throw std::runtime_error(m); }
void close_value(double a,double b,const char *m,double e=3e-6) {
    if (!std::isfinite(a) || std::abs(a-b)>e) {
        std::fprintf(stderr,"%s: actual %.12g expected %.12g\n",m,a,b); check(false,m);
    }
}
uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }
uint32_t ptr(auto &v) { return reinterpret_cast<uint32_t>(&v); }
template<class T=uint32_t> T &at(uint32_t p,uint32_t o=0) { return *reinterpret_cast<T *>(p+o); }
constexpr auto get_name="_ZNK6glitch5scene16CCameraSceneNode6getFOVEv";
constexpr auto set_name="_ZN6glitch5scene16CCameraSceneNode6setFOVEf";
constexpr auto matrices_name="_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv";
constexpr auto proj_name="_ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv";
constexpr auto frustum_name="_ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv";
constexpr auto custom_name="_ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb";
constexpr auto ctor1_name="_ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE";
constexpr auto ctor2_name="_ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE";
constexpr auto dtor_name="_ZN6glitch7collada16CCameraSceneNodeD2Ev";
constexpr auto enable_name="_ZN20CPlayerJeepComponent6EnableEb";
constexpr auto helper_name="_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib";
bool audit_guard=false;
void (*reset_callback)(uint32_t)=nullptr;
std::function<void(uint32_t)> query_callback;
std::function<void(uint32_t,uint32_t,uint32_t)> weapon_callback;
void __cdecl fixture_audit(uint32_t kind) noexcept { try {
    if(audit_guard) { std::fputs("INVALID-AFTER-OOM-MUTATION-OR-QUERY\n",stderr); std::fflush(stderr); }
    if(query_callback) query_callback(kind);

    } catch(...) { std::fprintf(stderr,"V01i fixture callback failure: fixture_audit\n"); std::abort(); }
}
void __cdecl fixture_weapon_callback(uint32_t depth,uint32_t stage,uint32_t p) noexcept { try {
    if(weapon_callback) weapon_callback(depth,stage,p);

    } catch(...) { std::fprintf(stderr,"V01i fixture callback failure: fixture_weapon_callback\n"); std::abort(); }
}
void __cdecl fixture_reset_callback(uint32_t level) noexcept { try { if(reset_callback) reset_callback(level); 
    } catch(...) { std::fprintf(stderr,"V01i fixture callback failure: fixture_reset_callback\n"); std::abort(); }
}
uint32_t load_original=0;
uint32_t __cdecl load_forward(uint32_t a,uint32_t b) noexcept { try { return invoke(load_original,{a,b}); 
    } catch(...) { std::fprintf(stderr,"V01i fixture callback failure: load_forward\n"); std::abort(); }
}
struct Harness {
    const nova::ElfImage &image;
    WorldFovContract &scene;
    Harness(const nova::ElfImage &i):image(i),scene(*reinterpret_cast<WorldFovContract *>(i.symbol("fov_scene"))) {}
    uint32_t fn(const char *n) const { return nova::abi::require_function(image,n); }
    uint32_t node(unsigned n=0) { return ptr(scene.nodes[n]); }
    float get(uint32_t p) { return nova::abi::call_guest_float(fn(get_name),&p,1); }
    void reset(int w=1280,int h=720) {
        at<int>(image.symbol("g_ScreenW"))=w; at<int>(image.symbol("g_ScreenH"))=h;
        at<float>(image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=70;
        at(image.symbol("fov_root_present"))=at(image.symbol("fov_component_present"))=1;
        std::memset(&scene,0,sizeof scene); invoke(fn("fov_fixture_setup")); pseudo(node(),70); matrices(node());
    }
    void pseudo(uint32_t p,float value) { invoke(fn("fov_fixture_pseudo"),{p,bits(value)}); }
    void matrices(uint32_t p) { invoke(fn(matrices_name),{p}); }
    void owner(uint32_t p) { at(ptr(scene.camera),0x26c)=p; }
    uint32_t projection(uint32_t p) { return invoke(fn(proj_name),{p}); }
    uint32_t frustum(uint32_t p) { return invoke(fn(frustum_name),{p}); }
    void geometry(uint32_t p,double expected_v) {
        const double aspect=at<float>(p,0x12c);
        const auto m=projection(p), f=frustum(p);
        close_value(at<float>(m,0),1/std::tan(expected_v/2)/aspect,"production native projection X");
        close_value(at<float>(m,20),1/std::tan(expected_v/2),"production native projection Y");
        close_value(at<float>(f,0),std::tan(expected_v/2)*aspect,"full native frustum/ray edge");
        close_value(at<float>(f,8),std::tan(expected_v/2)*aspect*100,"native far box bounds",.0005);
        close_value(at<float>(m,0)*at<float>(f,0),1,"world marker and edge ray agree");
        close_value(get(p),expected_v,"effective getter agrees with native geometry");
    }
};
std::vector<LensCallSite> fixture_sites(const nova::ElfImage &image) {
    std::vector<LensCallSite> result;
    for(auto [name,kind]:std::initializer_list<std::pair<const char *,LensSite>>{
        {"pseudo",LensSite::pseudo_write},{"vertical",LensSite::vertical_write},{"raw",LensSite::raw},{"weapon_begin",LensSite::weapon_begin},{"save",LensSite::jeep_save},
        {"vertical_direct",LensSite::vertical_write},{"vertical_blended",LensSite::vertical_write},
        {"vertical_key",LensSite::vertical_write},{"vertical_interpolated",LensSite::vertical_write},
        {"enable",LensSite::jeep_enable},{"update",LensSite::jeep_update},{"helper",LensSite::jeep_helper},
        {"jeep_write",LensSite::jeep_write},{"weapon_write",LensSite::weapon_write},
        {"weapon_restore",LensSite::weapon_restore},{"diff",LensSite::weapon_diff},
        {"level_first",LensSite::level_first},{"level_accumulate",LensSite::level_accumulate},
        {"render_read",LensSite::render_read},{"render_write",LensSite::additive_write},{"fast",LensSite::fast_node}}) {
        const std::string owner="fov_site_"+std::string(name);
        const auto start=image.symbol(owner),call=image.symbol(owner+"_call");
        check(start && call>=start,"fixture source CALL symbols");
        const uint32_t size=(kind==LensSite::fast_node || kind==LensSite::weapon_diff) ? 5 : 6;
        auto *bytes=reinterpret_cast<const uint8_t *>(call);
        result.push_back({owner,call-start,kind,std::vector<uint8_t>(bytes,bytes+size)});
    }
    const auto render=image.symbol("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv");
    const auto sink=image.symbol("fov_eligibility_projection_call");
    check(render && sink>=render,"fixture eligibility direct first projection CALL");
    const auto *sink_bytes=reinterpret_cast<const uint8_t *>(sink);
    result.push_back({"_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv",sink-render,
        LensSite::camera_projection,std::vector<uint8_t>(sink_bytes,sink_bytes+5)});
    return result;
}
struct CodeSnapshot {
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> code;
    explicit CodeSnapshot(const nova::ElfImage &image) {
        for(const auto &s:image.segments()) if(s.flags&1) {
            auto p=image.address(s.vaddr,s.memsz); auto *bytes=reinterpret_cast<const uint8_t *>(p);
            code.push_back({p,std::vector<uint8_t>(bytes,bytes+s.memsz)});
        }
    }
    void unchanged() {
        for(auto &[p,bytes]:code) check(!std::memcmp(reinterpret_cast<void *>(p),bytes.data(),bytes.size()),
            "all executable bytes restored, including checked CALL and rejected installation");
    }
};
void native_mode(const nova::ElfImage &image) {
    CodeSnapshot original(image); Harness h(image);
    { NativeWorldFov disabled(image,{}); disabled.bind_render_thread(); h.reset(); original.unchanged();
      { NativeWorldFov::JeepUpdateScope scope(ptr(h.scene.component),16); }
      check(h.scene.queries==0,"native/absent setting performs zero camera queries or installation");
      close_value(h.get(h.node()),70.0*720/1280*pi/180,"native getter unchanged");
      h.scene.weapon_mode[0]=1;
      invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
      check(h.scene.queries==0,"native no-lens rendering adds no host queries");
      close_value(h.scene.weapon_gpu_x[0],1/std::tan((70.0*720/1280*pi/180)/2)/reference_aspect,
                  "native no-opt-in camera/driver framing remains original"); }
    original.unchanged();
}
void world_and_aspect(Harness &h) {
    const double base=2*std::atan(1/reference_aspect);
    for(auto [w,height]:std::array<std::pair<int,int>,3>{{{1280,720},{1280,800},{2520,1080}}}) {
        h.reset(w,height); const auto p=h.node(); const auto raw=at(p,0x128);
        h.geometry(p,base);
        for(unsigned repeat=0;repeat<100;++repeat) { h.matrices(p); h.geometry(p,base); }
        check(at(p,0x128)==raw,"original native lens remains bit-exact after repeated builders/getters");
        for(float target:{70.f,65.f,50.f,30.f,42.f,61.f,70.f,90.f,70.f}) {
            h.pseudo(p,target);
            const double zoom=std::tan(target/reference_aspect*pi/360)/std::tan(70/reference_aspect*pi/360);
            h.geometry(p,2*std::atan(std::tan(base/2)*zoom));
        }
        for(unsigned state:{1u,3u,0u}) { at(ptr(h.scene.camera),0x348)=state; h.geometry(p,base); }
        // Ordinary component cameras share this local GameCamera even with normal player disabled.
        for(unsigned component=0;component<5;++component) {
            const auto camera=h.node(component%2);
            at<uint8_t>(ptr(h.scene.player),0xd)=component==0;
            h.owner(camera); h.pseudo(camera,70); h.geometry(camera,base);
        }
        h.owner(p);
        at(ptr(h.scene.manager),0xdc)=h.node(2); // A distinct active HUD camera does not own explicit world queries.
        h.geometry(p,base);
        // Node aspect and guest screen ratio are independent from the captured source ratio.
        at<float>(p,0x12c)=1.6f; at<uint16_t>(p,0x300)|=2;
        at<int>(h.image.symbol("g_ScreenW"))=1600; at<int>(h.image.symbol("g_ScreenH"))=900;
        h.geometry(p,base);
        h.pseudo(p,70); h.geometry(p,base);
        at<int>(h.image.symbol("g_ScreenW"))=0;
        h.geometry(p,at<float>(p,0x128));
        at<int>(h.image.symbol("g_ScreenW"))=1600; h.geometry(p,base);
        at<float>(h.image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=0;
        h.geometry(p,at<float>(p,0x128));
        at<float>(h.image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=70; h.geometry(p,base);
    }
}
void isolation_and_custom(Harness &h) {
    h.reset(); const auto other=h.node(2); const float native=.9f;
    const auto before_queries=h.scene.queries,before_matrices=h.scene.matrices;
    invoke(h.fn(set_name),{other,bits(native)});
    close_value(h.get(other),native,"unrelated/menu camera native FOV");
    h.projection(other); h.frustum(other);
    check(h.scene.matrices==before_matrices,"unowned getter/projection adds no full matrix work");
    check(h.scene.queries==before_queries,"unowned ordinary setter/getters add no root queries");
    const auto p=h.node(); std::array<float,16> authored{}; authored[0]=3.5f; authored[5]=4.5f; authored[10]=-1; authored[11]=-1;
    invoke(h.fn(custom_name),{p,ptr(authored),0});
    const auto matrices=h.scene.matrices;
    check(h.projection(p)==p+0x200 && at<float>(p,0x200)==3.5f,"explicit custom perspective(false) preserved");
    close_value(h.get(p),at<float>(p,0x128),"custom getter retains native raw lens");
    check(h.scene.matrices==matrices,"unknown clean custom never force-rebuilt");
    h.pseudo(p,70); h.geometry(p,2*std::atan(1/reference_aspect));
    at<float>(p,0x200)=9; // Unknown clean matrix output, without a recognized setter.
    close_value(h.get(p),at<float>(p,0x128),"unknown clean output revokes effective getter");
    check(at<float>(h.projection(p),0)==9,"unknown clean perspective is not overwritten");
    h.pseudo(p,70); h.geometry(p,2*std::atan(1/reference_aspect));
    invoke(h.fn(set_name),{p,bits(.8f)}); h.geometry(p,.8);
    h.pseudo(p,70); h.geometry(p,2*std::atan(1/reference_aspect));
}
void weapon_pass(Harness &h) {
    h.reset(); const auto p=h.node(),raw=at(p,0x128); h.scene.nested_weapon=1;
    invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
    const double native=std::bit_cast<float>(raw);
    for(unsigned depth=0;depth<2;++depth) {
        const double angle=native+.2*(depth+1);
        close_value(h.scene.weapon_fov[depth],angle,"shared-node nested weapon getter remains native");
        close_value(h.scene.weapon_x[depth],1/std::tan(angle/2)/reference_aspect,"weapon projection remains native");
        close_value(h.scene.weapon_frustum[depth],std::tan(angle/2)*reference_aspect,"weapon full frustum remains native");
    }
    check(at(p,0x128)==raw,"native weapon save/restore remains bit-exact");
    h.geometry(p,2*std::atan(1/reference_aspect));
    h.owner(h.node(1)); h.pseudo(h.node(1),70); h.matrices(h.node(1)); h.owner(p);
    h.scene.replace_weapon=1; h.scene.nested_weapon=0;
    invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
    h.geometry(h.node(1),2*std::atan(1/reference_aspect));
    close_value(h.get(p),native,"old supplied receiver reconciles native without stale saved-pointer restoration");
}
void pass_projection_contract(Harness &h) {
    const double world=2*std::atan(1/reference_aspect);
    auto x=[](double v) { return 1/std::tan(v/2)/reference_aspect; };
    auto draw=[&] { invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); };
    for(unsigned mode:{1u,2u,3u}) {
        h.reset(); const auto p=h.node(),raw=at(p,0x128),before=h.scene.queries,setters=h.scene.setters;
        h.scene.weapon_mode[0]=mode;
        uint32_t entry_queries=~0u,return_queries=~0u;
        weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) {
            if(stage==0) entry_queries=h.scene.queries;
            if(stage==2) return_queries=h.scene.queries;
        };
        draw(); weapon_callback={};
        check(h.scene.setters==setters && at(p,0x128)==raw,"no-lens branch truly skips raw get/add/set/restore");
        if(mode==3) check(h.scene.camera_setups==0 && h.scene.queries==before,"empty pass has no setup or scope queries");
        else {
            close_value(h.scene.weapon_gpu_x[0],x(world),"crystal/camera-tag no-lens draw preserves INCOMING driver world projection");
            close_value(h.scene.weapon_x[0],x(world),"no-lens CPU projection equals world projection");
            close_value(h.scene.weapon_frustum[0],1/x(world),"no-lens planes/rays equal world geometry");
            close_value(h.scene.weapon_box[0],100/x(world),"no-lens far box equals world geometry",.0005);
            close_value(h.scene.weapon_marker[0],.25*x(world),"no-lens screen coordinate equals world marker");
            check(h.scene.queries==return_queries && h.scene.camera_setups==1,"pure skipped return performs no reconciliation/publication");
        }
        check(entry_queries==before,"no-lens/empty entry never predicts native branch with guest queries");
        h.geometry(p,world);
    }
    for(unsigned outer:{0u,1u,2u}) for(unsigned inner:{0u,1u,2u}) {
        h.reset(); const auto p=h.node(),raw=at(p,0x128); const double native=at<float>(p,0x128);
        h.scene.weapon_mode[0]=outer; h.scene.weapon_mode[1]=inner; h.scene.nested_weapon=1;
        const double parent=outer==0 ? native+.2 : world;
        const double child=inner==0 ? native+.2*(outer==0 ? 2 : 1) : parent;
        draw();
        close_value(h.scene.weapon_gpu_x[0],x(parent),"outer draw uses actual native branch/inherited driver optics");
        close_value(h.scene.weapon_gpu_x[1],x(child),"inner draw uses native branch or incoming parent driver optics");
        close_value(h.scene.after_gpu_x[0],x(parent),"first draw after inner return uses restored PARENT DRIVER projection");
        close_value(h.scene.after_cpu_x[0],x(parent),"nested parent CPU projection restored before continuation");
        close_value(h.scene.after_frustum[0],1/x(parent),"nested parent full planes/rays restored before continuation");
        close_value(h.scene.after_view[0],7,"nested parent driver view uses native setup path");
        check(h.scene.camera_setups==(inner==0 ? 3u : 2u),"native setup is republished only for changed nested context, never blanket outer return");
        check(at(p,0x128)==raw && h.scene.render_words[1]==0,"raw native lens and two-word setup context remain exact");
        h.geometry(p,world);
        close_value(h.scene.driver_projection[0],x(parent),"outer CPU restoration does not add unauthorized driver publication");
    }
    h.reset(); h.scene.weapon_mode[0]=h.scene.weapon_mode[1]=h.scene.weapon_mode[2]=1;
    h.scene.nested_weapon=1; h.scene.weapon_limit=2; draw();
    for(unsigned depth=0;depth<3;++depth) close_value(h.scene.weapon_gpu_x[depth],x(world),"multiple skipped frames inherit world driver context");
    check(h.scene.camera_setups==3,"multiple skipped nesting adds no host setup calls");

    // An opaque clean projection stays authored even on a skipped active receiver.
    h.reset(); std::array<float,16> authored{}; authored[0]=3.5f; authored[5]=4.5f;
    invoke(h.fn(custom_name),{h.node(),ptr(authored),0}); h.scene.weapon_mode[0]=1; draw();
    close_value(h.scene.weapon_gpu_x[0],3.5,"skipped custom clean projection forwards unchanged to driver");

    // Fresh active-camera mismatch must not republish the old parent camera.
    h.reset(); h.scene.weapon_mode[0]=1; h.scene.nested_weapon=1;
    weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
        if(depth==1 && stage==2) {
            at(ptr(h.scene.manager),0xdc)=h.node(2);
            invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{h.node(2),0});
        }
    };
    draw(); weapon_callback={};
    check(h.scene.camera_setups==3 && h.scene.driver_node==h.node(2),"nested return never uploads supported but INACTIVE old world receiver");
    h.geometry(h.node(),world);

    h.reset(); h.scene.weapon_mode[0]=1; h.scene.nested_weapon=1;
    at<float>(h.node(2),0x1f0)=31;
    weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
        if(depth==1 && stage==1) {
            at(ptr(h.scene.manager),0xdc)=h.node(2);
            invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{h.node(2),0});
        }
        if(depth==1 && stage==2) at(ptr(h.scene.manager),0xdc)=h.node();
    };
    draw(); weapon_callback={};
    close_value(h.scene.weapon_gpu_view[1],31,"nested child actually publishes a DIFFERENT receiver view to driver");
    close_value(h.scene.after_view[0],7,"parent continuation restores VIEW after a different child camera publication");
    close_value(h.scene.after_gpu_x[0],x(world),"parent continuation restores projection after a different child camera publication");
    check(h.scene.camera_setups==4 && h.scene.driver_node==h.node(),"nested setup republishes both native driver transforms on the fresh active parent receiver");
}
void pass_lifetime_guards(Harness &h) {
    auto draw=[&] { invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); };
    auto nested=[&] { h.reset(); h.scene.weapon_mode[0]=1; h.scene.nested_weapon=1; };
    bool armed=false; unsigned checks=0,last_check=0;
    nested();
    weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) { if(depth==1 && stage==2) armed=true; };
    query_callback=[&](uint32_t kind) {
        if(armed && kind==4) ++checks;
        if(armed && kind==5) { last_check=checks; armed=false; }
    };
    draw(); query_callback={}; weapon_callback={};
    check(last_check>0 && h.scene.camera_setups==3,"calibrate actual last root query before nested native publication");

    nested(); checks=0; armed=false; bool reconstructed=false;
    weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) { if(depth==1 && stage==2) armed=true; };
    query_callback=[&](uint32_t kind) {
        if(!armed || kind!=4 || ++checks!=last_check) return;
        armed=false; reconstructed=true;
        std::array<unsigned char,0x350> saved{}; const auto p=h.node();
        std::memcpy(saved.data(),reinterpret_cast<void *>(p),saved.size());
        invoke(h.fn(ctor1_name),{p,73,91});
        std::memcpy(reinterpret_cast<void *>(p),saved.data(),saved.size());
    };
    draw(); query_callback={}; weapon_callback={};
    check(reconstructed && h.scene.camera_setups==2,
          "lifecycle published DURING final root query forbids old same-bit parent driver publication");
    close_value(h.get(h.node()),at<float>(h.node(),0x128),"copied old projection cannot revive lens authority after final-query lifetime event");

    h.reset(); armed=true; reconstructed=false; const auto projections=h.scene.projections;
    query_callback=[&](uint32_t kind) {
        if(!armed || kind!=4 || h.scene.projections==projections) return;
        armed=false; reconstructed=true;
        std::array<unsigned char,0x350> saved{}; const auto p=h.node();
        std::memcpy(saved.data(),reinterpret_cast<void *>(p),saved.size());
        invoke(h.fn(ctor1_name),{p,79,97});
        std::memcpy(reinterpret_cast<void *>(p),saved.data(),saved.size());
    };
    draw(); query_callback={};
    check(reconstructed,"actual native builder post-query lifecycle callback was exercised");
    close_value(h.get(h.node()),at<float>(h.node(),0x128),"native builder cannot stamp an obsolete same-address lifetime after its final query");

    // A new current supported receiver is reconciled using its own live view.
    nested(); const auto old=h.node(),fresh=h.node(1);
    h.owner(fresh); h.pseudo(fresh,70); h.matrices(fresh); h.owner(old);
    at<float>(fresh,0x1f0)=19;
    weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
        if(depth==1 && stage==2) { h.owner(fresh); at(ptr(h.scene.manager),0xdc)=fresh; }
    };
    draw(); weapon_callback={};
    check(h.scene.driver_node==fresh,"nested publication uses fresh CURRENT receiver, never saved parent node");
    close_value(h.scene.after_view[0],19,"fresh native setup publishes distinguishable replacement receiver VIEW");
    h.geometry(fresh,2*std::atan(1/reference_aspect));
    close_value(h.get(old),at<float>(old,0x128),"old receiver gains no authority from nested replacement");
    std::puts("installed no-lens passes: incoming CPU/driver geometry, mixed nesting, fresh view and final-query lifetime guards passed");
}
void jeep_and_saved(Harness &h) {
    const double base=2*std::atan(1/reference_aspect);
    for(auto [w,height]:std::array<std::pair<int,int>,3>{{{1280,720},{1280,800},{2520,1080}}}) {
        h.reset(w,height); const auto c=ptr(h.scene.component);
        at<uint8_t>(c,0x1ac)=0;
        invoke(h.fn(enable_name),{c,1}); auto p=h.node(1); h.geometry(p,base);
        // No setter: physical target==raw, but reference still has to converge.
        at<uint8_t>(c,0x1ac)=1;
        const float raw=at<float>(p,0x128);
        at<float>(ptr(h.scene.config),0x6c)=raw*180.f/float(pi);
        const auto lens=*pseudo_horizontal_lens(raw,w,height);
        const float target=at<float>(ptr(h.scene.config),0x6c)*.017453292519943295f;
        const float output=raw+.016f*(target-raw);
        const double reference=output+.984*(lens.reference_vertical-raw);
        const auto setter_calls=h.scene.setters; h.scene.fast_mutate=1;
        { NativeWorldFov::JeepUpdateScope scope(c,16); invoke(h.fn("fov_fixture_update"),{c,16}); }
        check(h.scene.setters==setter_calls,"the intended inline native no-op really skips the setter");
        const double effective=*world_vertical_fov({output,reference},70,90.f);
        h.geometry(p,effective);
        close_value(h.scene.inside_x,at<float>(h.projection(p),0),"inline Jeep first FastNode sees finalized reference");
        close_value(h.scene.inside_x*h.scene.inside_frustum,1,"within-Update ray/frustum agreement");
        check(h.scene.fast_nodes==1,"actual checked FastNode CALL forwarded once");
        // Native bool is low byte only; 0x100 means nonforce.
        at<float>(ptr(h.scene.config),0x6c)=60;
        at<float>(ptr(h.scene.config),0x74)=1;
        const float before=at<float>(p,0x128);
        invoke(h.fn(helper_name),{c,16,0x100});
        close_value(at<float>(p,0x128),before+.016f*(float(pi/3)-before),"force high byte must not change native smoothing");
        // refSaved survives ordinary node swap as a number, not old-node authority.
        h.owner(h.node()); h.geometry(h.node(),base); h.owner(p); at<uint8_t>(c,0x1ac)=0;
        invoke(h.fn(helper_name),{c,16,0}); h.geometry(p,base);
        // The saved number remains useful after its source node is destroyed/replaced.
        invoke(h.fn(dtor_name),{p,0x111});
        p=h.node(2); h.owner(p); at(c,0x11c)=p;
        at<int>(h.image.symbol("g_ScreenW"))=1280; at<int>(h.image.symbol("g_ScreenH"))=720;
        h.pseudo(p,70); invoke(h.fn(helper_name),{c,16,0}); h.geometry(p,base);
        // Observed saved-value mismatch revokes later same-bit return too.
        const auto old_saved=at(c,0x178);
        at<float>(c,0x178)+=.1f; invoke(h.fn(helper_name),{c,16,0});
        at(c,0x178)=old_saved; h.pseudo(p,70); invoke(h.fn(helper_name),{c,16,0});
        close_value(h.get(p),at<float>(p,0x128),"unknown saved-value overwrite cannot revive by old bits");
        invoke(h.fn(ctor1_name),{h.node(1),111,222});
        invoke(h.fn(enable_name),{c,0}); invoke(h.fn(enable_name),{c,1}); p=h.node(1);
        const auto saved_unknown=at(c,0x178);
        invoke(h.fn(set_name),{p,bits(.8f)}); // Current C is unavailable before numerical mismatch.
        at<float>(c,0x178)+=.2f; invoke(h.fn(helper_name),{c,16,0});
        at(c,0x178)=saved_unknown; h.pseudo(p,70); invoke(h.fn(helper_name),{c,16,0});
        close_value(h.get(p),at<float>(p,0x128),"saved-value mismatch during unknown C still revokes old bits");
        invoke(h.fn(enable_name),{c,0}); invoke(h.fn(enable_name),{c,1});
        // Reused component address/same saved raw cannot revive the old companion.
        invoke(h.fn("_ZN20CPlayerJeepComponentC2EP11CGameObject"),{c,0xabc});
        invoke(h.fn(helper_name),{c,16,0});
        close_value(h.get(p),at<float>(p,0x128),"same-address component construction revokes saved authority");
        check(h.scene.component_words[0]==c && h.scene.component_words[1]==0xabc,"Jeep ctor complete argument forwarding");
        invoke(h.fn(enable_name),{c,1});
        at(ptr(h.scene.player),4)=0x654321; (void)h.get(p);
        at(ptr(h.scene.player),4)=ptr(h.scene.eligibility_actor); h.pseudo(p,70);
        invoke(h.fn(helper_name),{c,16,0});
        close_value(h.get(p),at<float>(p,0x128),"observed root A to B to A cannot revive old saved authority");
    }
}
Harness *reset_harness=nullptr;
unsigned reset_depth=0;
void nested_reset(uint32_t level) {
    if(reset_depth++) { --reset_depth; return; }
    auto &h=*reset_harness; const auto p=h.node(1);
    invoke(h.fn("_ZN6CLevel5ResetEv"),{level});
    h.pseudo(p,70);
    invoke(h.fn(set_name),{p,bits(.8f)}); // Native delta producer observes unknown reference C.
    invoke(h.fn("fov_fixture_delta_raw"),{p,bits(.8f),0}); // Native D remains exactly zero.
    h.pseudo(p,70);
    --reset_depth;
}
void foreign_geometry(Harness &h) {
    h.reset(); const auto p=h.node(); const float raw=at<float>(p,0x128);
    auto queries=h.scene.queries;
    std::thread setter([&] { invoke(h.fn(set_name),{p,bits(raw)}); }); setter.join();
    check(h.scene.queries==queries,"foreign same-value setter makes no game queries");
    (void)h.frustum(p); h.geometry(p,raw);
    h.pseudo(p,70); h.matrices(p);
    const auto native_projection=h.fn("_ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv");
    queries=h.scene.queries;
    std::thread projection([&] { invoke(native_projection,{p}); }); projection.join();
    check(h.scene.queries==queries,"foreign native projection forwards without root queries");
    const auto projections=h.scene.projections;
    (void)h.frustum(p); h.geometry(p,raw);
    check(h.scene.projections==projections,"fresh receiver completes planes after projection already rebuilt");
    h.pseudo(p,70); h.matrices(p);
    std::array<float,16> authored{}; authored[0]=2; authored[5]=3; authored[11]=-1;
    std::thread custom([&] { invoke(h.fn(custom_name),{p,ptr(authored),0}); }); custom.join();
    (void)h.frustum(p);
    close_value(at<float>(h.projection(p),0),2,"foreign authored projection preserved during plane refresh");
    close_value(at<float>(h.frustum(p),0),.5,"foreign authored frustum uses unchanged custom matrix");
    // Previously-owned receiver has finished cleanup; later unrelated work is pure forwarding.
    h.owner(h.node(1)); (void)h.get(p);
    const auto before=h.scene.matrices;
    invoke(native_projection,{p});
    check(h.scene.matrices==before,"formerly-owned already-native receiver adds no full rebuild");
}
void delta_and_lifetime(Harness &h) {
    h.reset(2520,1080); const auto c=ptr(h.scene.component);
    at<uint8_t>(c,0x1ac)=0; invoke(h.fn(enable_name),{c,1}); const auto p=h.node(1);
    at<uint8_t>(c,0x1ac)=1; at<float>(ptr(h.scene.config),0x6c)=60;
    invoke(h.fn(helper_name),{c,16,1});
    const double mixed=h.get(p);
    for(unsigned i=0;i<30;++i) { invoke(h.fn("fov_fixture_render"),{p}); close_value(h.get(p),mixed,"zero Level delta preserves mixed Jeep lens"); }
    // Authenticated zero native D can have a nonzero reference companion.
    const float raw=at<float>(p,0x128);
    invoke(h.fn("fov_fixture_delta_raw"),{p,bits(raw),0});
    invoke(h.fn("fov_fixture_render"),{p});
    const auto reference=*pseudo_horizontal_lens(raw,2520,1080);
    close_value(h.get(p),*world_vertical_fov(reference,70,90.f),"actual Level delta source controls additive reference at native Dzero");
    invoke(h.fn("_ZN6CLevel5ResetEv"),{ptr(h.scene.level)});
    h.pseudo(p,70); const double base=2*std::atan(1/reference_aspect);
    invoke(h.fn("fov_fixture_render"),{p}); h.geometry(p,base);
    for(unsigned repeat=0;repeat<40;++repeat) for(float target:{60.f,45.f,30.f,70.f}) {
        invoke(h.fn("fov_fixture_delta"),{p,bits(target),0});
        // A late node-aspect change cannot reinterpret the producer's screen ratio.
        at<float>(p,0x12c)=1.6f; at<uint16_t>(p,0x300)|=2;
        invoke(h.fn("fov_fixture_render"),{p});
        const double q=std::tan(target/reference_aspect*pi/360)/std::tan(70/reference_aspect*pi/360);
        h.geometry(p,2*std::atan(q/reference_aspect));
    }
    h.pseudo(p,70);
    invoke(h.fn("fov_fixture_delta"),{p,bits(60.f),0});
    invoke(h.fn("fov_fixture_delta"),{p,bits(50.f),1});
    // Independent reference recurrence for two native updates before one render.
    const double r0=70/reference_aspect, d1=60/reference_aspect-r0;
    const double d2=50/reference_aspect+d1-r0;
    const double reference_after=(r0+d2)*pi/180;
    // Producer ratio was21:9; changing guest screen ratio now must not reinterpret it.
    at<int>(h.image.symbol("g_ScreenW"))=1280; at<int>(h.image.symbol("g_ScreenH"))=720;
    invoke(h.fn("fov_fixture_render"),{p});
    close_value(h.get(p),*world_vertical_fov({at<float>(p,0x128),reference_after},70,90.f),
                "consecutive native delta updates retain producer ratio and old reference delta",1e-5);
    const float known_delta=at<float>(ptr(h.scene.level),0x2dc);
    at<float>(ptr(h.scene.level),0x2dc)=known_delta+.2f;
    invoke(h.fn("fov_fixture_render"),{p});
    h.pseudo(p,70); at<float>(ptr(h.scene.level),0x2dc)=known_delta;
    invoke(h.fn("fov_fixture_render"),{p});
    close_value(h.get(p),at<float>(p,0x128),"observed unknown delta cannot revive by old bit-pattern return");
    h.pseudo(p,70); invoke(h.fn("fov_fixture_delta"),{p,bits(60.f),0});
    invoke(h.fn("fov_fixture_render"),{p});
    const auto fallback_delta=at(ptr(h.scene.level),0x2dc);
    invoke(h.fn(set_name),{p,bits(.8f)});
    at<float>(ptr(h.scene.level),0x2dc)+=.2f;
    invoke(h.fn("fov_fixture_render"),{p});
    at(ptr(h.scene.level),0x2dc)=fallback_delta; h.pseudo(p,70);
    invoke(h.fn("fov_fixture_render"),{p});
    close_value(h.get(p),at<float>(p,0x128),"delta mismatch observed during unknown C cannot revive old bits");
    // Reset's post-zero callback nests another reset and a later producer/revocation.
    reset_harness=&h; reset_callback=&nested_reset;
    invoke(h.fn("_ZN6CLevel5ResetEv"),{ptr(h.scene.level)});
    reset_callback=nullptr; reset_harness=nullptr;
    check(h.scene.reset_calls>=3,"nested native post-zero callbacks actually ran");
    invoke(h.fn("fov_fixture_render"),{p});
    close_value(h.get(p),at<float>(p,0x128),"obsolete outer Reset post cannot revive zero seed after newer producer");
    // Current root A -> null -> A must not revive lens/saved authority.
    at(h.image.symbol("fov_root_present"))=0; (void)h.get(p);
    at(h.image.symbol("fov_root_present"))=1;
    close_value(h.get(p),at<float>(p,0x128),"observed root loss/return is a new origin epoch");
    h.pseudo(p,70); h.geometry(p,base);
    std::array<float,16> old{}; std::memcpy(old.data(),reinterpret_cast<void *>(p+0x200),64);
    invoke(h.fn(dtor_name),{p,0x31415});
    invoke(h.fn(ctor2_name),{p,0x2468,123,456});
    check(h.scene.dtor_words[0]==p && h.scene.dtor_words[1]==0x31415 && h.scene.ctor_words[3]==0x2468,
          "shared D2 and hidden-VTT C2 forward exact complete this and words");
    std::memcpy(reinterpret_cast<void *>(p+0x200),old.data(),64); at<uint16_t>(p,0x300)&=~2;
    check(h.projection(p)==p+0x200 && !std::memcmp(reinterpret_cast<void *>(p+0x200),old.data(),64),
          "same-address new lifetime cannot claim copied old effective output");
    close_value(h.get(p),.7,"same-address new lifetime has no old lens origin");
    const auto queries=h.scene.queries;
    std::thread foreign([&] { invoke(h.fn(ctor1_name),{p,77,88}); }); foreign.join();
    check(h.scene.queries==queries,"foreign lifecycle publishes without game queries");
    close_value(h.get(p),.7,"foreign lifecycle invalidates before render authority");
}
bool fail_event() { return true; }
double weapon_expected(double reference,double setting=100) {
    const double multiplier=std::tan(setting*pi/360)/(reference_aspect*std::tan((90/reference_aspect)*pi/360));
    return 2*std::atan(multiplier*std::tan(reference/2));
}
void weapon_optics(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    CodeSnapshot code(image);
    for (bool world:{false,true}) for (bool weapon:{false,true}) {
        NativeWorldFov adapter(image,world ? std::optional(90.f) : std::nullopt,&sites,nullptr,
                               weapon ? std::optional(100.f) : std::nullopt);
        adapter.bind_render_thread(); Harness h(image);
        auto draw=[&] { invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); };
        const double reference=70/reference_aspect*pi/180;
        const double world_angle=world ? 2*std::atan(1/reference_aspect) : reference;
        auto endpoints=[&](float a,float b,int width=1280,int height=720) {
            at<float>(ptr(h.scene.weapon_config),0x11c)=a;
            at<float>(ptr(h.scene.weapon_config),0x120)=b;
            for (auto [offset,value]:{std::pair{0x298u,a},std::pair{0x29cu,b}}) {
                volatile float q=value-70.f; volatile float product=q*float(height);
                volatile float quotient=product/float(width);
                at<float>(ptr(h.scene.weapon_component),offset)=quotient*std::bit_cast<float>(0x3c8efa35u);
            }
        };
        auto sample=[&](double angle,const char *message) {
            const double aspect=at<float>(h.node(),0x12c),x=1/std::tan(angle/2)/aspect;
            close_value(h.scene.weapon_gpu_x[0],x,message);
            close_value(h.scene.weapon_x[0],x,"weapon CPU projection agrees with driver");
            close_value(h.scene.weapon_fov[0],angle,"weapon effective getter");
            if (world || weapon) {
                close_value(h.scene.weapon_frustum[0],1/x,"weapon native full frustum");
                close_value(h.scene.weapon_box[0],100/x,"weapon native far box",.001);
            }
            close_value(h.scene.weapon_marker[0],.25*x,"weapon native screen marker");
            close_value(h.scene.weapon_gpu_view[0],7,"weapon native driver view");
        };
        h.reset(); endpoints(90,80); h.scene.diff_sample=at<float>(ptr(h.scene.weapon_component),0x298);
        const auto raw=at(h.node(),0x128); const float native=at<float>(h.node(),0x128)+h.scene.diff_sample;
        draw(); sample(weapon ? weapon_expected(90/reference_aspect*pi/180) : native,"independent four-way regular framing");
        check(h.scene.diff_calls==1 && h.scene.diff_manager==ptr(h.scene.weapon_manager),"one original diff manager word per actual pass");
        check(bits(h.scene.forwarded_sample)==bits(h.scene.diff_sample),"unchanged actual diff x87 float sample");
        check(at(h.node(),0x128)==raw,"native saved/add/set/restore raw bits untouched");
        h.geometry(h.node(),world_angle);
        if (!world && !weapon) check(h.scene.queries==0,"both-native four-way path has zero queries");
        for(unsigned mode:{1u,2u,3u}) {
            h.reset(); h.scene.weapon_mode[0]=mode; draw();
            if(mode!=3) sample(weapon ? reference : world_angle,"unproved top-level weapon scope is Native; legacy world-only scope is unchanged");
        }
        if(world && !weapon) {
            h.reset(); float cleanup=0,driver=0;
            weapon_callback=[&](uint32_t,uint32_t stage,uint32_t p) {
                if(stage==2) { cleanup=h.get(p); driver=h.scene.driver_projection[0]; }
            };
            draw(); weapon_callback={};
            close_value(cleanup,reference,"world-only R1 regular cleanup stays native until wrapper pop");
            close_value(driver,1/std::tan((reference+.2)/2)/reference_aspect,"world-only cleanup adds no driver publication");
        }
        if(!weapon) continue;
        // Native zoom and finite interpolation extrapolation use the actual sample.
        for(float pseudo:{70.f,45.f,85.f}) for(float diff:{-.1f,0.f,.4f}) {
            h.reset(); endpoints(90,80); h.pseudo(h.node(),pseudo); h.matrices(h.node()); h.scene.diff_sample=diff;
            draw(); sample(weapon_expected(pseudo/reference_aspect*pi/180+diff),"native zoom and overshoot compose without clamp");
            check(h.scene.diff_calls==1,"no duplicate animation/diff query during composition");
        }
        // Exact zero reveals no ratio. A single nonzero endpoint does reveal it.
        for(auto [a,b]:{std::pair{70.f,70.f},std::pair{70.f,90.f},std::pair{90.f,70.f}}) {
            h.reset(); endpoints(a,b); h.scene.diff_sample=0; draw();
            sample(weapon_expected(reference),"ratio-free zero and single-zero endpoint support");
        }
        h.reset(); endpoints(70,70); h.scene.diff_sample=.1f; draw();
        sample(at<float>(h.node(),0x128)+.1,"both-zero with nonzero output stays native");
        h.reset(); endpoints(90,80); at<float>(ptr(h.scene.weapon_component),0x29c)*=1.01f; draw();
        sample(at<float>(h.node(),0x128)+.2,"incoherent inferred Load ratios stay native");
        h.reset(); endpoints(70,90); at<float>(ptr(h.scene.weapon_component),0x298)=1e-7f; draw();
        sample(at<float>(h.node(),0x128)+.2,"zero input cannot accept epsilon nonzero delta");
        h.reset(); endpoints(90,80); at<float>(ptr(h.scene.weapon_component),0x29c)*=1.0000002f; draw();
        sample(weapon_expected(reference+.2),"coherent native roundoff accepted by relative guard");
        h.reset(); endpoints(70,70); h.scene.noop_set=1; h.scene.diff_sample=-0.f; draw();
        sample(weapon_expected(reference),"actual known native builder supports zero/no-op weapon-only output");
        check(bits(h.scene.forwarded_sample)==bits(-0.f),"signed zero forwarded bit-exactly");
        h.reset(); h.scene.diff_sample=std::bit_cast<float>(0x7fc12345u); const auto native_raw=at(h.node(),0x128); draw();
        check(bits(h.scene.forwarded_sample)==0x7fc12345u && std::isnan(h.scene.weapon_fov[0]) &&
              at(h.node(),0x128)==native_raw,"nonfinite sample stays native and forwards its actual x87 bits");
        h.reset(); h.scene.noop_set=1; h.scene.diff_sample=0;
        std::array<float,16> authored{}; authored[0]=3.5f; authored[5]=4.5f;
        invoke(h.fn(custom_name),{h.node(),ptr(authored),0}); draw();
        close_value(h.scene.weapon_gpu_x[0],3.5,"opaque clean custom output is never blessed by a no-op enter");
        close_value(h.scene.weapon_fov[0],at<float>(h.node(),0x128),"opaque custom output keeps native getter");
        h.reset(); endpoints(90,80); at<float>(ptr(h.scene.weapon_component),0x298)=std::numeric_limits<float>::denorm_min(); draw();
        sample(at<float>(h.node(),0x128)+.2,"subnormal provenance stays native");
        h.reset(); endpoints(90,80); at<float>(ptr(h.scene.weapon_config),0x11c)=std::numeric_limits<float>::infinity(); draw();
        sample(at<float>(h.node(),0x128)+.2,"nonfinite config provenance stays native");
        // Independent true-V world, old Load16:10, current4:3 and node21:9.
        h.reset(1600,1000); endpoints(90,80,1600,1000);
        const auto component=ptr(h.scene.component);
        invoke(h.fn(enable_name),{component,1});
        h.owner(h.node()); at(component,0x11c)=h.node();
        h.pseudo(h.node(),70);
        at<float>(ptr(h.scene.config),0x6c)=60;
        invoke(h.fn(helper_name),{component,16,1});
        at<int>(image.symbol("g_ScreenW"))=1200; at<int>(image.symbol("g_ScreenH"))=900;
        at<float>(h.node(),0x12c)=float(21.0/9); at<uint16_t>(h.node(),0x300)|=2;
        h.scene.diff_sample=at<float>(ptr(h.scene.weapon_component),0x298);
        draw(); sample(weapon_expected(60*pi/180+20/reference_aspect*pi/180),"mixed true-V/Load/current/node aspects remain independent");
        // Tweak does not dereference ordinary inventory at all.
        h.reset(); at(ptr(h.scene.weapon_manager),0x148)=0;
        at<uint8_t>(ptr(h.scene.tweak),0x58)=7; at<float>(ptr(h.scene.tweak),0x5c)=90;
        draw(); sample(90/reference_aspect*pi/180,"tweak-only source lacks a current weapon owner and stays Native");
        for(unsigned mutation:{0u,1u,2u,3u,4u}) {
            h.reset(); at<uint8_t>(ptr(h.scene.tweak),0x58)=1; at<float>(ptr(h.scene.tweak),0x5c)=90;
            weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) {
                if(stage!=4) return;
                if(mutation==0) at<uint8_t>(ptr(h.scene.tweak),0x58)=0;
                if(mutation==1) at<float>(ptr(h.scene.tweak),0x5c)=100;
                if(mutation==2) at(image.symbol("_ZN16WeaponsTweakable10s_instanceE"))=0;
                if(mutation==3) at<int>(image.symbol("g_ScreenW"))=1600;
                if(mutation==4) at<uint8_t>(ptr(h.scene.tweak),0x58)=2;
            };
            draw(); weapon_callback={};
            sample(at<float>(h.node(),0x128)+h.scene.forwarded_sample,"changed tweak continuity falls back natively");
        }
        // Source/owner changes during and after the actual original return.
        for(unsigned stage:{4u,5u}) {
            h.reset(); weapon_callback=[&](uint32_t,uint32_t phase,uint32_t) {
                if(phase==stage) at<float>(ptr(h.scene.weapon_config),0x11c)=95;
            };
            draw(); weapon_callback={}; sample(at<float>(h.node(),0x128)+.2,"changed ordinary source at consumption stays native");
        }
        h.reset(); query_callback=[&](uint32_t kind) {
            if(kind==6) at(ptr(h.scene.weapon_manager),0x160)=1;
        };
        draw(); query_callback={}; sample(at<float>(h.node(),0x128)+.2,"original-return replacement invalidates numerical authority");
        // Calibrate the actual LAST ownership query before the native builder,
        // then mutate fields without publishing any lifecycle event. First CPU
        // and driver samples precede every lazy getter in the ELF draw sink.
        for(bool tweak:{false,true}) {
            unsigned queries=0,last=0; bool calibrating=false;
            auto prepare=[&] {
                h.reset();
                if(tweak) { at<uint8_t>(ptr(h.scene.tweak),0x58)=1; at<float>(ptr(h.scene.tweak),0x5c)=90; }
            };
            prepare();
            weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) {
                if(stage==5) calibrating=true;
            };
            query_callback=[&](uint32_t kind) {
                if(calibrating && kind==4) ++queries;
                if(calibrating && kind==5) { last=queries; calibrating=false; }
            };
            draw(); query_callback={}; weapon_callback={}; check(last>0,"calibrated actual last native builder ownership query");
            for(unsigned mutation:{0u,1u,2u,3u,4u,5u,6u}) {
                prepare(); unsigned seen=0; bool armed_last=false,mutated=false;
                weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) {
                    if(stage==5) armed_last=true;
                };
                query_callback=[&](uint32_t kind) {
                    if(!armed_last || kind!=4 || ++seen!=last) return;
                    armed_last=false; mutated=true; at<float>(h.node(),0x1f0)=23;
                    if(mutation==0) {
                        if(tweak) at<float>(ptr(h.scene.tweak),0x5c)=100;
                        else at<float>(ptr(h.scene.weapon_config),0x11c)=95;
                    }
                    if(mutation==1) {
                        if(tweak) at(image.symbol("_ZN16WeaponsTweakable10s_instanceE"))=0;
                        else at(ptr(h.scene.weapon_manager),0x160)=1;
                    }
                    if(mutation==2) {
                        if(tweak) at<uint8_t>(ptr(h.scene.tweak),0x58)=2;
                        else at<uint8_t>(ptr(h.scene.tweak),0x58)=1;
                    }
                    if(mutation==3) {
                        if(tweak) at<int>(image.symbol("g_ScreenW"))=1600;
                        else at<float>(ptr(h.scene.weapon_component),0x29c)=.3f;
                    }
                    if(mutation==4) at(ptr(h.scene.manager),0xdc)=h.node(2);
                    if(mutation==5) at(ptr(h.scene.player),0x170)=0;
                    if(mutation==6) h.owner(h.node(1));
                };
                draw(); query_callback={}; weapon_callback={}; check(mutated,"last-query no-event source/active mutation exercised");
                const double native_pass=at<float>(h.node(),0x128)+h.scene.forwarded_sample;
                const double native_x=1/std::tan(native_pass/2)/reference_aspect;
                close_value(h.scene.weapon_gpu_x[0],native_x,"last-query source/active mutation retires effective first DRIVER projection");
                close_value(h.scene.first_cpu_x[0],native_x,"last-query source/active mutation retires effective first CPU projection");
                close_value(h.scene.first_frustum[0],1/native_x,"last-query native fallback rebuilds full first CPU geometry");
                close_value(h.scene.weapon_gpu_view[0],23,"last-query mutation preserves fresh native driver VIEW before getters");
            }
            prepare(); unsigned seen=0; bool armed_last=false,replaced=false,fresh_consumed=false;
            std::array<float,16> replacement{},incoming_projection{},incoming_view{};
            replacement[0]=3.5f; replacement[5]=4.5f;
            auto calls=[&] { return std::array<unsigned,5>{h.scene.camera_setups,h.scene.projections,h.scene.matrices,
                h.scene.eligibility_driver_calls,h.scene.eligibility_view_calls}; };
            std::array<unsigned,5> before{};
            weapon_callback=[&](uint32_t,uint32_t stage,uint32_t p) {
                if(stage==5) armed_last=true;
                if(stage!=1) return;
                // Admission captured the old generation before its last root query.
                // Observe cancellation BEFORE any getter or fresh camera entry.
                check(replaced && calls()==before,"obsolete camera admission makes no body/builder/driver/VIEW calls");
                check(!std::memcmp(h.scene.driver_projection,incoming_projection.data(),64) &&
                      !std::memcmp(h.scene.driver_view,incoming_view.data(),64),"obsolete admission leaves incoming driver state unchanged");
                check(!std::memcmp(reinterpret_cast<void *>(p+0x200),replacement.data(),64),"obsolete admission preserves full replacement CPU matrix");
                close_value(at<float>(p,0x1f0),55,"obsolete admission preserves replacement view");
                close_value(at<float>(p,0x128),.7,"obsolete admission preserves replacement raw lens");
                // The old camera call has returned. A new entry consumes the
                // current lifetime's authored output; it cannot adopt old optics.
                invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{p,0});
                check(calls()==std::array<unsigned,5>{before[0]+1,before[1],before[2],before[3]+1,before[4]+1},
                      "fresh clean lifetime publishes exactly one original projection and VIEW without rebuild");
                check(!std::memcmp(h.scene.driver_projection,replacement.data(),64) &&
                      !std::memcmp(reinterpret_cast<void *>(p+0x200),replacement.data(),64),"fresh first sink preserves all replacement matrix words including 3.5/4.5");
                close_value(h.scene.driver_view[12],55,"fresh first sink preserves replacement view 55");
                close_value(at<float>(p,0x128),.7,"fresh first sink preserves replacement raw .7 before getters");
                fresh_consumed=true;
            };
            query_callback=[&](uint32_t kind) {
                if(!armed_last || kind!=4 || ++seen!=last) return;
                armed_last=false; replaced=true; const auto p=h.node();
                invoke(h.fn(ctor1_name),{p,11,22});
                (void)h.get(p); // Nested consumer drains the old map entry.
                invoke(h.fn(custom_name),{p,ptr(replacement),0}); at<float>(p,0x1f0)=55;
                before=calls();
                std::memcpy(incoming_projection.data(),h.scene.driver_projection,64);
                std::memcpy(incoming_view.data(),h.scene.driver_view,64);
            };
            draw(); query_callback={}; weapon_callback={}; check(replaced,"last-query reentrant new custom lifetime exercised");
            check(fresh_consumed,"replacement lifetime receives a separate fresh native camera consumption");
            close_value(h.scene.weapon_gpu_x[0],3.5,"new-life custom clean first driver projection remains untouched");
            close_value(h.scene.first_cpu_x[0],3.5,"new-life custom clean first CPU projection remains untouched");
            close_value(h.scene.weapon_gpu_view[0],55,"new-life custom first driver view remains untouched");
            close_value(h.scene.weapon_fov[0],.7,"new-life custom cannot inherit the retired reference or stamp");
        }
        // The original builder itself can reenter. Guard its temporary raw
        // restoration independently of optical-origin invalidation.
        for(unsigned mutation:{0u,1u,2u,3u,4u,5u,6u}) {
            h.reset();
            if(mutation==4) { at<uint8_t>(ptr(h.scene.tweak),0x58)=1; at<float>(ptr(h.scene.tweak),0x5c)=90; }
            bool armed_builder=false,mutated=false;
            std::array<float,16> authored_builder{}; authored_builder[0]=3.5f; authored_builder[5]=4.5f;
            weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) { if(stage==5) armed_builder=true; };
            query_callback=[&](uint32_t kind) {
                if(!armed_builder || kind!=7) return;
                armed_builder=false; mutated=true; const auto p=h.node(); at<float>(p,0x1f0)=23;
                if(mutation==0) {
                    invoke(h.fn(ctor1_name),{p,31,41}); (void)h.projection(p);
                }
                if(mutation==0 || mutation==2) {
                    invoke(h.fn(custom_name),{p,ptr(authored_builder),0}); at<float>(p,0x1f0)=55;
                }
                if(mutation==1) invoke(h.fn(set_name),{p,bits(.95f)});
                if(mutation==3) at<float>(ptr(h.scene.weapon_config),0x11c)=95;
                if(mutation==4) at<float>(ptr(h.scene.tweak),0x5c)=100;
                if(mutation==5) invoke(h.fn(ctor1_name),{h.node(2),51,61});
                if(mutation==6) invoke(h.fn("_ZN6CLevelC1Ev"),{ptr(h.scene.level)});
            };
            draw(); query_callback={}; weapon_callback={}; check(mutated,"actual original builder reentrant callback exercised");
            const double native_pass=70/reference_aspect*pi/180+h.scene.forwarded_sample;
            if(mutation==0 || mutation==2) {
                close_value(h.scene.weapon_gpu_x[0],3.5,"builder callback custom first DRIVER output is preserved");
                close_value(h.scene.first_cpu_x[0],3.5,"builder callback custom first CPU output is preserved");
                close_value(h.scene.weapon_gpu_view[0],55,"builder callback custom VIEW is preserved");
                close_value(h.scene.weapon_fov[0],mutation==0 ? .7 : native_pass,"temporary raw is not restored into new life or left effective in same life");
            } else {
                const double native=mutation==1 ? .95 : native_pass;
                const double output=mutation==5 ? weapon_expected(native) : native;
                close_value(h.scene.weapon_gpu_x[0],1/std::tan(output/2)/reference_aspect,"builder callback changed raw/source stays coherent at first driver sink");
                close_value(h.scene.weapon_fov[0],output,"builder callback raw/source cannot retain a stale effective stamp");
                close_value(h.scene.first_raw[0],native,"foreign or metadata publication never leaves the temporary effective raw field");
            }
        }
        // A reconstruction event at the final ownership query cannot reuse the
        // prior generation even when every compared scalar is restored.
        h.reset(); bool armed=false,hit=false;
        weapon_callback=[&](uint32_t,uint32_t stage,uint32_t) { if(stage==5) armed=true; };
        query_callback=[&](uint32_t kind) {
            if(!armed || hit || kind!=1) return;
            hit=true; const auto p=h.node();
            std::array<unsigned char,0x350> copy{}; std::memcpy(copy.data(),reinterpret_cast<void *>(p),copy.size());
            invoke(h.fn(ctor1_name),{p,1,2}); std::memcpy(reinterpret_cast<void *>(p),copy.data(),copy.size());
        };
        draw(); query_callback={}; weapon_callback={}; check(hit,"query-time same-bit lifecycle callback exercised");
        sample(at<float>(h.node(),0x128)+.2,"query-time same-bit reconstruction invalidates current weapon reference");
        // Every reached regular/skipped combination restores CPU and driver.
        for(unsigned outer:{0u,1u,2u}) for(unsigned inner:{0u,1u,2u}) {
            h.reset(); h.scene.nested_weapon=1; h.scene.weapon_mode[0]=outer; h.scene.weapon_mode[1]=inner;
            draw(); const double parent=outer==0 ? weapon_expected(reference+.2) : reference;
            const double child=inner==0 ? weapon_expected(reference+(outer==0 ? .4 : .2)) : parent;
            const auto x=[&](double v) { return 1/std::tan(v/2)/reference_aspect; };
            close_value(h.scene.weapon_gpu_x[0],x(parent),"parent regular/skipped weapon driver lens");
            close_value(h.scene.weapon_gpu_x[1],x(child),"child regular/skipped inherited native reference");
            close_value(h.scene.after_gpu_x[0],x(parent),"fresh parent driver projection restored before getter");
            close_value(h.scene.after_cpu_x[0],x(parent),"fresh parent CPU geometry restored before getter");
            close_value(h.scene.after_view[0],7,"fresh parent native driver view restored");
            check(h.scene.diff_calls==2,"one actual diff original per nonempty nested pass");
        }
        h.reset(); h.scene.nested_weapon=1; std::array<float,2> retired{};
        weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t p) {
            if(stage==2) retired[depth]=h.get(p);
        };
        draw(); weapon_callback={};
        close_value(retired[1],weapon_expected(reference+.2),"raw restore retires inner reference before cleanup consumers");
        close_value(retired[0],world_angle,"outer raw restore retires weapon reference before cleanup consumers");
        // Calibrate the last query before R1's nested parent publication. A
        // source mutation there must retire owned optics without a new query.
        for(bool tweak:{false,true}) {
            auto prepare=[&] {
                h.reset(); h.scene.nested_weapon=1;
                if(tweak) { at<uint8_t>(ptr(h.scene.tweak),0x58)=1; at<float>(ptr(h.scene.tweak),0x5c)=90; }
            };
            prepare(); bool armed=false; unsigned count=0,last=0;
            weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) { if(depth==1 && stage==2) armed=true; };
            query_callback=[&](uint32_t kind) {
                if(armed && kind==4) ++count;
                if(armed && kind==5) { last=count; armed=false; }
            };
            draw(); query_callback={}; weapon_callback={}; check(last>0,"calibrated last nested publication ownership query");
            prepare(); armed=false; count=0; bool changed=false;
            weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) { if(depth==1 && stage==2) armed=true; };
            query_callback=[&](uint32_t kind) {
                if(!armed || kind!=4 || ++count!=last) return;
                armed=false; changed=true; at<float>(h.node(),0x1f0)=29;
                if(tweak) at<float>(ptr(h.scene.tweak),0x5c)=100;
                else at<float>(ptr(h.scene.weapon_config),0x11c)=95;
            };
            draw(); query_callback={}; weapon_callback={}; check(changed,"last nested publication source mutation exercised");
            const double parent_native=reference+h.scene.forwarded_sample;
            const double x=1/std::tan(parent_native/2)/reference_aspect;
            close_value(h.scene.after_gpu_x[0],x,"last nested source mutation retires stale parent DRIVER projection");
            close_value(h.scene.after_cpu_x[0],x,"last nested source mutation retires stale parent CPU projection");
            close_value(h.scene.after_frustum[0],1/x,"last nested source mutation restores native full parent geometry");
            close_value(h.scene.after_view[0],29,"last nested source mutation preserves fresh parent VIEW");
        }
        h.reset(); h.scene.weapon_mode[0]=1; h.scene.nested_weapon=1;
        at<float>(h.node(2),0x1f0)=31;
        weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
            if(depth==1 && stage==1) {
                at(ptr(h.scene.manager),0xdc)=h.node(2);
                invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{h.node(2),0});
            }
            if(depth==1 && stage==2) at(ptr(h.scene.manager),0xdc)=h.node();
        };
        draw(); weapon_callback={};
        close_value(h.scene.weapon_gpu_view[1],31,"weapon child publishes distinct driver view");
        close_value(h.scene.after_view[0],7,"weapon child return republishes fresh parent driver view");
        close_value(h.scene.after_gpu_x[0],1/std::tan(reference/2)/reference_aspect,"weapon child return restores explicit Native no-lens parent projection");
        h.reset(); h.scene.weapon_mode[0]=1; h.scene.nested_weapon=1;
        h.owner(h.node(1)); h.pseudo(h.node(1),70); h.matrices(h.node(1)); h.owner(h.node());
        at<float>(h.node(1),0x1f0)=19;
        weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
            if(depth==1 && stage==2) { h.owner(h.node(1)); at(ptr(h.scene.manager),0xdc)=h.node(1); }
        };
        draw(); weapon_callback={};
        close_value(h.scene.after_view[0],19,"weapon child return publishes fresh replacement view");
        check(h.scene.driver_node==h.node(1),"weapon return never publishes cached old parent node");
        // Same raw bits under an unknown outer source cannot revive old world origin.
        h.reset(); h.scene.nested_weapon=1; h.scene.diff_sample=0;
        weapon_callback=[&](uint32_t depth,uint32_t stage,uint32_t) {
            if(stage==0) {
                at<float>(ptr(h.scene.weapon_component),0x29c)=depth==0 ? .5f :
                    10.f*720.f/1280.f*std::bit_cast<float>(0x3c8efa35u);
            }
        };
        draw(); weapon_callback={};
        close_value(h.scene.weapon_fov[1],reference,"unknown equal-bit outer reference is a barrier");
        // Foreign execution forwards once without host queries/metadata.
        h.reset(); const auto queries=h.scene.queries;
        std::thread foreign([&] { draw(); }); foreign.join();
        check(h.scene.queries==queries && h.scene.diff_calls==1,"foreign pass forwards original once with zero queries");
        check(bits(h.scene.forwarded_sample)==bits(.2f),"foreign x87 sample bits preserved");
    }
    for(float setting:{60.f,104.f}) {
        NativeWorldFov edge(image,{},&sites,nullptr,setting); edge.bind_render_thread(); Harness h(image); h.reset();
        h.scene.diff_sample=at<float>(ptr(h.scene.weapon_component),0x298);
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
        const double actual_h=2*std::atan(1/h.scene.weapon_gpu_x[0])*180/pi;
        close_value(actual_h,setting,"installed weapon policy range edge gives canonical actual HFOV",.00002);
    }
    auto diff_site=sites;
    auto diff=std::find_if(diff_site.begin(),diff_site.end(),[](const LensCallSite &s) { return s.kind==LensSite::weapon_diff; });
    check(diff!=diff_site.end(),"new observer fixture descriptor exists");
    diff->expected[0]^=1;
    { NativeWorldFov world_only(image,90.f,&diff_site); }
    code.unchanged();
    bool rejected=false;
    try { NativeWorldFov bad(image,{},&diff_site,nullptr,100.f); }
    catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("producer CALL mismatch")!=std::string::npos; }
    check(rejected,"weapon observer byte mismatch rejects while world-only ignores the extra descriptor"); code.unchanged();
    diff->expected[0]^=1;
    const auto address=image.symbol(diff->owner)+diff->offset;
    auto saved=diff->expected,wrong=saved; wrong[1]^=1;
    auto patch=[&](const std::vector<uint8_t> &bytes) {
        DWORD old=0,ignored=0;
        check(VirtualProtect(reinterpret_cast<void *>(address),bytes.size(),PAGE_EXECUTE_READWRITE,&old)!=0,"observer negative patch protection");
        std::memcpy(reinterpret_cast<void *>(address),bytes.data(),bytes.size());
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void *>(address),bytes.size());
        check(VirtualProtect(reinterpret_cast<void *>(address),bytes.size(),old,&ignored)!=0,"observer negative restore protection");
    };
    patch(wrong); diff->expected=wrong;
    try {
        CodeSnapshot mutated(image);
        { NativeWorldFov world_only(image,90.f,&diff_site); } mutated.unchanged();
        rejected=false;
        try { NativeWorldFov bad(image,{},&diff_site,nullptr,100.f); }
        catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("Guest direct call mismatch")!=std::string::npos; }
        check(rejected,"new observer target mismatch rejects after partial hooks/FastNode installation");
        mutated.unchanged();
        rejected=false;
        try { NativeWorldFov bad(image,{},&diff_site,nullptr,{},true); }
        catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("Guest direct call mismatch")!=std::string::npos; }
        check(rejected,"prepared Native partial installation restores every hook and CALL");
        mutated.unchanged();
    } catch(...) { patch(saved); throw; }
    patch(saved);
    code.unchanged();
    std::puts("weapon FOV installed: four configs, source continuity, zero/ratio/overshoot, mixed basis, nesting, driver/view and once-only observer passed");
}
void foreign_known_producer(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    using namespace nova::camera;
    for(unsigned variant=0;variant<7;++variant) {
        const bool foreign=variant!=0;
        NativeWorldFov live(image,{},&sites,nullptr,{},true); live.bind_render_thread(); Harness h(image);
        at<int>(image.symbol("g_ScreenW"))=1280; at<int>(image.symbol("g_ScreenH"))=720;
        at<float>(image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=70;
        at(image.symbol("fov_root_present"))=at(image.symbol("fov_component_present"))=1;
        std::memset(&h.scene,0,sizeof h.scene); invoke(h.fn("fov_fixture_setup"));
        const auto p=h.node();
        auto writer=[&] { h.pseudo(p,70); h.matrices(p); };
        if(variant==6) {
            const auto same_raw=bits(float(70.f*(720.f/1280.f)*.017453292519943295f));
            std::promise<void> reached,release; auto entered=reached.get_future(),resume=release.get_future();
            std::atomic<bool> once=false;
            query_callback=[&](uint32_t kind) { if(kind==7 && !once.exchange(true)) { reached.set_value(); resume.wait(); } };
            std::thread worker(writer);
            const bool ready=entered.wait_for(std::chrono::seconds(2))==std::future_status::ready;
            if(ready) { std::thread other([&] { invoke(h.fn(set_name),{p,same_raw}); }); other.join(); }
            release.set_value(); worker.join(); query_callback={}; check(ready,"foreign overlap reached original builder callback");
        } else if(foreign) { std::thread worker(writer); worker.join(); } else writer();
        if(variant==6) h.matrices(p);
        check(!(at<uint16_t>(p,0x300)&2),"foreign-known fixture starts with CLEAN native standard projection");
        const auto original_raw=at(p,0x128);
        auto unknown=[&,original_raw] { invoke(h.fn(set_name),{p,original_raw}); };
        if(variant==2) { at<int>(image.symbol("g_ScreenW"))=2520; at<int>(image.symbol("g_ScreenH"))=1080; }
        if(variant==3) { std::thread other(unknown); other.join(); }
        if(variant==4) {
            invoke(h.fn(ctor1_name),{p,111,222}); at(p,0x128)=original_raw; h.matrices(p);
        }
        if(variant==5) at<float>(p,0x128)=.9f;
        const auto raw=at(p,0x128);
        check(live.request_options({90.f,100.f}),"foreign-known prepared Native queues atomic Custom pair");
        live.trace_completed_edit({90.f,100.f}); live.service_options(1);
        if(variant>=2) {
            check(live.live_status().committed==NativeWorldFov::Options{90.f,100.f},"foreign counter still commits exact options");
            check(live.live_status().world_application==NativeWorldFov::Application::deferred,"foreign unsafe counter remains deferred");
            check(at(p,0x128)==raw,"foreign counter does not rewrite native raw state");
            std::printf("foreign-known counter=%u PASS conservative deferral, leaf=%s revoke=%s\n",variant,
                live.live_status().world_evidence.leaf,live.live_status().world_evidence.revoked);
            continue;
        }
        const double world=2*std::atan(std::tan(90*pi/360)/reference_aspect);
        const double x=1/std::tan(world/2)/reference_aspect;
        std::printf("foreign-known lane=%s committed=%llu application=%d gate=%d cpu=%.9g expected=%.9g\n",
            foreign ? "foreign" : "render",live.live_status().committed_sequence,
            int(live.live_status().world_application),int(live.live_status().world_gate),at<float>(p,0x200),x);
        close_value(at<float>(p,0x200),x,foreign ? "foreign-known producer first Custom CPU without new producer/getter" :
                    "on-render known producer positive first Custom CPU");
        close_value(at<float>(p,0x13c),1/x,"foreign-known full first native ray");
        close_value(at<float>(p,0x144),100/x,"foreign-known full first native far box",.001);
        close_value(h.scene.driver_projection[0],x,"foreign-known first native DRIVER projection");
        close_value(h.scene.driver_view[12],at<float>(p,0x1f0),"foreign-known first native DRIVER view");
        check(at(p,0x128)==raw,"foreign-known preserves original raw lens");
        const double native=70/reference_aspect*pi/180;
        const auto sample=h.scene.diff_sample;
        const double weapon=weapon_expected(native+sample,100.f),wx=1/std::tan(weapon/2)/reference_aspect;
        bool first=false;
        weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
            if(depth==0 && phase==1) {
                first=true;
                close_value(at<float>(node,0x200),wx,"foreign-known FIRST weapon CPU before getters");
                close_value(at<float>(node,0x13c),1/wx,"foreign-known FIRST weapon near ray");
                close_value(at<float>(node,0x144),100/wx,"foreign-known FIRST weapon far box",.001);
                close_value(h.scene.driver_projection[0],wx,"foreign-known FIRST weapon DRIVER");
            }
        };
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); weapon_callback={};
        check(first,"foreign-known actual weapon pass reached first sink");
        check(live.request_options({}),"foreign-known Reset requests Native"); live.service_options(2);
        close_value(at<float>(p,0x200),1/std::tan(native/2)/reference_aspect,"foreign-known native Reset CPU");
        std::printf("foreign-known %s first CPU/full geometry/DRIVER +weapon/Reset PASS\n",foreign ? "foreign" : "render");
    }
}
void additive_live_probe(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    for(unsigned variant=0;variant<2;++variant) {
        NativeWorldFov live(image,{},&sites,nullptr,{},true); live.bind_render_thread(); Harness h(image); h.reset();
        const auto p=h.node();
        if(variant) {
            at(image.symbol("fov_root_present"))=0; (void)h.get(p);
            at(image.symbol("fov_root_present"))=1; h.pseudo(p,70); h.matrices(p);
        }
        // The live game explicitly executes this Level getter/add/setter path
        // after the known ResetCamera writer and before the next safe service.
        invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p);
        const auto raw=at(p,0x128);
        check(live.request_options({90.f,100.f}),"additive probe queues actual prepared pair");
        live.trace_completed_edit({90.f,100.f}); live.service_options(1);
        const auto status=live.live_status();
        std::printf("additive-probe transient-root=%u committed=%llu leaf=%s establish=%s revoke=%s additive=%s cpu=%.9g\n",
            variant,status.committed_sequence,status.world_evidence.leaf,status.world_evidence.established,
            status.world_evidence.revoked,status.world_evidence.additive,at<float>(p,0x200));
        check(at(p,0x128)==raw,"additive probe preserves native raw");
        if(variant) {
            check(status.world_application==NativeWorldFov::Application::deferred &&
                  std::string(status.world_evidence.additive)=="delta-unseeded",
                  "additive diagnostic identifies missing Level seed without rescuing unknown delta");
            check(std::string(status.world_evidence.delta_reason)=="root-change" &&
                  status.world_evidence.delta_lifetime && status.world_evidence.delta_order>status.world_evidence.delta_lifetime,
                  "first additive diagnostic retains Level seed lifetime and root-clear order");
            for(unsigned frame=2;frame<5;++frame) {
                invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p); live.service_options(frame);
                check(std::string(live.live_status().world_evidence.additive)=="delta-unseeded",
                      "later revoked-lens frames retain first additive cause");
                check(std::string(live.live_status().world_evidence.delta_reason)=="root-change",
                      "later additive revocations cannot hide the original Level seed loss");
            }
            h.pseudo(p,70); invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p); live.service_options(5);
            check(std::string(live.live_status().world_evidence.delta_reason)=="root-change",
                  "fresh known lens cannot erase the upstream Level seed-loss cause");
            continue;
        }
        close_value(at<float>(p,0x200),1,"known writer plus Level additive first Custom CPU");
        close_value(at<float>(p,0x13c),1,"known writer plus Level additive first near ray");
        close_value(at<float>(p,0x144),100,"known writer plus Level additive first far box",.001);
        close_value(h.scene.driver_projection[0],1,"known writer plus Level additive first DRIVER");
    }
}
void startup_custom_additive(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    // Match the real persisted startup pair, not a Native/Native model.
    for(unsigned variant=0;variant<4;++variant) {
        NativeWorldFov live(image,114.5f,&sites,nullptr,102.9f,true); live.bind_render_thread(); Harness h(image); h.reset();
        const auto p=h.node(); live.service_options(1);
        const double initial_x=1/std::tan(114.5*pi/360);
        close_value(at<float>(p,0x200),initial_x,"persisted Custom startup first CPU");
        close_value(h.scene.driver_projection[0],initial_x,"persisted Custom startup first DRIVER");
        if(variant==1) invoke(h.fn("_ZN6CLevel5ResetEv"),{ptr(h.scene.level)});
        if(variant==2) { std::thread builder([&] { h.matrices(p); }); builder.join(); }
        if(variant==3) invoke(h.fn(set_name),{p,at(p,0x128)});
        invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p);
        check(live.request_options({96.5f,102.9f}),"persisted Custom next edit queues");
        live.trace_completed_edit({96.5f,102.9f}); live.service_options(2);
        const auto status=live.live_status();
        std::printf("startup-custom variant=%u leaf=%s additive=%s refD=%s upstream=%s\n",variant,
            status.world_evidence.leaf,status.world_evidence.additive,status.world_evidence.delta_reason,status.world_evidence.upstream);
        if(!variant) {
            const double x=1/std::tan(96.5*pi/360);
            close_value(at<float>(p,0x200),x,"persisted Custom additive first edited CPU");
            close_value(at<float>(p,0x13c),1/x,"persisted Custom additive first edited near ray");
            close_value(at<float>(p,0x144),100/x,"persisted Custom additive first edited far box",.001);
            close_value(h.scene.driver_projection[0],x,"persisted Custom additive first edited DRIVER");
        } else {
            check(status.world_application==NativeWorldFov::Application::deferred &&
                  std::string(status.world_evidence.additive)=="capture-lens" &&
                  std::string(status.world_evidence.delta_reason)=="level-zero-seed",
                  "startup operation reproduces real capture-lens with a valid Level seed");
            const char *cause=variant==1 ? "level" : variant==2 ? "foreign-builder" : "unknown-writer";
            check(std::string(status.world_evidence.upstream)==cause,
                  "first additive retains the actual non-additive predecessor instead of overwriting it");
        }
    }
}
void animation_controls(const nova::ElfImage &image,const std::vector<LensCallSite> &sites,bool omit=false) {
    auto checked=sites;
    if(omit) std::erase_if(checked,[](const LensCallSite &s) { return s.kind==LensSite::vertical_write; });
    CodeSnapshot code(image);
    for(unsigned variant=0;variant<9;++variant) {
        NativeWorldFov live(image,114.5f,&checked,nullptr,102.9f,true); live.bind_render_thread(); Harness h(image); h.reset(variant==8 ? 2520 : 1280,variant==8 ? 1080 : 720);
        const auto p=h.node(); live.service_options(1);
        const float values[2]={variant==7 ? std::numeric_limits<float>::quiet_NaN() : .7f,.9f};
        const float weights[2]={.25f,.75f}; const uint32_t count=variant==1 ? 2 : 1;
        auto animate=[&] { invoke(h.fn("fov_fixture_animation"),{p,ptr(values),ptr(weights),count}); };
        const auto setters=h.scene.setters,queries=h.scene.queries;
        if(variant==6) { std::thread writer(animate); writer.join();
            check(h.scene.queries==queries,"foreign animation adds no root/object query"); }
        else animate();
        check(h.scene.setters==setters+1,"animation performs exactly one original native setter");
        volatile float a=values[0]*weights[0],b=values[1]*weights[1],sum=a+b;
        const float output=count==1 ? values[0] : float(sum);
        check(at(p,0x128)==bits(output),"count1/weighted animation preserves original vertical-radian output bits");
        if(variant==2) invoke(h.fn(set_name),{p,bits(output)});
        if(variant==3) { at(image.symbol("fov_root_present"))=0; (void)h.get(p); at(image.symbol("fov_root_present"))=1; }
        if(variant==4) { invoke(h.fn(ctor1_name),{p,111,222}); at(p,0x128)=bits(output); }
        if(variant==5) at<int>(image.symbol("g_ScreenW"))=0;
        // Match the observed missing-writer -> native Update delta getter ->
        // Level additive -> next render safe service, not a copied status model.
        if(variant!=8) invoke(h.fn("fov_fixture_delta_raw"),{p,bits(.65f),0});
        invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p);
        const auto native_bits=at(p,0x128); const float native=at<float>(p,0x128);
        check(live.request_options({90.2f,104.f}),"animation connected completed pair queues");
        live.trace_completed_edit({90.2f,104.f}); live.service_options(2);
        const auto status=live.live_status();
        std::printf("animation-connected variant=%u omit=%u leaf=%s additive=%s refD=%s upstream=%s committed=%llu cpu=%.9g\n",
            variant,unsigned(omit),status.world_evidence.leaf,status.world_evidence.additive,
            status.world_evidence.delta_reason,status.world_evidence.upstream,status.committed_sequence,at<float>(p,0x200));
        check(at(p,0x128)==native_bits,"animation application preserves native raw bits");
        if(variant>=2 && variant!=8) {
            check(status.committed==NativeWorldFov::Options{90.2f,104.f} &&
                  status.world_application==NativeWorldFov::Application::deferred,
                  "unknown/root/generation/dimensions/foreign/nonfinite animation source remains deferred");
            continue;
        }
        const double zoom=std::tan(native*.5)/std::tan((70/reference_aspect*pi/180)*.5);
        const double aspect=at<float>(p,0x12c);
        const double x=reference_aspect/(std::tan(90.2*pi/360)*zoom*aspect);
        close_value(at<float>(p,0x200),x,"animation first world CPU after real delta/additive");
        close_value(at<float>(p,0x13c),1/x,"animation first world near ray");
        close_value(at<float>(p,0x144),100/x,"animation first world far box",.001);
        close_value(h.scene.driver_projection[0],x,"animation first world DRIVER");
        const auto raw=at(p,0x128); const double wx=1/std::tan(weapon_expected(native+h.scene.diff_sample,104.f)/2)/aspect;
        bool first=false;
        weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
            if(!depth && phase==1) { first=true;
                close_value(at<float>(node,0x200),wx,"animation first weapon CPU before getter");
                close_value(at<float>(node,0x13c),1/wx,"animation first weapon near ray");
                close_value(at<float>(node,0x144),100/wx,"animation first weapon far box",.001);
                close_value(h.scene.driver_projection[0],wx,"animation first weapon DRIVER"); }
        };
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); weapon_callback={};
        check(first && at(p,0x128)==raw,"animation weapon pass restores native raw");
        check(live.request_options({}),"animation Native Reset queues"); live.service_options(3);
        close_value(at<float>(p,0x200),1/std::tan(native/2)/aspect,"animation Reset first native CPU");
        first=false;
        weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
            if(!depth && phase==1) { first=true;
                const double nx=1/std::tan((native+h.scene.diff_sample)/2)/aspect;
                close_value(at<float>(node,0x200),nx,"animation Reset first native weapon CPU");
                close_value(h.scene.driver_projection[0],nx,"animation Reset first native weapon DRIVER"); }
        };
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); weapon_callback={};
        check(first && at(p,0x128)==raw,"animation Reset keeps native raw and weapon sink");
    }
    code.unchanged();
    auto bad=sites; auto it=std::find_if(bad.begin(),bad.end(),[](const LensCallSite &s) { return s.kind==LensSite::vertical_write; });
    check(it!=bad.end(),"animation checked CALL descriptor exists"); it->expected[0]^=1;
    bool rejected=false;
    try { NativeWorldFov invalid(image,90.f,&bad,nullptr,100.f,true); }
    catch(const std::runtime_error &) { rejected=true; }
    check(rejected,"animation CALL byte mismatch rejects before installation"); code.unchanged();
}
void live_controls(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    CodeSnapshot original(image); Harness h(image);
    using Options=NativeWorldFov::Options;
    using Application=NativeWorldFov::Application;
    const double native=70/reference_aspect*pi/180;
    auto world=[](std::optional<float> degrees) {
        return degrees ? 2*std::atan(std::tan(*degrees*pi/360)/reference_aspect) : 70/reference_aspect*pi/180;
    };
    auto direct=[&](double angle,const char *message,uint32_t receiver=0) {
        const auto p=receiver ? receiver : h.node(); const double x=1/std::tan(angle/2)/reference_aspect;
        close_value(at<float>(p,0x200),x,message);
        close_value(at<float>(p,0x13c),1/x,"live first full native plane/ray");
        close_value(at<float>(p,0x144),100/x,"live first full native far box",.001);
        close_value(h.scene.driver_projection[0],x,"live first DRIVER projection before getter/new producer");
        close_value(h.scene.driver_view[12],at<float>(p,0x1f0),"live first DRIVER view before getter/new producer");
    };
    {
        NativeWorldFov live(image,{},&sites,nullptr,{},true); live.bind_render_thread(); h.reset();
        check(!live.live_status().requested_sequence,"prepared initial mailbox revision is zero");
        const auto setups=h.scene.camera_setups,queries=h.scene.queries;
        live.service_options(1);
        check(h.scene.camera_setups==setups && h.scene.queries==queries,"unchanged prepared Native has no service setup/query");
        const auto raw=at(h.node(),0x128);
        h.scene.diff_sample=at<float>(ptr(h.scene.weapon_component),0x298);
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
        check(h.scene.diff_calls==1 && bits(h.scene.forwarded_sample)==bits(h.scene.diff_sample),
              "prepared Native preserves one actual diff and its float bits");
        check(at(h.node(),0x128)==raw,"prepared Native restores raw bits");
        // All requests below use the same clean native raw source, with no new
        // producer and no lazy projection/FOV getter before the first sink.
        uint64_t frame=2;
        for (Options pair:std::initializer_list<Options>{{90.f,{}},{90.f,100.f},{},{{},100.f},
             {60.f,60.f},{120.f,104.f},{95.f,104.f},{95.f,103.f},{95.f,{}},{},{{},100.f},{}}) {
            const auto before=live.live_status();
            check(live.request_options(pair),"valid atomic live pair queued");
            live.service_options(frame++);
            const auto after=live.live_status();
            check(after.committed==pair && after.requested==pair &&
                  after.committed_sequence==after.requested_sequence,"coherent request and commit pair");
            check(after.option_epoch==before.option_epoch+(pair!=before.committed),"only changed pairs advance option epoch");
            direct(world(pair.world),"live first CPU projection before getter/new producer");
            check(at(h.node(),0x128)==raw,"live changes preserve original raw bits");
            const auto diff=h.scene.diff_calls;
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            check(h.scene.diff_calls==diff+1,"live preview never repeats the native difference getter");
            const double pass=native+h.scene.diff_sample;
            const double expected=pair.weapon ? weapon_expected(pass,*pair.weapon) : pass;
            close_value(h.scene.first_cpu_x[0],1/std::tan(expected/2)/reference_aspect,"live first weapon CPU before getter");
            close_value(h.scene.weapon_gpu_x[0],1/std::tan(expected/2)/reference_aspect,"live first weapon DRIVER before getter");
            close_value(h.scene.weapon_gpu_view[0],7,"live first weapon DRIVER view");
            live.service_options(frame++);
            check(live.live_status().application==Application::applied,"observed completed preview differs from commit-only state");
        }
        auto unchanged=live.live_status();
        for (float invalid:{59.f,104.001f,130.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
            check(!live.request_options({90.f,invalid}),"invalid/nonfinite weapon rejects entire pair");
        check(!live.request_options({121.f,100.f}),"invalid world rejects entire pair");
        check(live.request_options(unchanged.requested),"equal pair accepted as no-op");
        live.service_options(frame++);
        check(live.live_status().option_epoch==unchanged.option_epoch &&
              live.live_status().requested_sequence==unchanged.requested_sequence,"invalid/equal requests preserve pair/sequence/epoch");
        std::atomic<bool> requesting{true};
        std::thread requester([&] {
            for(unsigned i=0;i<500;++i) {
                check(live.request_options({80.f+float(i%5),100.f+float(i%5)}),"synthetic host drag queued");
                const auto copy=live.live_status();
                check(copy.requested.world && copy.requested.weapon &&
                      *copy.requested.weapon-100==*copy.requested.world-80,
                      "x86 synchronized pair is never torn");
            }
            live.request_options({92.f,102.f}); live.service_options(frame);
            requesting.store(false);
        });
        uint64_t observed_sequence=unchanged.requested_sequence;
        while(requesting.load()) {
            const auto copy=live.live_status();
            check(copy.requested_sequence>=observed_sequence,"concurrent x86 snapshot sequence is monotonic");
            observed_sequence=copy.requested_sequence;
            if(copy.requested.world && copy.requested.weapon)
                check(*copy.requested.weapon-*copy.requested.world==20 || copy.requested==Options{92.f,102.f},
                      "concurrent x86 pair snapshot cannot tear");
        }
        requester.join();
        check(live.live_status().committed==unchanged.committed,"foreign host lane cannot service");
        live.service_options(frame++); direct(world(92.f),"coalesced latest request first CPU");
        check(live.live_status().committed==Options{92.f,102.f},"coalesced latest valid request commits atomically");
        bool query=false;
        query_callback=[&](uint32_t kind) {
            if(query || kind!=1) return; query=true;
            // This would deadlock if service held the mailbox across a query.
            const auto before=live.live_status().committed_sequence;
            live.request_options({96.f,104.f}); live.service_options(frame);
            check(live.live_status().committed_sequence==before,"query reentry cannot commit within guest work");
        };
        live.request_options({94.f,103.f}); live.service_options(frame++); query_callback={};
        check(query && live.live_status().committed==Options{94.f,103.f} &&
              live.live_status().requested==Options{96.f,104.f},"query enqueue retained after unlocked service copy");
        live.service_options(frame++); direct(world(96.f),"query queued pair applied at next outer safe point");
        for (unsigned stage:{0u,4u,5u,1u,2u}) {
            bool hit=false; const auto epoch=live.live_status().option_epoch;
            weapon_callback=[&](uint32_t,uint32_t phase,uint32_t) {
                if(hit || phase!=stage) return; hit=true;
                live.request_options({90.f+float(stage),104.f}); live.service_options(frame);
                check(live.live_status().option_epoch==epoch,"weapon scope queues without in-pass commit");
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); weapon_callback={};
            check(hit,"each installed weapon branch callback reached"); live.service_options(frame++);
        }
        {
            NativeWorldFov::JeepUpdateScope scope(ptr(h.scene.component),16);
            const auto epoch=live.live_status().option_epoch;
            check(live.request_options({98.f,102.f}),"valid Jeep scoped pair queues"); live.service_options(frame);
            check(live.live_status().option_epoch==epoch,"Jeep scope defers immediate commit");
        }
        live.service_options(frame++); direct(world(98.f),"Jeep queued pair applies outside scope");
        bool builder=false;
        query_callback=[&](uint32_t kind) {
            if(kind!=7 || builder) return; builder=true;
            const auto epoch=live.live_status().option_epoch;
            check(live.request_options({99.f,103.f}),"valid matrix scoped pair queues"); live.service_options(frame);
            check(live.live_status().option_epoch==epoch,"matrix/temp-raw guest callback defers commit");
        };
        h.pseudo(h.node(),70); h.matrices(h.node()); query_callback={}; check(builder,"native builder callback reached");
        live.service_options(frame++); direct(world(99.f),"matrix queued pair applies outside scope");
        // Two proved, clean receivers can carry different option epochs while
        // sharing one unchanged local root. Reacquire only the current receiver.
        h.reset(); const auto a=h.node(),b=h.node(1);
        h.owner(b); at(ptr(h.scene.manager),0xdc)=b; h.pseudo(b,70); h.matrices(b);
        at<float>(b,0x1f0)=19;
        h.owner(a); at(ptr(h.scene.manager),0xdc)=a;
        live.request_options({110.f,100.f}); live.service_options(frame++);
        direct(world(110.f),"current A first epoch CPU");
        h.owner(b); at(ptr(h.scene.manager),0xdc)=b;
        const auto same_sequence=live.live_status().committed_sequence;
        live.service_options(frame++);
        direct(world(110.f),"same-root clean B first CPU uses current option epoch",b);
        check(live.live_status().committed_sequence==same_sequence,"receiver refresh is not an option transaction");
        invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{b,0});
        close_value(h.scene.driver_projection[0],at<float>(b,0x200),"first direct B native setup cannot publish old epoch");
        h.owner(a); at(ptr(h.scene.manager),0xdc)=a;
        live.request_options({}); live.service_options(frame++);
        h.owner(b); at(ptr(h.scene.manager),0xdc)=b; live.service_options(frame++);
        direct(native,"same-root inactive B Native reset retires old effective stamp",b);
        const auto no_extra_setup=h.scene.camera_setups;
        live.service_options(frame++);
        check(h.scene.camera_setups==no_extra_setup,"unchanged prepared Native adds no blanket native setup");
        // A changed option is not a native provenance reset. Finalize a pending
        // Level delta after the transaction and keep its original additive math.
        h.reset(2520,1080); const auto metadata=h.node();
        invoke(h.fn("fov_fixture_delta"),{metadata,bits(50.f),0});
        live.request_options({105.f,{}}); live.service_options(frame++);
        invoke(h.fn("fov_fixture_render"),{metadata});
        const auto level_lens=ReferenceLens{at<float>(metadata,0x128),50/reference_aspect*pi/180};
        h.geometry(metadata,*world_vertical_fov(level_lens,70,105.f));
        // Observe the saved Jeep lens in prepared Native, then enable Custom
        // without another producer before the ordinary native restore helper.
        live.request_options({}); live.service_options(frame++); h.reset(2520,1080);
        const auto component=ptr(h.scene.component); at<uint8_t>(component,0x1ac)=0;
        invoke(h.fn(enable_name),{component,1}); const auto jeep=h.node(1);
        at(ptr(h.scene.manager),0xdc)=jeep;
        live.request_options({108.f,100.f}); live.service_options(frame++);
        invoke(h.fn(helper_name),{component,16,0});
        h.geometry(jeep,world(108.f));
        at<uint8_t>(component,0x1ac)=1; at<float>(ptr(h.scene.config),0x6c)=60;
        const auto before_jeep=*pseudo_horizontal_lens(at<float>(jeep,0x128),2520,1080);
        invoke(h.fn(helper_name),{component,16,0});
        const auto after_jeep=*affine_reference_lens(before_jeep,before_jeep.native_vertical,at<float>(jeep,0x128),.016f);
        check(live.request_options({112.f,104.f}),"valid Jeep changed pair queues"); live.service_options(frame++);
        h.geometry(jeep,*world_vertical_fov(after_jeep,70,112.f));
        h.reset(); live.request_options({90.f,{}}); live.service_options(frame++);
        pass_projection_contract(h); pass_lifetime_guards(h); jeep_and_saved(h); foreign_geometry(h); delta_and_lifetime(h);
        h.reset(); check(live.request_options({99.f,102.f}),"valid pre-defer pair queues"); live.service_options(frame++);
        at(image.symbol("fov_root_present"))=0;
        check(live.request_options({100.f,103.f}),"valid deferred pair queues"); live.service_options(frame++);
        check(live.live_status().world_application==Application::deferred,"missing camera commit is honestly deferred");
        at(image.symbol("fov_root_present"))=1; live.service_options(frame++);
        check(live.live_status().world_application==Application::deferred,"restored root does not invent native provenance");
        h.pseudo(h.node(),70); h.matrices(h.node()); live.service_options(frame++);
        direct(world(100.f),"new proved producer resolves deferred current camera");
        live.request_options({}); live.service_options(frame++); direct(native,"Native reset retires exact owned output");
        // A clean authored projection never becomes an invented native origin.
        h.reset(); std::array<float,16> authored{}; authored[0]=3.5f; authored[5]=4.5f;
        invoke(h.fn(custom_name),{h.node(),ptr(authored),0});
        live.request_options({101.f,100.f}); live.service_options(frame++);
        close_value(at<float>(h.node(),0x200),3.5,"late enable preserves unknown clean custom CPU matrix");
        check(live.live_status().world_application==Application::deferred,"unknown clean custom is honestly deferred");
        h.pseudo(h.node(),70); h.matrices(h.node()); live.service_options(frame++);
        direct(world(101.f),"actual native source/builder resolves clean custom barrier");
        // Foreign mutation revokes the current native origin and cannot retain
        // ownership of the adjusted output under a new option epoch.
        const auto foreign_raw=at(h.node(),0x128);
        std::thread foreign([&]{invoke(h.fn(set_name),{h.node(),foreign_raw});}); foreign.join();
        live.request_options({102.f,101.f}); live.service_options(frame++);
        direct(native,"foreign same-bit mutation retires old custom CPU output");
        check(live.live_status().world_application==Application::deferred,"foreign same-bit origin loss stays deferred");
        h.pseudo(h.node(),70); h.matrices(h.node()); live.request_options({}); live.service_options(frame++);
        // The initial no-change decision is provisional until the final pure
        // source fence. A query can change DEFAULT_FOV without a lifecycle event.
        h.reset(); live.request_options({103.f,100.f}); live.service_options(frame++);
        unsigned final_queries=0;
        query_callback=[&](uint32_t kind) { if(kind==4) ++final_queries; };
        live.service_options(frame++); query_callback={};
        check(final_queries>0,"calibrated final unchanged-pair service query");
        unsigned mutation_queries=0; bool source_changed=false;
        const auto before_retirement=h.scene.camera_setups; at<float>(h.node(),0x1f0)=33;
        query_callback=[&](uint32_t kind) {
            if(kind==4 && ++mutation_queries==final_queries) {
                at<float>(image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=75; source_changed=true;
            }
        };
        live.service_options(frame++); query_callback={};
        check(source_changed,"final-fence no-event source mutation exercised");
        direct(native,"final-fence no-event retirement first CPU projection");
        check(h.scene.camera_setups==before_retirement+1,"actual final-fence retirement republishes native setup once");
        check(live.live_status().world_application==Application::deferred,"final-fence source mutation is not an applied custom preview");
        at<float>(image.symbol("_ZN11CGameCamera11DEFAULT_FOVE"))=70;
        // A final ownership query may synchronously replace a same-address
        // lifetime. Publication must stop with no temporary raw leak.
        h.reset(); unsigned checks=0,last=0;
        query_callback=[&](uint32_t kind) { if(kind==4) ++checks; if(kind==5) last=checks; };
        live.request_options({104.f,100.f}); live.service_options(frame++); query_callback={};
        check(last>0,"calibrated actual last safe-service ownership query");
        h.reset(); checks=0; bool replaced=false;
        const auto setups_before=h.scene.camera_setups;
        query_callback=[&](uint32_t kind) {
            if(replaced || kind!=4 || ++checks!=last) return; replaced=true;
            invoke(h.fn(ctor1_name),{h.node(),73,91});
            std::array<float,16> replacement{}; replacement[0]=3.5f; replacement[5]=4.5f;
            invoke(h.fn(custom_name),{h.node(),ptr(replacement),0}); at<float>(h.node(),0x1f0)=55;
        };
        live.request_options({105.f,101.f}); live.service_options(frame++); query_callback={};
        check(replaced && h.scene.camera_setups==setups_before,"final safe-service query lifetime replacement forbids stale driver publication");
        check(live.live_status().world_application==Application::deferred,"final-query lifetime replacement remains deferred");
        close_value(at<float>(h.node(),0x200),3.5,"final-query new custom lifetime CPU matrix preserved");
        close_value(at<float>(h.node(),0x128),.7,"final-query new custom lifetime raw preserved");
        h.pseudo(h.node(),70); h.matrices(h.node()); live.request_options({}); live.service_options(frame++);
    }
    original.unchanged(); // Includes the diff CALL after values returned Native.
    {
        NativeWorldFov legacy(image,{},&sites);
        check(!legacy.request_options({90.f,100.f}),"legacy adapter cannot acquire live installation later");
    }
    auto diff=std::find_if(sites.begin(),sites.end(),[](const LensCallSite &s){return s.kind==LensSite::weapon_diff;});
    check(diff!=sites.end(),"prepared diff descriptor exists");
    auto bad=sites; auto bad_diff=std::find_if(bad.begin(),bad.end(),[](const LensCallSite &s){return s.kind==LensSite::weapon_diff;});
    bad_diff->expected.back()^=1;
    bool rejected=false; try { NativeWorldFov broken(image,{},&bad,nullptr,{},true); }
    catch(const std::runtime_error &) { rejected=true; }
    check(rejected,"prepared Native rejects wrong actual diff CALL"); original.unchanged();
    std::puts("live FOV installed: prepared Native, first CPU/geometry/DRIVER/view, atomic options, safe scopes, provenance/deferred and restoration passed");
}
void render_extent_controls(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    CodeSnapshot original(image);
    {
        NativeWorldFov live(image,95.f,&sites,nullptr,100.f,true); live.bind_render_thread();
        Harness h(image); h.reset(); live.service_options(1);
        const auto p=h.node(),raw=at(p,0x128);
        const double world=2*std::atan(std::tan(95*pi/360)/reference_aspect);
        uint64_t frame=2;
        int ow=1280,oh=720;
        for(auto [nw,nh]:std::array<std::pair<int,int>,7>{{{640,360},{1024,576},{1600,900},{1920,1080},
                                                       {2560,1440},{3840,2160},{1280,720}}}) {
            check(live.prepare_render_extent(ow,oh,nw,nh),"current known lens admits an exact same-aspect resize");
            at<int>(image.symbol("g_ScreenW"))=nw; at<int>(image.symbol("g_ScreenH"))=nh;
            check(live.verify_render_extent(true),"new dimensions retain exact native raw/reference/lifetime proof");
            live.finish_render_extent(true); live.service_options(frame++);
            const double x=1/std::tan(world/2)/reference_aspect;
            close_value(at<float>(p,0x200),x,"same-scene resize preserves first world CPU projection");
            close_value(h.scene.driver_projection[0],x,"same-scene resize preserves first world DRIVER projection");
            check(at(p,0x128)==raw,"same-aspect resize never replaces native raw optics");
            const double pass=70/reference_aspect*pi/180+h.scene.diff_sample;
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            close_value(h.scene.weapon_gpu_x[0],1/std::tan(weapon_expected(pass,100.f)/2)/reference_aspect,
                        "same-scene resize preserves weapon DRIVER projection");
            ow=nw; oh=nh;
        }
        check(!live.prepare_render_extent(ow,oh,1280,800),"different aspect cannot reuse same-aspect lens evidence");
        check(live.prepare_render_extent(ow,oh,1600,900),"rollback lens transaction starts");
        at<int>(image.symbol("g_ScreenW"))=1600; at<int>(image.symbol("g_ScreenH"))=900;
        check(live.verify_render_extent(true),"provisional same-aspect pair is checked");
        at<int>(image.symbol("g_ScreenW"))=ow; at<int>(image.symbol("g_ScreenH"))=oh;
        check(live.verify_render_extent(false),"restored old dimensions retain old lens evidence");
        live.finish_render_extent(false);
        check(live.prepare_render_extent(ow,oh,1600,900),"foreign raw negative starts from a known lens");
        at(p,0x128)=raw^1;
        check(!live.verify_render_extent(false),"changed native raw cannot retain resize lens authority");
        at(p,0x128)=raw; live.finish_render_extent(false);
        check(live.prepare_render_extent(ow,oh,1600,900),"active-child replacement negative starts from a known lens");
        h.owner(h.node(1));
        check(!live.verify_render_extent(false),"same Root with a different active camera child rejects old-node authority");
        h.owner(p); live.finish_render_extent(false);
    }
    original.unchanged();
    std::puts("render extent FOV:720/900/1080 same-scene CPU/DRIVER/weapon, raw/aspect negatives and rollback passed");
}
void synthetic(const nova::ElfImage &image,bool fail) {
    auto sites=fixture_sites(image); CodeSnapshot original(image);
    native_mode(image);
    {
        NativeWorldFov camera(image,90.f,&sites,fail ? fail_event : nullptr); Harness h(image);
        if(fail) {
            audit_guard=true;
            std::fputs("event-failure-child-started\n",stderr); std::fflush(stderr);
            invoke(h.fn(ctor1_name),{h.node(),1,2});
            std::fputs("INVALID-CONTINUED-AFTER-EVENT-FAILURE\n",stderr); throw std::runtime_error("event failure continued");
        }
        camera.bind_render_thread(); world_and_aspect(h); isolation_and_custom(h);
        weapon_pass(h); pass_projection_contract(h); pass_lifetime_guards(h);
        jeep_and_saved(h); foreign_geometry(h); delta_and_lifetime(h);
    }
    original.unchanged();
    weapon_optics(image,sites);
    {
        const auto slot=image.symbol("_ZTV22CCustomCameraSceneNode")+16+8;
        const auto saved=at(slot); at(slot)=0;
        bool rejected=false;
        try { NativeWorldFov mismatch(image,90.f,&sites); }
        catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("camera setup contract mismatch")!=std::string::npos; }
        at(slot)=saved;
        check(rejected,"native setup vtable mismatch rejects before any entry/CALL installation");
        original.unchanged();
    }
    for(float setting:{60.f,120.f}) {
        NativeWorldFov edge(image,setting,&sites); edge.bind_render_thread(); Harness h(image); h.reset();
        h.geometry(h.node(),2*std::atan(std::tan(setting*pi/360)/reference_aspect));
    }
    original.unchanged();
    for(auto [name,expected]:std::initializer_list<std::pair<const char *,uint32_t>>{
        {"fov_eax_load_return",0x12345678},{"fov_edx_load_return",0xabcdef01}}) {
        const bool eax=expected==0x12345678;
        auto probe=[&](std::initializer_list<uint8_t> bytes) {
          nova::abi::GuestHook hook(image,name,bytes,reinterpret_cast<uint32_t>(&load_forward));
          load_original=hook.original();
          check(invoke(nova::abi::require_function(image,name),{0x12345678,0xabcdef01})==expected,
                "copied exact MOV register load executes against aligned trampoline frame");
        };
        if(eax) probe({0x55,0x89,0xe5,0x8b,0x45,0x08}); else probe({0x55,0x89,0xe5,0x8b,0x55,0x0c});
        original.unchanged();
    }
    for(auto [name,bytes]:std::initializer_list<std::pair<const char *,std::initializer_list<uint8_t>>>{
        {"fov_bad_eax_offset",{0x55,0x89,0xe5,0x8b,0x45,0x0c}},
        {"fov_bad_edx_offset",{0x55,0x89,0xe5,0x8b,0x55,0x10}}}) {
        bool rejected=false;
        try { nova::abi::GuestHook bad(image,name,bytes,ptr(fail_event)); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected,"neighbor MOV addressing encodings remain rejected");
    }
    auto bad=sites; bad[0].expected.back()^=1;
    bool rejected=false; try { NativeWorldFov mismatch(image,90.f,&bad); }
    catch(const std::runtime_error &) { rejected=true; }
    check(rejected,"wrong native producer CALL rejects before installation"); original.unchanged();
    std::puts("world FOV installed adapter: geometry, raw/reference writers, native weapons, lifecycle and strict ABI passed");
}
void real(const char *path,bool weapon=false,bool prepared=false) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); CodeSnapshot original(image);
    nova::input::Controls controls{nova::input::ControlsConfig{}};
    {
        nova::input::NativeControls previous(image,controls);
        if (prepared) {
            NativeWorldFov live(image,{},nullptr,nullptr,{},true);
            nova::movement_wobble::RequestedStrength strength;
            nova::movement_wobble::NativeCameraBob bob(image,strength);
        }
        else if (weapon) {
            for (auto world:{std::optional<float>{},std::optional(90.f)}) {
                NativeWorldFov fov(image,world,nullptr,nullptr,100.f);
            }
        } else { NativeWorldFov fov(image,90.f); }
    }
    original.unchanged();
    std::puts(prepared ? "real prepared Native complete checked install/restore with accepted controls; zero guest initializers/gameplay" :
              weapon ? "real weapon-only/both FOV complete checked install/restore with accepted controls; zero guest initializers/gameplay" :
                      "real world FOV complete checked install/restore with accepted controls; zero guest initializers/gameplay");
}
// Insert inside the existing native_world_fov_tests.cpp anonymous namespace.
// Call after animation_controls() in the existing --animation positive branch.
// This covers optical samples, not native ADS permission/clip completion.
void animation_trajectory(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    std::array<double,25> native_world{},native_weapon{};
    const double base=70/reference_aspect*pi/180;
    const double world_scale=std::tan(90*pi/360)/(reference_aspect*std::tan(base/2));
    const double weapon_scale=std::tan(100*pi/360)/(reference_aspect*std::tan((90/reference_aspect*pi/180)/2));
    uint32_t native_setters=0;
    for(unsigned custom=0;custom<2;++custom) {
        NativeWorldFov live(image,custom ? std::optional(90.f) : std::nullopt,&sites,nullptr,
                            custom ? std::optional(100.f) : std::nullopt,true);
        live.bind_render_thread(); Harness h(image); h.reset(); const auto p=h.node();
        double previous_world=0,previous_weapon=0;
        for(unsigned frame=0;frame<25;++frame) {
            // Preserve production ordering: host safe service precedes guest
            // animation/Update/Render. Options remain constant for the series.
            live.service_options(frame+1);
            const float progress=float(frame<=12 ? frame : 24-frame)/12;
            const float values[2]={.75f,.55f},weights[2]={1-progress,progress};
            const auto before=h.scene.setters;
            invoke(h.fn("fov_fixture_animation"),{p,ptr(values),ptr(weights),2});
            check(h.scene.setters==before+1,"trajectory preserves one native animation setter per sample");
            const float animation=at<float>(p,0x128);
            invoke(h.fn("fov_fixture_delta_raw"),{p,bits(animation-.08f*progress),0});
            invoke(h.fn("fov_fixture_render"),{p}); h.matrices(p);
            const auto raw=at(p,0x128);
            invoke(h.fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{p,0});
            const double world=h.scene.driver_projection[0];
            close_value(at<float>(p,0x200),world,"trajectory first world CPU and driver agree");
            close_value(at<float>(p,0x13c),1/world,"trajectory first world near ray");
            close_value(at<float>(p,0x144),100/world,"trajectory first world far box",.001);
            h.scene.diff_sample=.2f-.08f*progress;
            bool first=false; double weapon=0;
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
                if(!depth && phase==1) { first=true; weapon=h.scene.driver_projection[0];
                    close_value(at<float>(node,0x200),weapon,"trajectory first weapon CPU before getter");
                    close_value(at<float>(node,0x13c),1/weapon,"trajectory first weapon near ray");
                    close_value(at<float>(node,0x144),100/weapon,"trajectory first weapon far box",.001); }
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)}); weapon_callback={};
            check(first && at(p,0x128)==raw,"trajectory restores exact native raw after weapon pass");
            if(!custom) { native_world[frame]=world; native_weapon[frame]=weapon; }
            else {
                close_value(world,native_world[frame]/world_scale,"each forward/reverse Custom world tangent ratio preserves native sample");
                close_value(weapon,native_weapon[frame]/weapon_scale,"each forward/reverse Custom weapon tangent ratio preserves native sample");
            }
            if(frame) {
                check(frame<=12 ? world>previous_world : world<previous_world,
                      "world zoom direction remains monotone through entry and animated exit samples");
                check(frame<=12 ? weapon>previous_weapon : weapon<previous_weapon,
                      "weapon zoom direction remains monotone through entry and animated exit samples");
            }
            previous_world=world; previous_weapon=weapon;
        }
        if(!custom) native_setters=h.scene.setters;
        else check(h.scene.setters==native_setters,"Custom optics introduce no animation/raw setter calls");
    }
}

#include "fixtures/viewmodel_eligibility_connected.inc"
#include "fixtures/camera_bob_connected.inc"
#include "fixtures/rigid_draw_activation.inc"
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && (std::string(argv[1])=="--real" || std::string(argv[1])=="--real-weapon" || std::string(argv[1])=="--real-live")) {
            if(!std::filesystem::exists(argv[2])) return 77;
            real(argv[2],std::string(argv[1])=="--real-weapon",std::string(argv[1])=="--real-live"); return 0;
        }
        check(argc==2 || (argc==3 && (std::string(argv[2])=="--fail-event" || std::string(argv[2])=="--live" || std::string(argv[2])=="--foreign-known" || std::string(argv[2])=="--additive-probe" || std::string(argv[2])=="--animation" || std::string(argv[2])=="--animation-omit" || std::string(argv[2])=="--render-extent" || std::string(argv[2])=="--eligibility" || std::string(argv[2])=="--eligibility-omit" || std::string(argv[2])=="--rigid-draw")),"fixture path required");
        nova::ImportRegistry imports; nova::bionic::register_boot(imports);
        imports.function("fov_audit",reinterpret_cast<void *>(&fixture_audit));
        imports.function("fov_reset_callback",reinterpret_cast<void *>(&fixture_reset_callback));
        imports.function("fov_weapon_callback",reinterpret_cast<void *>(&fixture_weapon_callback));
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1],imports);
        if(argc==3 && (std::string(argv[2])=="--eligibility" || std::string(argv[2])=="--eligibility-omit")) {
            auto sites=fixture_sites(image);
            if(std::string(argv[2])=="--eligibility-omit")
                std::erase_if(sites,[](const LensCallSite &s) { return s.kind==LensSite::camera_projection; });
            viewmodel_eligibility_connected<O09NativeBinding>(image,sites);
            animation_trajectory(image,sites);
            camera_bob_connected(image,sites);
        }
        else if(argc==3 && std::string(argv[2])=="--rigid-draw") rigid_draw_activation(image,fixture_sites(image));
        else if(argc==3 && std::string(argv[2])=="--render-extent") render_extent_controls(image,fixture_sites(image));
        else if(argc==3 && (std::string(argv[2])=="--animation" || std::string(argv[2])=="--animation-omit")) {
            const auto sites=fixture_sites(image); animation_controls(image,sites,std::string(argv[2])=="--animation-omit");
            if(std::string(argv[2])=="--animation") animation_trajectory(image,sites);
        }
        else if(argc==3 && std::string(argv[2])=="--additive-probe") { additive_live_probe(image,fixture_sites(image)); startup_custom_additive(image,fixture_sites(image)); }
        else if(argc==3 && std::string(argv[2])=="--foreign-known") foreign_known_producer(image,fixture_sites(image));
        else if(argc==3 && std::string(argv[2])=="--live") live_controls(image,fixture_sites(image));
        else synthetic(image,argc==3);
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"native world FOV failure: %s\n",e.what()); return 1; }
}
