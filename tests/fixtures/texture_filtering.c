/* Independent synthetic sampler publisher; no game code. */
typedef unsigned int u32;
extern void glTexParameteri(u32,u32,int);
extern void glTexParameterf(u32,u32,float);
void fixture_texture_parameters(u32 *texture) {
    unsigned char *state=(unsigned char *)texture[4];
    u32 flags=*(u32 *)(state+0x24);
    unsigned short dirty=*(unsigned short *)(state+0x2a);
    static const u32 filters[]={0x2600,0x2601,0x2700,0x2701,0x2702,0x2703};
    static const u32 targets[]={0xde0,0xde1,0x806f,0x8513};
    u32 target=targets[flags&7];
    if(dirty&8) glTexParameteri(target,0x2801,filters[(flags>>14)&7]);
    unsigned char *driver=*(unsigned char **)(state+0x14);
    if((dirty&0x100) && (driver[0xe7]&1)) {
        float requested=*(float *)(state+0x30),limit=*(float *)(driver+0x548);
        glTexParameterf(target,0x84fe,requested<limit?requested:limit);
    }
    *(unsigned short *)(state+0x2a)=dirty&0xc007;
}
