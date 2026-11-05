#include "menu/native_menu_observer.h"
#include "ui/menu_surfaces.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nova::menu {
static_assert(sizeof(void *)==4,"Guest observer requires the x86 host ABI");
namespace {
thread_local unsigned load_depth=0;
[[noreturn]] void scaling_callback_failure(const char* boundary) noexcept {
    std::fprintf(stderr,"initial-menu-scaling FAIL boundary=%s\n",boundary);
    std::fflush(stderr); TerminateProcess(GetCurrentProcess(),86); std::abort();
}
constexpr const char *load_name="_ZN12CMenuManager4LoadEP13CMemoryStream";
constexpr const char *sprite_name="_ZN14CSpriteManager10LoadSpriteEPKcbbbi";
constexpr const char *select_name="_ZN14CSpriteManager17GetSpriteFilenameEPKcRSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS4_6memory13E_MEMORY_HINTE0EEEERfRiSD_b";
constexpr const char *get_name="_ZN14CSpriteManager9GetSpriteEPKc";
constexpr const char *rtt_name="_ZN14CRTTMenuObjectC1Eii";
constexpr const char *life_names[]={"_ZN12CMenuManagerC1Ev",
    "_ZN12CMenuManagerD1Ev","_ZN12CMenuManager6UnloadEv"};
constexpr uint32_t life_va[]={0x5337a0,0x53a920,0x53a160};
constexpr const char *fixture_life[]={"obs_c1","obs_d1","obs_unload"};
constexpr const char *fixture_calls[]={"obs_resource","obs_select","obs_width","obs_height","obs_rtt0","obs_rtt1"};
constexpr unsigned sprite_call=0,select_call=1,width_call=2,height_call=3,rtt0_call=4,rtt1_call=5;

struct Guard {
    SRWLOCK &lock;
    explicit Guard(SRWLOCK &value):lock(value) { AcquireSRWLockExclusive(&lock); }
    ~Guard() { ReleaseSRWLockExclusive(&lock); }
};
// The bounded SEH leaf contains no C++ objects needing unwinding. Readability is
// NOT an ownership proof; callers must first hold the generation/owning-thread fence.
bool copy_bytes(uint32_t address,void *out,size_t size) noexcept {
    if(!address || size>std::numeric_limits<uint32_t>::max()-address) return false;
    const auto end=uint64_t(address)+size;
    for(uint64_t cursor=address;cursor<end;) {
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void *>(static_cast<uint32_t>(cursor)),&region,sizeof region) ||
           region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD protection=region.Protect&0xff;
        if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
        const auto next=uint64_t(reinterpret_cast<uint32_t>(region.BaseAddress))+region.RegionSize;
        if(next<=cursor) return false;
        cursor=next;
    }
    __try { std::memcpy(out,reinterpret_cast<const void *>(address),size); return true; }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
bool copy_name(uint32_t address,char (&out)[NativeMenuObserver::name_limit]) noexcept {
    if(!address || address>UINT32_MAX-NativeMenuObserver::name_limit) return false;
    for(unsigned i=0;i<NativeMenuObserver::name_limit;++i) {
        if(!copy_bytes(address+i,&out[i],1)) return false;
        if(!out[i]) return true;
    }
    out[NativeMenuObserver::name_limit-1]=0; return false;
}
// Match the repository's BCrypt identity pattern, but authenticate ONLY the
// retained bytes that actually produced this ElfImage. No path reopen or profile call.
void authenticate(const ElfImage &image) {
    constexpr std::array<uint8_t,32> accepted{
        0xa6,0x72,0xcf,0x14,0xe2,0x7f,0x1c,0x18,0x8e,0xdd,0x97,0xd8,0x5d,0x22,0x0b,0xec,
        0xd9,0x9e,0x64,0x1a,0xd9,0xcb,0x55,0x29,0x87,0x51,0x2d,0x5e,0xf9,0x0e,0x10,0x2e};
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
        std::vector<uint8_t> object;
        ~Hash() { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    } h;
    ULONG size=0,got=0;
    if(BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
       BCryptGetProperty(h.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&got,0)<0)
        throw std::runtime_error("menu observer SHA256 provider failed");
    if(!size || size>65536) throw std::runtime_error("menu observer SHA256 allocation bound");
    h.object.resize(size);
    std::array<uint8_t,32> digest{};
    const auto bytes=image.source_bytes();
    // ElfImage caps the retained source at 512 MiB, representable by ULONG.
    if(BCryptCreateHash(h.algorithm,&h.hash,h.object.data(),size,nullptr,0,0)<0 ||
       BCryptHashData(h.hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)<0 ||
       BCryptFinishHash(h.hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0 || digest!=accepted)
        throw std::runtime_error("menu observer loaded image identity mismatch");
}
}

struct NativeMenuObserver::Impl {
    // Single installation, published only when ALL authenticated seams are ready.
    // Installation/removal require caller-enforced guest quiescence, like GuestHook.
    static std::atomic<Impl *> active;
    static SRWLOCK installation;
    struct Scope { Impl *observer; uint32_t owner,generation,invocation,phase; };
    struct ResourceScope { Scope *load; uint32_t receiver,name; };
    static thread_local Scope *current;
    static thread_local ResourceScope *resource;
    mutable SRWLOCK lock=SRWLOCK_INIT;
    Snapshot state{};
    RttBackingCapture backing;
    std::unique_ptr<InitialTargetScaling> scaling;
#ifdef NOVA_MENU_OBSERVER_FIXTURE
    InitialTargetScaling* fixture_lifecycle_scaling=nullptr;
#endif
    std::array<std::unique_ptr<abi::GuestHook>,6> entries;
    std::array<std::unique_ptr<abi::GuestCall>,6> calls;
    uint32_t width_address=0,flag_address=0,phase_offset=0x114;
    uint32_t owner=0,generation=0,thread=0,serial=0,last_phase=0;
    uint32_t sprite_pointer=0,sprite_manager=0;
    bool constructed=false,started=false,reported=false,resource_seen=false,selected_seen=false;
    bool width_seen=false,height_seen=false;
    unsigned rtts=0,capacity=record_limit;
    char input[name_limit]{};

