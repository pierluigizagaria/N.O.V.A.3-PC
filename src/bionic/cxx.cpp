#include "bionic/imports.h"
#include <stdexcept>
namespace nova::bionic {
CxaRegistry *CxaRegistry::active_ = nullptr;
CxaRegistry::CxaRegistry(ImportRegistry &registry) {
    if (active_)
        throw std::runtime_error("Nested CxaRegistry scope");
    registry.function("__cxa_atexit", reinterpret_cast<const void *>(&record));
    active_ = this;
}
CxaRegistry::~CxaRegistry() { active_ = nullptr; }
int __cdecl CxaRegistry::record(uint32_t function, uint32_t argument, uint32_t dso) {
    if (!active_)
        return -1;
    try {
        std::lock_guard lock(active_->guard_);
        active_->entries_.push_back({function, argument, dso});
    } catch (...) {
        return -1;
    }
    return 0;
}
} // namespace nova::bionic
