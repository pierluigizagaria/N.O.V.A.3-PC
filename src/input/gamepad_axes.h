#pragma once
namespace nova::input {
// Dimensionless stick positions. Persistent state, never mouse counts or an
// already-integrated angle. Native rate/time consumption is a separate contract.
struct GamepadAxes {
    float move_x=0,move_y=0,look_x=0,look_y=0;
};
}
