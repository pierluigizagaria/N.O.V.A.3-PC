#pragma once
#include "platform/fov_panel.h"
#include "movement_wobble/integration_gate.h"
namespace nova::platform {
// UI owner after panel command handoff, outside native callbacks. This is the
// production request dispatch used by play; persistence remains in its caller.
// False means no movement command was applied, including inactive capability.
inline bool dispatch_movement_wobble_request(movement_wobble::RequestedStrength& requested,
                                             const FovCommands& command) {
    if constexpr(movement_wobble::kControlAvailable) {
        if(command.movement_wobble) {
            requested.set(*command.movement_wobble);
            return true;
        }
    }
    return false;
}
}
