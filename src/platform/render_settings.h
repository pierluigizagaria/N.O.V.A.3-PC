#pragma once
#include "platform/render_catalogue.h"
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <chrono>

namespace nova::platform {
enum class RenderAction { preview, keep, revert };
struct RenderRequest { uint64_t revision=0; GuestExtent extent; RenderAction action=RenderAction::preview; };
struct RenderSettingsSnapshot {
    GuestExtent selected,active;
    bool available=false,busy=false,unsaved=false;
    bool confirming=false;
    int seconds_remaining=0;
    RenderLimits limits;
    std::string error;
};
// Host values only. UI stages/requests; GL consumes between native steps and
// completes only after native resources and the host backing agree. A request
// or a selected combo value never establishes applied dimensions.
class RenderSettings {
  public:
    explicit RenderSettings(GuestExtent active) { state_.selected=state_.active=active; }
    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    RenderSettingsSnapshot snapshot(int64_t now=now_ms()) const {
        std::lock_guard lock(mutex_); auto result=state_;
        result.seconds_remaining=state_.confirming ? static_cast<int>(std::max<int64_t>(0,deadline_-now+999)/1000) : 0;
        return result;
    }
    void initialize(GuestExtent actual,RenderLimits limits) {
        std::lock_guard lock(mutex_);
        if(state_.available || state_.busy || !limits.admits(actual))
            throw std::logic_error("Invalid render capability initialization");
        if(actual!=state_.active) state_.error="Saved resolution is unavailable. Using "+render_extent_name(actual)+".";
        state_.selected=state_.active=actual; state_.limits=limits;
    }
    void available(bool value) { std::lock_guard lock(mutex_); state_.available=value; }
    bool select(GuestExtent extent) {
        if(!supported_render_extent(extent)) return false;
        std::lock_guard lock(mutex_);
        if(state_.busy || state_.confirming || !state_.limits.admits(extent)) return false;
        state_.selected=extent; state_.error.clear(); return true;
    }
    bool request() {
        std::lock_guard lock(mutex_);
        if(!state_.available || state_.busy || state_.confirming ||
           (state_.selected==state_.active && !state_.unsaved)) return false;
        state_.busy=true; state_.error.clear();
        pending_=RenderRequest{++revision_,state_.selected,
            state_.selected==state_.active ? RenderAction::keep : RenderAction::preview}; return true;
    }
    bool decide(bool keep,int64_t now=now_ms()) {
        std::lock_guard lock(mutex_);
        if(!state_.confirming || state_.busy) return false;
        queue_decision(keep && now<deadline_); return true;
    }
    std::optional<RenderRequest> take(int64_t now=now_ms()) {
        std::lock_guard lock(mutex_);
        if(state_.confirming && !state_.busy && now>=deadline_) queue_decision(false);
        if(!pending_) return {};
        in_flight_=std::exchange(pending_,std::nullopt); return in_flight_;
    }
    void complete(const RenderRequest &request,bool applied,bool saved,std::string error,int64_t now=now_ms()) {
        std::lock_guard lock(mutex_);
        if(!in_flight_ || request.revision!=in_flight_->revision || request.extent!=in_flight_->extent)
            throw std::logic_error("Stale render completion");
        if(applied) {
            if(request.action==RenderAction::preview) {
                rollback_=state_.active; rollback_unsaved_=state_.unsaved;
                state_.active=request.extent; state_.confirming=true;
                state_.unsaved=false; deadline_=now+15000;
            } else if(request.action==RenderAction::revert) {
                state_.selected=state_.active=request.extent;
                state_.unsaved=rollback_unsaved_; state_.confirming=false;
            } else { state_.unsaved=!saved; state_.confirming=false; }
        } else if(request.action==RenderAction::revert) {
            // A transient native boundary can refuse recovery without mutation.
            // Keep the unsaved preview and retry on the GL lane, once a second.
            deadline_=now+1000;
        }
        state_.error=std::move(error); state_.busy=false; in_flight_.reset();
    }
  private:
    void queue_decision(bool keep) {
        state_.busy=true;
        pending_=RenderRequest{++revision_,keep ? state_.active : rollback_,
            keep ? RenderAction::keep : RenderAction::revert};
    }
    mutable std::mutex mutex_;
    RenderSettingsSnapshot state_;
    std::optional<RenderRequest> pending_,in_flight_;
    uint64_t revision_=0;
    GuestExtent rollback_;
    bool rollback_unsaved_=false;
    int64_t deadline_=0;
};
} // namespace nova::platform
