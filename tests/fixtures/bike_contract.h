#ifndef BIKE_CONTRACT_H
#define BIKE_CONTRACT_H
struct BikeQueries { unsigned calls; unsigned actor,component,physics,base; };
struct BikeContract {
    unsigned state,enabled,move_enabled,primary_enabled,held,pressed,released;
    unsigned updates,base_calls,visual_calls,shots,events[4],transitions,cursor;
    float move_x,lean,forward,camera,template_scale,script_target;
    unsigned native_args[4];
    void (*after_event)(struct BikeContract *);
    float carry_scale;
    void (*after_base)(struct BikeContract *);
    void (*after_visual)(struct BikeContract *);
};
#endif
