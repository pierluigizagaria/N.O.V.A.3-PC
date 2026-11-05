#pragma once
#include "loader/imports.h"
#include <mutex>
#include <vector>
namespace nova::bionic {
void register_data(ImportRegistry &registry);
void register_pthread(ImportRegistry &registry);
void register_wchar(ImportRegistry &registry);
void register_time(ImportRegistry &registry);
void register_stdlib(ImportRegistry &registry);
void register_string(ImportRegistry &registry);

// Process-wide registrations are retained; guest destructors never run at shutdown.
// This scope must outlive guest execution. Clearing it never dereferences guest addresses.
class CxaRegistry {
  public:
    explicit CxaRegistry(ImportRegistry &registry);
    ~CxaRegistry();
    CxaRegistry(const CxaRegistry &) = delete;
    CxaRegistry &operator=(const CxaRegistry &) = delete;
    size_t size() const { return entries_.size(); }

  private:
    struct Entry {
        uint32_t function, argument, dso;
    };
    std::vector<Entry> entries_;
    std::mutex guard_;
    static CxaRegistry *active_;
    static int __cdecl record(uint32_t function, uint32_t argument, uint32_t dso);
};
} // namespace nova::bionic
