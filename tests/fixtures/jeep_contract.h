#pragma once
/* Original model of the evidenced jeep fire input contracts, not game data. */
struct JeepWeapon {
    unsigned firing, allowed[2], ammo[2], cooldown[2], shots[2], stops[2], messages[2], last_channel, last_messages, on_stop;
};
struct JeepAnimation { unsigned starts[2], stops[2], state, enabled, channel; };
struct JeepContract {
    unsigned held[2], pressed[2], released[2], events[33], resets, updates, enabled, shooter, secondary, halt, driver;
    struct JeepWeapon weapon;
    struct JeepAnimation animation;
};
void jeep_update_body(struct JeepContract *,unsigned);
void jeep_event_body(struct JeepContract *,unsigned);
void jeep_reset_body(struct JeepContract *);
void jeep_stop_body(struct JeepWeapon *,unsigned,unsigned);
void jeep_animation_body(struct JeepAnimation *);
