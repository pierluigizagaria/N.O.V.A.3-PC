/* Synthetic eight-word sprite call, including an aligned guest re-entry. */
.text
.globl _ZN7GS_Logo6RenderEv
.type _ZN7GS_Logo6RenderEv,@function
_ZN7GS_Logo6RenderEv:
    pushl %ebp
    movl %esp,%ebp
    subl $40,%esp
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl $17,4(%esp)
    movl $0x41200000,8(%esp)
    movl $0xc1200000,12(%esp)
    movl $31,16(%esp)
    movl $0x3f800000,20(%esp)
    movl $0x40000000,24(%esp)
    movl $200,28(%esp)
    jmp 1f
    .org _ZN7GS_Logo6RenderEv+0xc6,0x90
1:  call _ZNK7CSprite10PaintFrameEiffjffh
    leave
    ret
.size _ZN7GS_Logo6RenderEv,.-_ZN7GS_Logo6RenderEv

.globl _ZNK7CSprite10PaintFrameEiffjffh
.type _ZNK7CSprite10PaintFrameEiffjffh,@function
_ZNK7CSprite10PaintFrameEiffjffh:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    movl 8(%ebp),%edx
    movl $0,%ecx
2:  movl 12(%ebp,%ecx,4),%eax
    movl %eax,(%edx,%ecx,4)
    incl %ecx
    cmpl $7,%ecx
    jne 2b
    movl $0xabcdef01,%eax
    leave
    ret
.size _ZNK7CSprite10PaintFrameEiffjffh,.-_ZNK7CSprite10PaintFrameEiffjffh
