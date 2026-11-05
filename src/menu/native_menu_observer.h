#pragma once
#include "loader/elf_image.h"
#include "menu/rtt_backing_capture.h"
#include "menu/initial_target_scaling.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace nova::menu {
// Default-off diagnostic CPU join; optional product scaling reuses the same
// authenticated initial owner scopes. Public Begin observations remain separate.
class NativeMenuObserver {
public:
    static constexpr unsigned name_limit=96, record_limit=8;
    struct Record {
        enum class Kind : uint32_t { resource, selected, nominal_width, nominal_height, rtt } kind{};
        uint32_t manager=0, generation=0, invocation=0, receiver=0;
        int32_t width=0,height=0,native_width=0;
        uint32_t flag=0;
        uint64_t original_registers=0; // Opaque EDX:EAX, NOT a constructor object result.
        bool returned_normally=false;
        char name[name_limit]{};
    };
    struct Snapshot {
        bool enabled=false,terminal=false,complete=false;
        const char *reason="disabled";
        uint32_t count=0;
        // One attempt per returned selector or nominal-field acquisition, not bytes.
        uint32_t output_acquisitions=0;
        std::array<Record,record_limit> records{};
    };
    // Install/remove only with all guest execution quiescent, before initializers.
    explicit NativeMenuObserver(const ElfImage &,bool enabled=false,bool initial_scaling=false);
    // GL lane before first initial Load; ACTUAL allocated host extent and admitted limits.
    void configure_initial_targets(platform::GuestExtent,platform::RenderLimits) noexcept;
    ~NativeMenuObserver();
    NativeMenuObserver(const NativeMenuObserver &)=delete;
    NativeMenuObserver &operator=(const NativeMenuObserver &)=delete;
    RttBackingCapture::Snapshot backing_snapshot() const;
    Snapshot snapshot() const; // Copies host-owned facts only; no guest query.
    // Resource policy boundary, independent of diagnostic observation state.
    static bool loading() noexcept;
    // Deferred FILE output; call between original guest entries, never in a hook.
    // final=true terminates an incomplete observation, without running guest code.
    void report(FILE *,bool final=false);
#ifdef NOVA_MENU_OBSERVER_FIXTURE
    // Available ONLY in the separately compiled synthetic test executable.
    // Symbols are fixture-owned; this cannot authenticate or enable a real game.
    struct FixtureBindings {
        uint32_t width=0,flag=0,phase_offset=0x114;
        unsigned capacity=record_limit;
        InitialTargetScaling* lifecycle_scaling=nullptr; // Borrowed only by synthetic lifecycle tests.
    };
    NativeMenuObserver(const ElfImage &,FixtureBindings);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
