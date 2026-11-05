/* Synthetic level-owned overlay; no game implementation or artwork. */
#include "draw_contract.h"
SpriteDrawState draw_state;
void draw_rush_site(DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord);
void draw_other_site(DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord,DrawWord);
void draw_paint_body(DrawWord sprite,DrawWord frame,DrawWord x,DrawWord y,
                     DrawWord flags,DrawWord sx,DrawWord sy,DrawWord alpha) {
    DrawWord args[8]={sprite,frame,x,y,flags,sx,sy,alpha};
    DrawWord n=draw_state.count++;
    if (n>=16) return;
    for (DrawWord i=0;i<8;++i) draw_state.arguments[n][i]=args[i];
    for (DrawWord i=0;i<4;++i) draw_state.clips[n][i]=draw_state.clip[i];
    draw_state.colors[n]=draw_state.color;
}
void draw_owner_body(DrawWord frame) {
    if (draw_state.enabled) {
        ++draw_state.percent_queries;
        int height=(int)(draw_state.stamina*100);
        if (height>0) {
            draw_state.clip[0]=0; draw_state.clip[1]=100-height;
            draw_state.clip[2]=1280; draw_state.clip[3]=100;
            draw_state.color=draw_state.active ? 0xffffdf65 : 0xffffffff;
            draw_rush_site(0x1111,frame,0x42580000,0x42c80000,3,0,0,0xff);
            /* Native owner cleanup must still execute after the patched CALL. */
            draw_state.color=0xffffffff;
            draw_state.clip[0]=draw_state.clip[1]=0;
            draw_state.clip[2]=1280; draw_state.clip[3]=720;
        }
    }
    /* Same sprite/frame through other sites, plus another sprite resource. */
    draw_other_site(0x1111,32,1,2,4,5,6,7);
    draw_other_site(0x1111,57,8,9,10,11,12,13);
    draw_other_site(0x2222,57,14,15,16,17,18,19);
}
