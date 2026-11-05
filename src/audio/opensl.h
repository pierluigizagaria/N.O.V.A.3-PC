#pragma once
#include "loader/imports.h"
#include <filesystem>
#include <string>
namespace nova::audio {
struct Options {
    std::string backend = "sdl";
    std::filesystem::path dump;
};
struct Statistics {
    uint64_t players = 0, enqueued = 0, callbacks = 0, underruns = 0, fallbacks = 0,
             submitted_bytes = 0;
    uint64_t callback_enqueues = 0;
    uint32_t max_queued_bytes = 0, active_device = 0;
};
// Configure on the UI thread before entering the guest. No device is needed for null.
void configure(const Options &options);
void register_opensl(ImportRegistry &registry);
void device_removed(uint32_t device);
// Playback physical-device availability hint; recovery remains on the player worker.
void device_added(uint32_t device);
Statistics statistics();
void shutdown();
} // namespace nova::audio
