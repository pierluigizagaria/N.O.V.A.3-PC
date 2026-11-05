#pragma once
/* Independent synthetic ABI, with the observed input layout at its start.
   No game code, actor layout or resource is included in this fixture. */
struct ContractInput {
    float yaw_rate, yaw_rate_target, yaw_rate_step, yaw_target;
    unsigned char yaw_pending, padding[3];
    float pitch_target, pitch_delta, pitch_override;
};
struct ContractActor { float yaw, pitch; };
struct ContractObservation {
    unsigned frame, locked, milliseconds;
    float limit;
    float entry_yaw, entry_pitch, velocity_yaw, velocity_pitch;
    float effect_yaw_change, effect_pitch_change;
    unsigned yaw_calls, pitch_calls, nonzero_yaw_calls, nonzero_pitch_calls;
};