    void fail(const char *why) noexcept {
        if(!state.terminal) { state.terminal=true; state.complete=false; state.reason=why; backing.reject("cpu_join_inconclusive"); }
    }
    bool local(const Scope *s) const noexcept {
        return !state.terminal && started && constructed && thread==GetCurrentThreadId() &&
            s && current==s && s->observer==this && s->owner==owner && s->generation==generation;
    }
    bool add(Record record) noexcept {
        if(state.count==capacity) { fail("record_overflow"); return false; }
        state.records[state.count++]=record; return true;
    }
    Record record(Record::Kind kind,const Scope &s) const noexcept {
        Record out; out.kind=kind; out.manager=s.owner; out.generation=s.generation; out.invocation=s.invocation; return out;
    }
    bool globals(Record &r) noexcept {
        uint8_t flag=0;
        if(!copy_bytes(width_address,&r.native_width,4) || !copy_bytes(flag_address,&flag,1)) {
            fail("native_width_or_flag_unreadable"); return false;
        }
        r.flag=flag; return true;
    }
    static Impl &self() noexcept { return *active.load(std::memory_order_acquire); }
    static uint64_t forward(uint32_t original,const uint32_t *words,size_t count) {
        return abi::call_guest_u64(original,words,count); // sole guest entry in each wrapper
    }
    static uint64_t life(unsigned index,uint32_t receiver) {
        auto &p=self(); uint32_t token=0;
        uint32_t scaling_generation=0;
        { Guard guard(p.lock); if(p.owner==receiver) scaling_generation=p.generation; }
        auto* lifecycle_scaling=p.scaling.get();
#ifdef NOVA_MENU_OBSERVER_FIXTURE
        if(p.fixture_lifecycle_scaling) lifecycle_scaling=p.fixture_lifecycle_scaling;
#endif
        if(lifecycle_scaling && scaling_generation) lifecycle_scaling->lifecycle(receiver,scaling_generation);
        p.backing.retire(receiver,true,index==0); // Independent of completed CPU report.
        {
            Guard guard(p.lock);
            if(!p.state.terminal) {
                if(p.started) p.fail("manager_lifecycle_replacement");
                if(p.generation==UINT32_MAX) p.fail("generation_counter_overflow");
                if(!p.state.terminal) {
                    if(index==0) { p.owner=receiver; p.constructed=false; token=++p.generation; }
                    else if(p.owner==receiver) { ++p.generation; p.constructed=false; }
                }
            }
        }
        const uint32_t args[]={receiver};
        const auto result=forward(p.entries[index]->original(),args,1);
        if(index==0) {
            Guard guard(p.lock);
            if(!p.state.terminal && p.owner==receiver && p.generation==token) p.constructed=true;
        }
        return result;
    }
    static uint64_t __cdecl c1(uint32_t p) noexcept { try { return life(0,p); } catch(...) { scaling_callback_failure("c1"); } }
    static uint64_t __cdecl d1(uint32_t p) noexcept { try { return life(1,p); } catch(...) { scaling_callback_failure("d1"); } }
    static uint64_t __cdecl unload(uint32_t p) noexcept { try { return life(2,p); } catch(...) { scaling_callback_failure("unload"); } }
    static uint64_t load_impl(uint32_t receiver,uint32_t stream) {
        struct LoadingScope {
            LoadingScope(){++load_depth;}
            ~LoadingScope(){--load_depth;}
        } loading_scope;
        auto &p=self(); Scope scope{&p,receiver,0,0,0}; bool observe=false;
        {
            Guard guard(p.lock);
            if(!p.state.terminal) {
                if(current) p.fail("reentrant_load");
                else if(!p.constructed || p.owner!=receiver) p.fail("missing_constructed_owner");
                else if(p.started && p.thread!=GetCurrentThreadId()) p.fail("owner_thread_changed");
                else if(receiver>UINT32_MAX-p.phase_offset || !copy_bytes(receiver+p.phase_offset,&scope.phase,4)) p.fail("phase_unreadable");
                else if(!p.started && scope.phase!=0) p.fail("missed_startup_phase");
                else if(p.started && (scope.phase<p.last_phase || scope.phase>16)) p.fail("unsupported_phase_sequence");
                else if(p.serial==UINT32_MAX) p.fail("invocation_counter_overflow");
                else {
                    p.started=true; p.thread=GetCurrentThreadId(); p.last_phase=scope.phase;
                    scope.generation=p.generation; scope.invocation=++p.serial; observe=true;
                }
            }
        }
        auto *previous=current; if(observe) current=&scope;
        const uint32_t args[]={receiver,stream};
        const auto result=forward(p.entries[3]->original(),args,2);
        if(p.scaling && scope.phase==16) p.scaling->finish();
        {
            Guard guard(p.lock);
            if(observe && p.local(&scope) && scope.phase==16) {
                if(!p.resource_seen) p.fail("first_resource_not_observed");
                else if(!p.selected_seen) p.fail("selected_variant_not_supplied");
                else if(!p.width_seen || !p.height_seen) p.fail("nominal_fields_not_supplied");
                else if(p.rtts!=2) p.fail("two_original_creations_not_observed");
                else { p.state.terminal=true; p.state.complete=true; p.state.reason="cpu_join_complete"; }
            }
        }
        current=previous; return result;
    }
    static uint64_t __cdecl load(uint32_t receiver,uint32_t stream) noexcept {
        try { return load_impl(receiver,stream); }
        catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("Load"); }
    }
    static uint64_t sprite_impl(uint32_t receiver,uint32_t name,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
        auto &p=self(); ResourceScope scope{current,receiver,name}; bool observe=false; unsigned record_index=record_limit;
        {
            Guard guard(p.lock);
            if(p.local(current)) {
                if(current->phase!=10 || resource || p.resource_seen) p.fail("unsupported_resource_invocation");
                else if(!copy_name(name,p.input)) p.fail("input_name_unreadable_or_overflow");
                else if(std::strcmp(p.input,"menu_elements_nova3.bsprite")) p.fail("unexpected_first_resource");
                else {
                    auto r=p.record(Record::Kind::resource,*current); r.receiver=receiver;
                    std::memcpy(r.name,p.input,name_limit); observe=p.globals(r) && p.add(r); p.resource_seen=observe; p.sprite_manager=receiver; if(observe) record_index=p.state.count-1;
                }
            }
        }
        auto *previous=resource; if(observe) resource=&scope;
        const uint32_t args[]={receiver,name,a,b,c,d};
        const auto result=forward(p.calls[sprite_call]->original(),args,6);
        {
            Guard guard(p.lock);
            if(observe && p.local(scope.load)) {
                p.state.records[record_index].original_registers=result;
                p.state.records[record_index].returned_normally=true;
                if(static_cast<int32_t>(static_cast<uint32_t>(result))<0) p.fail("original_resource_load_failed");
            }
        }
        resource=previous; return result;
    }
    static uint64_t select_impl(uint32_t receiver,uint32_t name,uint32_t text,uint32_t scale,uint32_t width,uint32_t height,uint32_t option) {
        auto &p=self(); auto *scope=resource; const uint32_t args[]={receiver,name,text,scale,width,height,option};
        const auto result=forward(p.calls[select_call]->original(),args,7);
        Guard guard(p.lock);
        if(!p.state.terminal && scope && p.local(scope->load)) {
            if(scope->receiver!=receiver || scope->name!=name || p.selected_seen) p.fail("selector_invocation_mismatch");
            else {
                auto r=p.record(Record::Kind::selected,*scope->load); r.receiver=receiver;
                uint32_t chars=0;
                // Fresh output references supplied by this exact original invocation.
                // Never retain the string object or its data pointer past this hook.
                ++p.state.output_acquisitions;
                if(!copy_bytes(text,&chars,4) || !copy_name(chars,r.name) ||
                   !copy_bytes(width,&r.width,4) || !copy_bytes(height,&r.height,4)) p.fail("selector_output_unreadable_or_overflow");
                else if(!r.name[0]) p.fail("selected_variant_empty");
                else if(p.globals(r)) { r.original_registers=result; r.returned_normally=true; p.selected_seen=p.add(r); }
            }
        }
        return result;
    }
    static uint64_t get(unsigned index,uint32_t receiver,uint32_t name) {
        auto &p=self(); auto *scope=current; const uint32_t args[]={receiver,name};
        const auto result=forward(p.calls[index]->original(),args,2);
        Guard guard(p.lock);
        if(p.local(scope)) {
            const bool width=index==width_call; char supplied[name_limit]{};
            const auto pointer=static_cast<uint32_t>(result);
            if(scope->phase!=10 || receiver!=p.sprite_manager || !p.resource_seen || (width ? p.width_seen : !p.width_seen || p.height_seen)) p.fail("nominal_field_sequence_mismatch");
            else if(!copy_name(name,supplied) || std::strcmp(supplied,p.input)) p.fail("nominal_resource_name_mismatch");
            else if(!pointer || (!width && pointer!=p.sprite_pointer)) p.fail("nominal_sprite_identity_mismatch");
            else {
                auto r=p.record(width ? Record::Kind::nominal_width : Record::Kind::nominal_height,*scope);
                r.receiver=pointer; r.original_registers=result; r.returned_normally=true;
                ++p.state.output_acquisitions;
                const uint32_t offset=width ? 0xb8u : 0xbcu;
                // Fresh GetSprite return: native caller's immediately following
                // instruction consumes this same field. No retained sprite query.
                if(pointer>UINT32_MAX-offset || !copy_bytes(pointer+offset,width ? &r.width : &r.height,4)) p.fail("nominal_field_unreadable");
                else if(p.globals(r) && p.add(r)) {
                    if(width) { p.sprite_pointer=pointer; p.width_seen=true; } else p.height_seen=true;
                }
            }
        }
        return result;
    }
    static uint64_t __cdecl width(uint32_t p,uint32_t n) noexcept { try { return get(width_call,p,n); } catch(...) { scaling_callback_failure("width"); } }
    static uint64_t __cdecl height(uint32_t p,uint32_t n) noexcept { try { return get(height_call,p,n); } catch(...) { scaling_callback_failure("height"); } }
    static uint64_t rtt(unsigned index,uint32_t receiver,uint32_t width,uint32_t height) {
        auto &p=self(); auto *scope=current; const uint32_t args[]={receiver,width,height};
        bool scale=false;
        { Guard guard(p.lock); scale=p.scaling && p.local(scope) && scope->phase==16 && p.rtts==index-rtt0_call; }
        const auto result=scale ? p.scaling->construct(scope->owner,scope->generation,index-rtt0_call,receiver,width,height,p.calls[index]->original()) : p.scaling ? p.scaling->native_constructor(receiver,width,height,p.calls[index]->original()) : forward(p.calls[index]->original(),args,3);
        Guard guard(p.lock);
        if(p.local(scope)) {
            if(scope->phase!=16 || p.rtts!=(index-rtt0_call)) p.fail("rtt_creation_sequence_mismatch");
            else {
                auto r=p.record(Record::Kind::rtt,*scope); r.receiver=receiver;
                r.width=static_cast<int32_t>(width); r.height=static_cast<int32_t>(height);
                r.original_registers=result; r.returned_normally=true;
                // C1 returns void. Only normal completion, supplied this and the
                // original argument words are facts; no post-constructor read.
                if(p.globals(r) && p.add(r)) {
                    p.backing.created(index-rtt0_call,scope->owner,scope->generation,scope->invocation,
                        receiver,p.thread,width,height);
                    ++p.rtts;
                }
            }
        }
        return result;
    }
    static uint64_t __cdecl rtt0(uint32_t p,uint32_t w,uint32_t h) noexcept {
        try { return rtt(rtt0_call,p,w,h); }
        catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("rtt0"); }
    }
    static uint64_t __cdecl rtt1(uint32_t p,uint32_t w,uint32_t h) noexcept {
        try { return rtt(rtt1_call,p,w,h); }
        catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("rtt1"); }
    }

    static uint64_t begin_impl(uint32_t receiver,uint32_t clear) {
        auto &p=self();
        RttBackingCapture::Scope capture(p.backing,receiver,GetCurrentThreadId(),clear);
        const uint32_t args[]={receiver,clear};
        const auto result=forward(p.entries[4]->original(),args,2);
        capture.returned(result); return result;
    }
    static uint64_t rtt_d1_impl(uint32_t receiver) {
        ui::MenuSurfaces::retire(receiver);
        auto &p=self(); p.backing.retire(receiver,false);
        const uint32_t args[]={receiver};
        return forward(p.entries[5]->original(),args,1);
    }

    static uint64_t __cdecl sprite(uint32_t receiver,uint32_t name,uint32_t a,uint32_t b,uint32_t c,uint32_t d) noexcept {
        try { return sprite_impl(receiver,name,a,b,c,d); } catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("sprite"); }
    }
    static uint64_t __cdecl select(uint32_t receiver,uint32_t name,uint32_t text,uint32_t scale,uint32_t width,uint32_t height,uint32_t option) noexcept {
        try { return select_impl(receiver,name,text,scale,width,height,option); } catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("select"); }
    }
    static uint64_t __cdecl begin(uint32_t receiver,uint32_t clear) noexcept {
        try { return begin_impl(receiver,clear); } catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("begin"); }
    }
    static uint64_t __cdecl rtt_d1(uint32_t receiver) noexcept {
        try { return rtt_d1_impl(receiver); } catch(...) { if(self().scaling) self().scaling->finish(); scaling_callback_failure("rtt_d1"); }
    }
    Impl(const ElfImage &image,bool fixture,uint32_t width,uint32_t flag,uint32_t phase,unsigned limit,bool scale=false):
        width_address(width),flag_address(flag),phase_offset(phase),capacity(limit) {
        Guard install(installation);
        if(active.load() || !capacity || capacity>record_limit) throw std::runtime_error("menu observer installation unavailable");
        if(!fixture) {
            authenticate(image);
            width_address=image.address(0x11437f0,4); flag_address=image.address(0x115c026,1);
            for(unsigned i=0;i<3;++i) if(abi::require_function(image,life_names[i])!=image.address(life_va[i]))
                throw std::runtime_error("menu observer lifecycle mapping mismatch");
            const char *names[]={load_name,sprite_name,select_name,get_name,rtt_name,
                "_ZN14CRTTMenuObject8BeginRTTEb","_ZN14CRTTMenuObjectD1Ev"};
            const uint32_t values[]={0x540e50,0x3fe7d0,0x3fd6a0,0x3f8490,0x2b0460,0x2a26a0,0x2b7900};
            for(unsigned i=0;i<7;++i) if(abi::require_function(image,names[i])!=image.address(values[i]))
                throw std::runtime_error("menu observer call mapping mismatch");
        }
        // Preflight every seam before the first mutation. Installation still
        // repeats GuestHook/GuestCall checks; no mismatch is silently bypassed.
        constexpr uint8_t short_entry[]={0x55,0x89,0xe5,0x56,0x53};
        constexpr uint8_t death_entry[]={0x55,0x89,0xe5,0x57,0x56,0x53};
        constexpr uint8_t load_entry[]={0x55,0x89,0xe5,0x81,0xec,0xa8,0,0,0};
        constexpr uint8_t begin_entry[]={0x55,0x89,0xe5,0x83,0xec,0x28};
        constexpr uint8_t rtt_death_entry[]={0x55,0x89,0xe5,0x83,0xec,0x48};
        const auto entry_name=[&](unsigned i) -> const char * {
            if(i<3) return fixture ? fixture_life[i] : life_names[i];
            if(i==3) return fixture ? "obs_load" : load_name;
            if(i==4) return fixture ? "obs_begin" : "_ZN14CRTTMenuObject8BeginRTTEb";
            return fixture ? "obs_rtt_d1" : "_ZN14CRTTMenuObjectD1Ev";
        };
        for(unsigned i=0;i<6;++i) {
            const char *name=entry_name(i);
            const auto at=abi::require_function(image,name);
            const auto *expected=i==0 ? short_entry : i==3 ? load_entry : i==4 ? begin_entry : i==5 ? rtt_death_entry : death_entry;
            const unsigned count=i==0 ? 5u : i==3 ? 9u : 6u;
            if(at>UINT32_MAX-count || !image.executable(at+count-1) || std::memcmp(reinterpret_cast<const void *>(at),expected,count))
                throw std::runtime_error("menu observer entry preflight mismatch");
        }
        const uint32_t call_offsets[]={0x4fd,0x21b,0x7c1,0x7ef,0xc78,0xcd1};
        const char *call_callees[]={sprite_name,select_name,get_name,get_name,rtt_name,rtt_name};
        for(unsigned i=0;i<6;++i) {
            const std::string owner_name=fixture ? fixture_calls[i] : (i==select_call ? sprite_name : load_name);
            const std::string callee=fixture ? owner_name+"_body" : call_callees[i];
            const auto start=abi::require_function(image,owner_name);
            const auto offset=fixture ? image.symbol(owner_name+"_site")-start : call_offsets[i];
            if(start>UINT32_MAX-4 || offset>UINT32_MAX-start-4) throw std::runtime_error("menu observer call preflight overflow");
            const auto at=start+offset; uint8_t bytes[5]{}; uint32_t displacement=0;
            if(!image.executable(at) || !image.executable(at+4) || !copy_bytes(at,bytes,5))
                throw std::runtime_error("menu observer call preflight unreadable");
            std::memcpy(&displacement,bytes+1,4);
            if(bytes[0]!=0xe8 || at+5+displacement!=abi::require_function(image,callee))
                throw std::runtime_error("menu observer direct call preflight mismatch");
        }
        if(scale && !fixture) scaling=std::make_unique<InitialTargetScaling>(image);
        const uint32_t replacements[]={reinterpret_cast<uint32_t>(&c1),reinterpret_cast<uint32_t>(&d1),
            reinterpret_cast<uint32_t>(&unload),reinterpret_cast<uint32_t>(&load),
            reinterpret_cast<uint32_t>(&begin),reinterpret_cast<uint32_t>(&rtt_d1)};
        for(unsigned i=0;i<6;++i) {
            const char *name=entry_name(i);
            if(i==0) entries[i]=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},replacements[i]);
            else if(i==3) entries[i]=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x81,0xec,0xa8,0,0,0},replacements[i]);
            else if(i==4) entries[i]=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x28},replacements[i]);
            else if(i==5) entries[i]=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x48},replacements[i]);
            else entries[i]=std::make_unique<abi::GuestHook>(image,name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},replacements[i]);
        }
        const uint32_t adapters[]={reinterpret_cast<uint32_t>(&sprite),reinterpret_cast<uint32_t>(&select),
            reinterpret_cast<uint32_t>(&Impl::width),reinterpret_cast<uint32_t>(&Impl::height),reinterpret_cast<uint32_t>(&rtt0),reinterpret_cast<uint32_t>(&rtt1)};
        const uint32_t offsets[]={0x4fd,0x21b,0x7c1,0x7ef,0xc78,0xcd1};
        const char *callees[]={sprite_name,select_name,get_name,get_name,rtt_name,rtt_name};
        for(unsigned i=0;i<6;++i) {
            const std::string owner_name=fixture ? fixture_calls[i] : (i==select_call ? sprite_name : load_name);
            const std::string callee=fixture ? owner_name+"_body" : callees[i];
            const uint32_t offset=fixture ? image.symbol(owner_name+"_site")-abi::require_function(image,owner_name) : offsets[i];
            calls[i]=std::make_unique<abi::GuestCall>(image,owner_name,offset,callee,adapters[i]);
        }
        state.enabled=true; state.reason="waiting_for_initial_load";
        active.store(this,std::memory_order_release);
    }
    ~Impl() {
        Guard install(installation);
        // Caller has joined/quiesced every guest producer. No concurrent callback
        // or report may outlive this object. Restore before releasing active.
        for(auto &call:calls) call.reset();
        for(auto &entry:entries) entry.reset();
        active.store(nullptr,std::memory_order_release);
    }
};
std::atomic<NativeMenuObserver::Impl *> NativeMenuObserver::Impl::active{nullptr};
SRWLOCK NativeMenuObserver::Impl::installation=SRWLOCK_INIT;
thread_local NativeMenuObserver::Impl::Scope *NativeMenuObserver::Impl::current=nullptr;
bool NativeMenuObserver::loading() noexcept { return load_depth!=0; }
thread_local NativeMenuObserver::Impl::ResourceScope *NativeMenuObserver::Impl::resource=nullptr;
NativeMenuObserver::NativeMenuObserver(const ElfImage &image,bool enabled,bool scale) {
    if(!enabled && !scale) return;
    try { impl_=std::make_unique<Impl>(image,false,0,0,0x114,record_limit,scale); }
    catch(const std::exception &error) {
        // Explicit diagnostic admission fails before startup. Do not continue
        // after a failed code-protection transaction in the shared hook facility.
        std::fprintf(stderr,"menu-observer CPU_ONLY status=INCONCLUSIVE reason=installation_rejected detail=%s\n",error.what());
        throw;
    }
}
#ifdef NOVA_MENU_OBSERVER_FIXTURE
NativeMenuObserver::NativeMenuObserver(const ElfImage &image,FixtureBindings b):
    impl_(std::make_unique<Impl>(image,true,b.width,b.flag,b.phase_offset,b.capacity)) {
    impl_->fixture_lifecycle_scaling=b.lifecycle_scaling;
}
#endif
NativeMenuObserver::~NativeMenuObserver()=default;
void NativeMenuObserver::configure_initial_targets(platform::GuestExtent extent,platform::RenderLimits limits) noexcept {
    if(impl_ && impl_->scaling) impl_->scaling->configure(extent,limits);
}
RttBackingCapture::Snapshot NativeMenuObserver::backing_snapshot() const {
    return impl_ ? impl_->backing.snapshot() : RttBackingCapture::Snapshot{};
}
NativeMenuObserver::Snapshot NativeMenuObserver::snapshot() const {
    if(!impl_) return {};
    Guard guard(impl_->lock); return impl_->state;
}
void NativeMenuObserver::report(FILE *file,bool final) {
    if(!impl_ || !file) return;
    if(final && impl_->scaling) impl_->scaling->report(file);
    impl_->backing.report(file,final); // Can complete after the CPU join was reported.
    Snapshot out;
    {
        Guard guard(impl_->lock);
        if(impl_->reported) return;
        if(final && !impl_->state.terminal) impl_->fail(impl_->started ? "initialization_not_finished" : "initial_load_not_observed");
        if(!impl_->state.terminal) return;
        impl_->reported=true; out=impl_->state;
    }
    std::fprintf(file,"menu-observer CPU_ONLY status=%s reason=%s records=%u\n",out.complete ? "COMPLETE" : "INCONCLUSIVE",out.reason,out.count);
    for(unsigned i=0;i<out.count;++i) {
        const auto &r=out.records[i];
        // Hex-escape every name byte so filenames cannot inject diagnostic lines.
        std::fprintf(file,"menu-observer kind=%u manager=%08x generation=%u invocation=%u receiver=%08x w=%d h=%d native_width_snapshot=%d flag_snapshot=%u returned_normally=%u edx_eax=%016llx name_hex=",
            static_cast<unsigned>(r.kind),r.manager,r.generation,r.invocation,r.receiver,r.width,r.height,r.native_width,r.flag,
            r.returned_normally ? 1u : 0u,static_cast<unsigned long long>(r.original_registers));
        for(unsigned j=0;j<name_limit && r.name[j];++j) std::fprintf(file,"%02x",static_cast<unsigned>(static_cast<unsigned char>(r.name[j])));
        std::fputc('\n',file);
    }
}
}
