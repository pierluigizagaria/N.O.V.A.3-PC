#ifndef NOVA_DRAW_CONTRACT_H
#define NOVA_DRAW_CONTRACT_H
typedef unsigned int DrawWord;
typedef struct SpriteDrawState {
    DrawWord enabled, allowed, active;
    float stamina;
    DrawWord percent_queries, color, clip[4], count;
    DrawWord arguments[16][8], colors[16], clips[16][4];
} SpriteDrawState;
#endif
