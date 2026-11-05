#pragma once
// T09f: passive capture of ORIGINAL provider calls during two identified first
// BeginRTT entries. No GL query, guest pointer read, allocation policy or GL state.
#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <type_traits>
#include <utility>
namespace nova::menu {
class RttBackingCapture {
public:
    static constexpr unsigned target_limit=2,event_limit=64;
    enum class Op : uint32_t { active_texture,bind_texture,tex_image_2d,
        bind_renderbuffer,renderbuffer_storage,bind_framebuffer,
        framebuffer_texture_2d,framebuffer_renderbuffer,check_framebuffer,viewport };
    struct Event { Op op{}; std::array<uint32_t,9> words{}; uint32_t result=0; };
    struct Target {
        uint32_t manager=0,generation=0,invocation=0,receiver=0,thread=0,width=0,height=0;
        uint32_t clear_word=0,count=0;
        uint64_t original_registers=0;
        bool registered=false,begun=false,returned=false,retired=false,invalid=false;
        const char *reason="not_created";
        std::array<Event,event_limit> events{};
    };
    struct Snapshot { bool enabled=false; std::array<Target,target_limit> targets{}; };
private:
    mutable std::mutex mutex_;
    Snapshot state_{};
    bool reported_=false;
    static void invalidate(Target &t,const char *why) noexcept {
        if(!t.invalid) { t.invalid=true; t.reason=why; }
    }
public:
    RttBackingCapture() { state_.enabled=true; }
    void created(unsigned slot,uint32_t manager,uint32_t generation,uint32_t invocation,
                 uint32_t receiver,uint32_t thread,uint32_t width,uint32_t height) {
        std::lock_guard guard(mutex_);
        if(slot>=target_limit) return;
        auto &t=state_.targets[slot];
        if(t.registered || !receiver || (slot && state_.targets[0].receiver==receiver)) {
            invalidate(t,"duplicate_or_null_creation"); return;
        }
        t.manager=manager; t.generation=generation; t.invocation=invocation;
        t.receiver=receiver; t.thread=thread; t.width=width; t.height=height;
        t.registered=true; t.reason="waiting_first_begin";
    }
    // Host identity comparisons only; called BEFORE original retirement forwards.
    void retire(uint32_t receiver,bool manager,bool replacement=false) {
        std::lock_guard guard(mutex_);
        for(auto &t:state_.targets) if(t.registered &&
            (replacement || (manager ? t.manager==receiver : t.receiver==receiver))) {
            t.retired=true;
            if(!t.returned) invalidate(t,"retired_before_first_begin_return");
        }
    }
    void reject(const char *reason) {
        std::lock_guard guard(mutex_);
        for(auto &t:state_.targets) if(t.registered) invalidate(t,reason);
    }
    class Scope {
        RttBackingCapture &owner_;
        Scope *previous_=nullptr;
        unsigned slot_=target_limit;
        bool finished_=false;
        inline static thread_local Scope *current_=nullptr;
        friend class RttBackingCapture;
        void append(Event event) {
            std::lock_guard guard(owner_.mutex_);
            if(slot_==target_limit) return;
            auto &t=owner_.state_.targets[slot_];
            if(t.invalid || t.retired) return;
            if(t.count==event_limit) { invalidate(t,"event_overflow"); return; }
            t.events[t.count++]=event;
        }
    public:
        Scope(RttBackingCapture &owner,uint32_t receiver,uint32_t thread,uint32_t clear):owner_(owner) {
            previous_=current_;
            // Any nested BeginRTT suppresses attribution, including another RTT.
            if(previous_) previous_->owner_.reject("nested_begin");
            {
                std::lock_guard guard(owner_.mutex_);
                for(unsigned i=0;i<target_limit;++i) {
                    auto &t=owner_.state_.targets[i];
                    if(!t.registered || t.receiver!=receiver || t.invalid || t.retired || t.begun) continue;
                    if(t.thread!=thread) { invalidate(t,"begin_thread_changed"); break; }
                    if(previous_) { invalidate(t,"nested_begin"); break; }
                    t.begun=true; t.clear_word=clear; t.reason="first_begin_in_progress"; slot_=i; break;
                }
            }
            current_=this; // Also masks unrelated/repeated original BeginRTT calls.
        }
        Scope(const Scope &)=delete;
        Scope &operator=(const Scope &)=delete;
        void returned(uint64_t registers) {
            std::lock_guard guard(owner_.mutex_);
            finished_=true;
            if(slot_==target_limit) return;
            auto &t=owner_.state_.targets[slot_];
            if(t.invalid || t.retired) return;
            t.returned=true; t.original_registers=registers;
            t.reason=t.count ? "original_calls_captured" : "no_observed_provider_calls";
        }
        ~Scope() {
            if(!finished_ && slot_!=target_limit) {
                std::lock_guard guard(owner_.mutex_);
                invalidate(owner_.state_.targets[slot_],"begin_did_not_return_normally");
            }
            current_=previous_;
        }
        static void record(Event event) { if(current_) current_->append(event); }
    };
    // Invoked by the actual cdecl->stdcall adapters and by mocked fixture providers.
    // The provider is called exactly once. Capture happens only after normal return.
    template<class Provider> static auto forward(Event event,Provider &&provider)
        -> std::invoke_result_t<Provider> {
        using Result=std::invoke_result_t<Provider>;
        if constexpr(std::is_void_v<Result>) {
            std::forward<Provider>(provider)(); Scope::record(event);
        } else {
            auto result=std::forward<Provider>(provider)();
            event.result=static_cast<uint32_t>(result); Scope::record(event); return result;
        }
    }
    Snapshot snapshot() const { std::lock_guard guard(mutex_); return state_; }
    void report(FILE *file,bool final) {
        Snapshot out;
        {
            std::lock_guard guard(mutex_);
            if(reported_) return;
            bool ready=true;
            for(const auto &t:state_.targets) ready=ready && (t.returned || t.invalid);
            if(!final && !ready) return;
            reported_=true; out=state_;
        }
        std::fprintf(file,"menu-rtt-backing API_CALLS_ONLY targets=2 extra_queries=0\n");
        for(unsigned i=0;i<target_limit;++i) {
            const auto &t=out.targets[i];
            std::fprintf(file,"menu-rtt-backing slot=%u manager=%08x generation=%u invocation=%u receiver=%08x logical_w=%u logical_h=%u registered=%u begun=%u returned=%u retired=%u invalid=%u reason=%s clear_word=%08x edx_eax=%016llx events=%u\n",
                i,t.manager,t.generation,t.invocation,t.receiver,t.width,t.height,
                unsigned(t.registered),unsigned(t.begun),unsigned(t.returned),unsigned(t.retired),unsigned(t.invalid),
                t.reason,t.clear_word,static_cast<unsigned long long>(t.original_registers),t.count);
            for(unsigned j=0;j<t.count;++j) {
                const auto &e=t.events[j];
                std::fprintf(file,"menu-rtt-backing slot=%u event=%u op=%u result=%08x words=",i,j,unsigned(e.op),e.result);
                for(auto word:e.words) std::fprintf(file,"%08x,",word);
                std::fputc('\n',file);
            }
        }
    }
};
}
