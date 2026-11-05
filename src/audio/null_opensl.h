#pragma once
#include "audio/opensl.h"
namespace nova::audio {
// T04 regression entry: retain the clocked null backend explicitly.
inline void register_null_opensl(ImportRegistry &registry) {
    configure({"null", {}});
    register_opensl(registry);
}
} // namespace nova::audio
