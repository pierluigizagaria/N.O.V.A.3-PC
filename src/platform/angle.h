#pragma once
#include "gles/runtime.h"
#include "platform/render_catalogue.h"
#include <memory>
#include <string>
#include <vector>
namespace nova::platform {
class Angle {
  public:
    explicit Angle(const std::string &backend);
    ~Angle();
    Angle(const Angle &) = delete;
    Angle &operator=(const Angle &) = delete;
    int create(void *window, int width, int height, int extra_contexts,
               const NativeDeviceProfile *profile=nullptr);
    bool make_current(int index);
    int current_index() const;
    // GL-thread only. This changes presentation, not the guest target/profile.
    void presentation_size(int width,int height);
    void present(gles::PresentationOverlay overlay=nullptr,void *user=nullptr);
    void capture(const std::filesystem::path &path);
    gles::Context &main_target();
    RenderLimits render_limits() const { return limits_; }
    class TargetChange {
      public:
        ~TargetChange(); // GL lane/current primary context throughout its lifetime.
        void activate();
        void rollback();
        void commit();
        TargetChange(const TargetChange &)=delete;
        TargetChange &operator=(const TargetChange &)=delete;
      private:
        friend class Angle;
        explicit TargetChange(Angle &owner):owner_(&owner) {}
        void swap();
        Angle *owner_;
        gles::Context retained_;
        bool active_=false;
    };
    std::unique_ptr<TargetChange> prepare_target(int width,int height,
                                                const NativeDeviceProfile *profile=nullptr);
    bool target_matches(int width,int height);

  private:
    struct Slot {
        EGLContext context = EGL_NO_CONTEXT;
        EGLSurface surface = EGL_NO_SURFACE;
        gles::Context target;
    };
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    std::vector<std::unique_ptr<Slot>> slots_;
    int output_width_=0,output_height_=0;
    RenderLimits limits_;
};
} // namespace nova::platform
