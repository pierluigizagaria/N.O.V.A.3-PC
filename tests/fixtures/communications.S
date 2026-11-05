.text
.globl _ZN13CButtonIGChat7PressedEv
.type _ZN13CButtonIGChat7PressedEv,@function
_ZN13CButtonIGChat7PressedEv:
    push %ebp
    xor %eax,%eax
    mov %esp,%ebp
    mov 8(%ebp),%ecx
    cmpb $0,0x1d(%ecx)
    je 1f
    movzbl 0x1c(%ecx),%eax
    orb 0x20(%ecx),%al
    setne %al
1:  pop %ebp
    ret
.size _ZN13CButtonIGChat7PressedEv,.-_ZN13CButtonIGChat7PressedEv
/* Synthetic draw witnesses: side effects, label visibility and both raster sites. */
.globl _ZN13CButtonIGChat4DrawEi
.type _ZN13CButtonIGChat4DrawEi,@function
_ZN13CButtonIGChat4DrawEi:
    push %ebp
    mov %esp,%ebp
    push %edi
    push %esi
    push %ebx
    sub $44,%esp
    mov 8(%ebp),%esi
    incl 0xa4(%esi)
    mov 0x80(%esi),%eax
    cmpl $0,0x88(%esi)
    je 2f
    incl 4(%eax)
2:  xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    movaps %xmm0,16(%esp)
    mov %eax,(%esp)
    jmp 3f
    .org _ZN13CButtonIGChat4DrawEi+0x113,0x90
3:  call _ZNK7CSprite10PaintFrameEiffjffh
    jmp 4f
    .org _ZN13CButtonIGChat4DrawEi+0x2c6,0x90
4:  call _ZNK7CSprite10PaintFrameEiffjffh
    add $44,%esp
    pop %ebx
    pop %esi
    pop %edi
    pop %ebp
    ret
.size _ZN13CButtonIGChat4DrawEi,.-_ZN13CButtonIGChat4DrawEi
.globl _ZNK7CSprite10PaintFrameEiffjffh
.type _ZNK7CSprite10PaintFrameEiffjffh,@function
_ZNK7CSprite10PaintFrameEiffjffh:
    mov 4(%esp),%eax
    incl (%eax)
    ret
.size _ZNK7CSprite10PaintFrameEiffjffh,.-_ZNK7CSprite10PaintFrameEiffjffh
.globl _ZN10CButtonSpr4DrawEi
.type _ZN10CButtonSpr4DrawEi,@function
_ZN10CButtonSpr4DrawEi:
    push %ebp
    mov %esp,%ebp
    push %edi
    push %esi
    push %ebx
    mov 8(%ebp),%eax
    mov 0x80(%eax),%eax
    incl (%eax)
    incl 4(%eax)
    pop %ebx
    pop %esi
    pop %edi
    pop %ebp
    ret
.size _ZN10CButtonSpr4DrawEi,.-_ZN10CButtonSpr4DrawEi
.section .note.GNU-stack,"",@progbits
