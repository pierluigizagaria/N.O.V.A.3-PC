#pragma once
#include "loader/elf_image.h"
#include <filesystem>
#include <cstdint>

namespace nova::platform {
// T11d compatibility policy: preserve the caller's CPU/MEM selections when the
// actual render width is1920. No screen dimension, profile value or option is
// fabricated. Install/remove only while all guest execution is quiescent.
// Keep the mapped image alive until this component has been destroyed.
// Admission hashes its immutable loaded source bytes; exact_library is a
// separately checked caller prerequisite, never an authority for another image.
// This component is deliberately not activated by play.cpp yet.
class NativeDeviceProfile {
  public:
    NativeDeviceProfile(const ElfImage &,const std::filesystem::path &exact_library);
    ~NativeDeviceProfile();
    NativeDeviceProfile(const NativeDeviceProfile &)=delete;
    NativeDeviceProfile &operator=(const NativeDeviceProfile &)=delete;
    bool applied() const { return target_!=0; }
  private:
    uint32_t target_=0;
};
} // namespace nova::platform
