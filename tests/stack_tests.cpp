#include "abi/guest_call.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "loader/elf_image.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <thread>

// Test executable only: fail real shared/map allocations without a runtime switch.
static thread_local int allocation_failure = -1;
void* operator new(size_t size) {
    if (allocation_failure == 0) { allocation_failure = -1; throw std::bad_alloc(); }
    if (allocation_failure > 0) --allocation_failure;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {
using Word = uint32_t;
using Itf = void*;
struct Context {
    Word frame, observe, alignment = 99, touched = 0;
    Word target_state = 0, target_protect = 0, target_allocation = 0;
    Word stack_base = 0, stack_limit = 0, guard_pages = 0, committed = 0;
    Word lower_state = 0, lower_protect = 0;
    DWORD tid = 0;
    int initial_errno = -1;
    std::atomic<unsigned> observed{0};
};
static_assert(offsetof(Context, alignment) == 8 && offsetof(Context, touched) == 12);
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
template<class F> F import(nova::ImportRegistry& r, const char* name) {
    auto p = r.find(name); check(p != nullptr, "missing synthetic import");
    return reinterpret_cast<F>(p->address);
}
template<class F> F method(Itf p, unsigned slot) { return reinterpret_cast<F>((*static_cast<Word**>(p))[slot]); }
using Create = int(__cdecl*)(Word*, const nova::bionic::ThreadAttr*, Word, Word);
using Join = int(__cdecl*)(Word, Word*);
using KeyCreate = int(__cdecl*)(Word*, Word);
using Specific = int(__cdecl*)(Word, const void*);
static Specific specific = nullptr;
static Word key = 0;
static bool use_tls = false;
static std::atomic<unsigned> destructors{0};
void __cdecl destructor(void* raw) {
    auto& c = *static_cast<Context*>(raw);
    if (nova::bionic::guest_errno != 1337 || c.touched != 1) std::abort();
    ++destructors;
}
void __cdecl observe(Context* raw, Word before, Word target) {
    auto& c = *raw; c.tid = GetCurrentThreadId(); c.initial_errno = nova::bionic::guest_errno;
    auto tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    c.stack_base = reinterpret_cast<Word>(tib->StackBase);
    c.stack_limit = reinterpret_cast<Word>(tib->StackLimit);
    MEMORY_BASIC_INFORMATION m{};
    check(VirtualQuery(reinterpret_cast<void*>(target), &m, sizeof m) == sizeof m, "target query");
    c.target_state = m.State; c.target_protect = m.Protect;
    c.target_allocation = reinterpret_cast<Word>(m.AllocationBase);
    Word at = c.target_allocation;
    unsigned regions = 0;
    while (at < c.stack_base && regions++ < 256) {
        check(VirtualQuery(reinterpret_cast<void*>(at), &m, sizeof m) == sizeof m, "stack region query");
        check(reinterpret_cast<Word>(m.AllocationBase) == c.target_allocation && m.RegionSize, "stack allocation extent");
        if (m.State == MEM_COMMIT) c.committed += static_cast<Word>(m.RegionSize);
        if (m.State == MEM_COMMIT && (m.Protect & PAGE_GUARD)) c.guard_pages += static_cast<Word>(m.RegionSize / 4096);
        std::printf("region tid=%lu base=0x%08x bytes=%zu state=0x%lx protect=0x%lx\n", c.tid, at, static_cast<size_t>(m.RegionSize), m.State, m.Protect);
        at += static_cast<Word>(m.RegionSize);
    }
    check(at == c.stack_base, "bounded complete stack region scan");
    check(VirtualQuery(reinterpret_cast<void*>(c.target_allocation-1), &m, sizeof m)==sizeof m &&
        reinterpret_cast<Word>(m.AllocationBase)!=c.target_allocation,"bounded lower native allocation boundary");
    c.lower_state=m.State;c.lower_protect=m.Protect;
    if(!c.guard_pages) check(c.committed==c.stack_base-c.target_allocation,"no growth guard only when entire native stack committed");
    std::printf("stack before=0x%08x target=0x%08x frame=%u state=0x%x protect=0x%x base=0x%x limit=0x%x allocation=0x%x committed=%u guard_pages=%u lower_state=0x%x lower_protect=0x%x tid=%lu\n",
        before, target, c.frame, c.target_state, c.target_protect, c.stack_base, c.stack_limit, c.target_allocation, c.committed, c.guard_pages,c.lower_state,c.lower_protect,c.tid);
    std::fflush(stdout);
    if (use_tls) check(specific(key, &c) == 0, "set synthetic TLS");
    nova::bionic::guest_errno = 1337;
    ++c.observed;
}
Context context(Word frame) { return Context{frame, reinterpret_cast<Word>(&observe)}; }
void completed(const Context& c) {
    check(c.observed == 1 && c.touched == 1 && c.alignment == 0, "unprobed CALL/SSE and16-byte guest entry");
    check(c.target_state == MEM_COMMIT && (c.target_protect & 0xff) == PAGE_READWRITE && !(c.target_protect & PAGE_GUARD), "target committed writable before guest SUB");
}
void callback(nova::ImportRegistry& r, nova::ElfImage& image, Context& c) {
    using State = Word(__cdecl*)(Itf, Word);
    using Get = Word(__cdecl*)(Itf, const void*, Itf*);
    using Destroy = void(__cdecl*)(Itf);
    auto id = [&](const char* n) { return *reinterpret_cast<const void**>(r.find(n)->address); };
    Itf engine_object = nullptr, engine = nullptr, mix = nullptr, player = nullptr, play = nullptr, queue = nullptr;
    auto create = import<Word(__cdecl*)(Itf*, Word, const void*, Word, const void*, const void*)>(r, "slCreateEngine");
    check(create(&engine_object,0,nullptr,0,nullptr,nullptr)==0, "engine create");
    check(method<State>(engine_object,0)(engine_object,0)==0, "engine realize");
    check(method<Get>(engine_object,3)(engine_object,id("SL_IID_ENGINE"),&engine)==0, "engine get");
    check(method<Word(__cdecl*)(Itf,Itf*,Word,const void*,const void*)>(engine,7)(engine,&mix,0,nullptr,nullptr)==0, "mix create");
    check(method<State>(mix,0)(mix,0)==0, "mix realize");
    Word locator[]={6,1},pcm[]={2,1,44100000,16,16,4,2},output[]={4,reinterpret_cast<Word>(mix)};
    const void* source[]={locator,pcm}; const void* sink[]={output,nullptr};
    const void* ids[]={id("SL_IID_BUFFERQUEUE")}; Word required[]={1};
    check(method<Word(__cdecl*)(Itf,Itf*,const void*,const void*,Word,const void*,const void*)>(engine,2)(engine,&player,source,sink,1,ids,required)==0,"player create");
    check(method<State>(player,0)(player,0)==0,"player realize");
    check(method<Get>(player,3)(player,id("SL_IID_PLAY"),&play)==0,"play get");
    check(method<Get>(player,3)(player,id("SL_IID_BUFFERQUEUE"),&queue)==0,"queue get");
    check(method<Word(__cdecl*)(Itf,Word,void*)>(queue,3)(queue,image.lookup("stack_callback"),&c)==0,"callback set");
    short samples[44]{};
    check(method<Word(__cdecl*)(Itf,const void*,Word)>(queue,0)(queue,samples,sizeof samples)==0,"queue put");
    check(method<State>(play,0)(play,3)==0,"play");
    for (unsigned i=0; c.observed==0 && i<3000; ++i) Sleep(1);
    check(c.observed==1,"owned audio callback completion");
    method<Destroy>(player,6)(player);
    method<Destroy>(mix,6)(mix); method<Destroy>(engine_object,6)(engine_object);
    check(destructors==1,"audio worker TLS cleanup");
}
} // namespace
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if (argc<3 || argc>5) return 2;
    try {
        const std::string mode=argv[2];
        Word frame=argc>3?static_cast<Word>(std::strtoul(argv[3],nullptr,0)):0x20008;
        Word requested=argc>4?static_cast<Word>(std::strtoul(argv[4],nullptr,0)):0;
        check(frame>=24 && frame<=0x180008 && (frame&15)==8,"bounded aligned synthetic frame");
        nova::ImportRegistry r; nova::bionic::register_boot(r);
        nova::audio::configure({"null",{}}); nova::audio::register_opensl(r);
        nova::ElfImage image(argv[1],r);
        auto create=import<Create>(r,"pthread_create"); auto join=import<Join>(r,"pthread_join");
        auto entry=image.lookup("stack_unprobed"); Context c=context(frame);
        specific=import<Specific>(r,"pthread_setspecific");
        use_tls=mode=="pthread" || mode=="detach" || mode=="callback" || mode=="guard0" || mode=="guard8192";
        if(use_tls) check(import<KeyCreate>(r,"pthread_key_create")(&key,reinterpret_cast<Word>(&destructor))==0,"key create");
        nova::bionic::guest_errno=73;
        if(mode=="main") { Word arg=reinterpret_cast<Word>(&c);check(nova::abi::call_guest(entry,&arg,1)==0x51a7,"main result");check(c.tid==GetCurrentThreadId()&&c.initial_errno==73,"main provenance"); }
        else if(mode=="thread") { std::thread t([&] {Word arg=reinterpret_cast<Word>(&c);if(nova::abi::call_guest(entry,&arg,1)!=0x51a7) std::abort();});t.join();check(c.tid!=GetCurrentThreadId()&&c.initial_errno==0,"std thread provenance"); }
        else if(mode=="callback") callback(r,image,c);
        else if(mode=="pthread"||mode=="detach"||mode=="guard0"||mode=="guard8192") {
            nova::bionic::ThreadAttr a{};check(import<int(__cdecl*)(nova::bionic::ThreadAttr*)>(r,"pthread_attr_init")(&a)==0,"attr init");
            check(sizeof a==24 && a.stack_size==1048576 && a.guard_size==4096 && a.flags==0 && a.stack_base==0 && a.policy==0 && a.priority==0,"default Bionic attribute layout/values");
            if(requested) check(import<int(__cdecl*)(nova::bionic::ThreadAttr*,Word)>(r,"pthread_attr_setstacksize")(&a,requested)==0,"explicit size");
            if(mode=="detach") a.flags=1;
            if(mode=="guard0") a.guard_size=0;
            if(mode=="guard8192") a.guard_size=8192;
            auto before=a;Word id=0,result=0;
            check(create(&id,requested||mode=="detach"?&a:nullptr,entry,reinterpret_cast<Word>(&c))==0,"pthread create");
            if(mode=="detach") {for(unsigned i=0;destructors==0&&i<3000;++i) Sleep(1);check(destructors==1,"detached cleanup");check(join(id,nullptr)!=0,"detached join rejects");}
            else check(join(id,&result)==0&&result==0x51a7&&destructors==1,"pthread join/result/TLS");
            check(std::memcmp(&a,&before,sizeof a)==0,"guest attr preserved");check(c.initial_errno==0&&c.tid!=GetCurrentThreadId(),"pthread provenance/errno");
        } else if(mode=="failures") {
            nova::bionic::ThreadAttr a{};check(import<int(__cdecl*)(nova::bionic::ThreadAttr*)>(r,"pthread_attr_init")(&a)==0,"attr init");
            Word id=0xdeadbeef;
            auto setsize=import<int(__cdecl*)(nova::bionic::ThreadAttr*,Word)>(r,"pthread_attr_setstacksize");
            auto original=a;check(setsize(&a,8191)==22&&std::memcmp(&a,&original,sizeof a)==0,"invalid setter leaves attributes unchanged");
            check(setsize(&a,8192)==0&&a.stack_size==8192&&a.guard_size==original.guard_size,"minimum setter preserves guard");
            for(Word bad:{0u,4096u,8191u}) {a.stack_size=bad;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==22&&id==0xdeadbeef,"malformed size no publication");}
            a.stack_size=65536;a.stack_base=1;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==95&&id==0xdeadbeef,"existing custom stack unsupported");a.stack_base=0;
            a.policy=1;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==95&&id==0xdeadbeef,"existing scheduling unsupported");a.policy=0;
            a.priority=1;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==95&&id==0xdeadbeef,"existing priority unsupported");a.priority=0;
            a.flags=2;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==95&&id==0xdeadbeef,"existing flags unsupported");a.flags=0;
            DWORD handles_before=0,handles_after=0;check(GetProcessHandleCount(GetCurrentProcess(),&handles_before)!=0,"handle count");
            for(unsigned n=0;n<16;++n) {
                for(int fault:{0,1}) {allocation_failure=fault;int rc=create(&id,&a,entry,reinterpret_cast<Word>(&c));allocation_failure=-1;check(rc==11&&id==0xdeadbeef,"allocation failure11/no publication");}
                a.stack_size=0xffff0000;check(create(&id,&a,entry,reinterpret_cast<Word>(&c))==11&&id==0xdeadbeef,"actual32-bit create resource failure");a.stack_size=65536;
            }
            check(GetProcessHandleCount(GetCurrentProcess(),&handles_after)!=0&&handles_after==handles_before,"no failed creation handle leak");
            check(c.observed==0&&c.touched==0&&nova::bionic::guest_errno==73,"failed create has no guest entry/errno change");
            std::puts("PASS stack failures: validation, allocation and real32-bit API failure preserve ownership/output");return 0;
        } else return 2;
        completed(c);
        if(mode!="main") check(nova::bionic::guest_errno==73,"parent errno preserved");
        if(use_tls) check(import<int(__cdecl*)(Word)>(r,"pthread_key_delete")(key)==0,"key delete");
        nova::audio::shutdown();
        std::printf("PASS stack %s frame=%u requested=%u result=0x51a7 alignment=16\n",mode.c_str(),frame,requested);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"stack test: %s\n",e.what());return 1;}
}
