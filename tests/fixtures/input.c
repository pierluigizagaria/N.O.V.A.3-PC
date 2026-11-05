/* Synthetic JNI input receiver; no game bytes or implementation. */
typedef unsigned int u32;
struct env {
    u32 *slots;
};
u32 input_count, input_records[256][5], input_env, input_receiver;
char input_text[256];
int input_text_size, input_visible;
#define J(e, n, t) ((t)((e)->slots[n]))
void input_record(struct env *env, void *receiver, u32 type, u32 a, u32 b, u32 c, u32 d) {
    input_env = (u32)env;
    input_receiver = (u32)receiver;
    if (input_count < 256) {
        u32 *record = input_records[input_count++];
        record[0] = type;
        record[1] = a;
        record[2] = b;
        record[3] = c;
        record[4] = d;
    }
}
void input_touch(struct env *e, void *c, u32 action, u32 x, u32 y, u32 pointer) {
    input_record(e, c, 1, action, x, y, pointer);
}
void input_down(struct env *e, void *c, u32 key) { input_record(e, c, 2, key, 0, 0, 0); }
void input_up(struct env *e, void *c, u32 key) { input_record(e, c, 3, key, 0, 0, 0); }
void input_finish(struct env *e, void *c) {
    input_record(e, c, 4, 0, 0, 0, 0);
    void *method = J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(
        e, c, "GetKeyboardText", "()[B");
    void *bytes = J(e, 114, void *(*)(struct env *, void *, void *, ...))(e, c, method);
    input_text_size = J(e, 171, int (*)(struct env *, void *))(e, bytes);
    if (input_text_size >= 0 && input_text_size <= 255) {
        J(e, 200, void (*)(struct env *, void *, int, int, void *))(e, bytes, 0, input_text_size,
                                                                    input_text);
        input_text[input_text_size] = 0;
    }
    method = J(e, 113, void *(*)(struct env *, void *, const char *, const char *))(
        e, c, "IsKeyboardVisible", "()I");
    input_visible = J(e, 129, int (*)(struct env *, void *, void *, ...))(e, c, method);
}

/* Diagnostic-only accessors; the host runs the real native snapshot formatter. */
u32 snapshot_queries[4], snapshot_argument_errors;
u32 snapshot_control_body(u32 *scheme) {
    ++snapshot_queries[0];
    return scheme[1];
}
u32 snapshot_ammo_body(u32 *powers, u32 index) {
    ++snapshot_queries[1];
    if (index != 2 || powers[0x4c / 4] != index) ++snapshot_argument_errors;
    return 17;
}
u32 snapshot_can_use_body(u32 *powers, u32 grenade) {
    ++snapshot_queries[2];
    if (grenade || powers[0x4c / 4] != 2) ++snapshot_argument_errors;
    return 0xa5a50001; /* Native bool is in AL. */
}
u32 snapshot_power_type_body(u32 *powers, u32 index) {
    ++snapshot_queries[3];
    if (index != 2 || powers[0x4c / 4] != index) ++snapshot_argument_errors;
    return 4;
}

/* Normal owned input cleanup; no game implementation or external data. */
void normal_release_event_body(u32 *state,u32 type) {
    state[4+state[3]++]=type;
    if(type==30) state[0]=0;
    if(type==14) state[2]=0;
}
void normal_release_aim_body(u32 *state) {
    state[4+state[3]++]=99;
    state[1]=0;
}
