#pragma once
#include "platform/native_device_profile.h"
#include "platform/guest_extent.h"
#include <memory>
#include <string>

namespace nova::camera { class NativeWorldFov; }
namespace nova::platform {
// Main GL lane only, strictly between guest steps. Own this before initializers
// and keep it alive through the image's lifetime; it retains profile admission.
// The caller stages Java dimensions and a replacement host FBO first, retaining
// their old values until this native transaction acknowledges success.
class NativeRenderExtent {
  public:
    NativeRenderExtent(const ElfImage &,const std::filesystem::path &,camera::NativeWorldFov &);
    ~NativeRenderExtent();
    NativeRenderExtent(const NativeRenderExtent &)=delete;
    NativeRenderExtent &operator=(const NativeRenderExtent &)=delete;
    const NativeDeviceProfile &profile() const;
    bool available() const;
    std::string unavailable_reason() const;
    GuestExtent current() const;
    struct ApplyResult { bool applied=false; std::string error; };
    // false => unchanged or positively verified restored old native dimensions.
    // Throws if recovery cannot be verified; never continue a partial mutation.
    ApplyResult apply(GuestExtent);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nova::platform
