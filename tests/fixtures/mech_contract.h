#pragma once
#include "look_contract.h"
/* Original synthetic state, not a game component or copied game implementation. */
struct MechContract {
    struct ContractInput look;
    struct ContractActor actor;
    struct ContractObservation seen;
    unsigned held[2], pressed[2], released[2], allowed[2], firing[2];
    unsigned events[65], shots[2], stops[2], messages[2], resets;
    unsigned last_channel, last_message_argument;
    float movement[2];
};
struct MechDrawControl { unsigned enabled, visible, pressed, sets; };
