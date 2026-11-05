#pragma once
// Synthetic host/ELF contract only. It is NOT native ownership evidence.
struct MenuObserverFixture {
    unsigned char manager[0x118];
    unsigned char sprite[0xc0];
    unsigned rtt[2];
    unsigned sprite_manager;
    const char *input_name;
    const char *selected_name;
    unsigned callback; // Host cdecl void callback; injected adversarial boundary.
    unsigned callback_at; // 1=selector, 2=GetSprite width, 3=RTT0, 4=Load, 5=BeginRTT, 6=RTT D1.
    unsigned counts[14]; // C1,C2,D1,D2,Unload,Load,resource,select,width,height,RTT0,RTT1,BeginRTT,RTT D1
    unsigned arguments[14][7];
    unsigned native_width;
    unsigned char flag;
    unsigned nominal_width,nominal_height;
    unsigned suppress_rtt;
    unsigned last_low[6],last_high[6];
};
