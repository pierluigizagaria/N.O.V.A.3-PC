#pragma once
/* Authored native-layout storage and scheduling contract. No game code embedded. */
struct PostDrainEvent { unsigned table,type; float x,y; };
struct PostDrainPlayer {
    unsigned char native[0x400];
    unsigned poison,obsolete,events,consumers,last_dt,lifetimes,look_calls;
    float last_x,last_y;
};
struct PostDrainWorld {
    unsigned level,player,app,state,manager;
    unsigned drains,drain_dt,immediate,queued,queue_count,acquisitions;
    unsigned event_callback,after_drain_callback,getter_callback,reset_observer,event_observer;
    unsigned callback_type,queue_host,stale_caller,forwarded_result;
    unsigned effects_calls,mp_calls,state_calls,foreign_calls;
    unsigned native_event_entry,native_consumer_entry,native_caller_entry,native_later_writer,velocity_samples,velocity_dt;
    float consumed_move_x,consumed_move_y;
    struct PostDrainEvent queue[16];
};
