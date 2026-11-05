/* Independently written behavioral model of the bounded native look consumer.
   See tests/look_contract.md for verified branches and deliberate exclusions. */
#include "look_contract.h"
static float absolute(float value) { return value < 0 ? -value : value; }
static void turn_yaw(struct ContractActor *actor, struct ContractObservation *seen, float delta) {
    ++seen->yaw_calls;
    seen->nonzero_yaw_calls += delta != 0;
    actor->yaw += delta;
}
static void turn_pitch(struct ContractActor *actor, struct ContractObservation *seen, float delta) {
    ++seen->pitch_calls;
    seen->nonzero_pitch_calls += delta != 0;
    actor->pitch += delta;
}
void contract_consume_body(struct ContractInput *input, struct ContractActor *actor,
                           struct ContractObservation *seen) {
    seen->entry_yaw = actor->yaw;
    seen->entry_pitch = actor->pitch;
    seen->yaw_calls = seen->pitch_calls = 0;
    seen->nonzero_yaw_calls = seen->nonzero_pitch_calls = 0;
    if (!seen->locked) {
        if (input->yaw_pending) {
            turn_yaw(actor,seen,input->yaw_target-actor->yaw);
            input->yaw_pending = 0;
        }
        if (input->yaw_rate != input->yaw_rate_target) {
            input->yaw_rate += input->yaw_rate_step;
            if (absolute(input->yaw_rate-input->yaw_rate_target) < .001f)
                input->yaw_rate = input->yaw_rate_target;
        }
        if (input->pitch_override != 0) {
            // Priority input is persistent, and this branch applies native limits.
            turn_pitch(actor,seen,input->pitch_override);
            if (actor->pitch < -seen->limit) turn_pitch(actor,seen,-seen->limit-actor->pitch);
            if (actor->pitch > seen->limit) turn_pitch(actor,seen,seen->limit-actor->pitch);
        } else if (input->pitch_delta > 0 && input->pitch_target > actor->pitch) {
            turn_pitch(actor,seen,input->pitch_delta);
            if (actor->pitch >= input->pitch_target) {
                turn_pitch(actor,seen,input->pitch_target-actor->pitch);
                input->pitch_delta = 0;
            }
        } else if (input->pitch_delta < 0 && input->pitch_target < actor->pitch) {
            turn_pitch(actor,seen,input->pitch_delta);
            if (actor->pitch <= input->pitch_target) {
                turn_pitch(actor,seen,input->pitch_target-actor->pitch);
                input->pitch_delta = 0;
            }
        }
        turn_yaw(actor,seen,-input->yaw_rate*(float)seen->milliseconds*.02f);
    }
    // Observe the same point used by subsequent velocity/effect work. We do
    // not implement physics or activate an actual camera/postprocessing effect.
    seen->velocity_yaw = actor->yaw;
    seen->velocity_pitch = actor->pitch;
    seen->effect_yaw_change = actor->yaw-seen->entry_yaw;
    seen->effect_pitch_change = actor->pitch-seen->entry_pitch;
}
