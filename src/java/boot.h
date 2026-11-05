#pragma once
#include "jni/runtime.h"
#include "platform/guest_extent.h"
#include <memory>
namespace nova::java {
void register_boot(jni::Runtime &runtime, platform::GuestExtent extent={},
                   std::shared_ptr<platform::GuestExtentState> mutable_extent={});
void startup(jni::Runtime &runtime);
// Provisional offline host-cache renewal; does not change the response identity.
std::string installer_host_validity(const std::string &response, const std::string &validity,
                                    int64_t now);
} // namespace nova::java
