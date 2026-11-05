#pragma once
/* Original synthetic native-input contracts; no game code/data is copied. */
struct DriverContract {
    float raw_x, raw_y, scale_x, scale_y, gain;
    unsigned accelerated, one_shot, shooter, invert_native;
    float driver_distance, shooter_distance, yaw, pitch, pitch_minimum, pitch_maximum;
    float native_x, native_y, steering, throttle, brake;
    unsigned mode, axes, enabled, ingame, accelerator, brake_pedal, dt;
    int accelerator_timer, brake_timer;
    unsigned calls, pedal_calls, releases, events, exit_calls;
    unsigned mutate, mutate_owner, query;
};
void driver_camera_body(struct DriverContract *,unsigned);
void driver_pedals_body(struct DriverContract *);
void driver_event_body(struct DriverContract *,unsigned,float,float);
struct DriverDrawContract { unsigned type, draws, alpha; };
void driver_draw_body(struct DriverDrawContract *,unsigned);
