/* Synthetic caller offsets and a float-setting consumer with alignment checks. */
.text
.macro producer name, offset
.globl \name
.type \name,@function
\name:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl 12(%ebp),%eax
    movl %eax,4(%esp)
    jmp 1f
    .org \name+\offset,0x90
1:  call _ZN21CCustomColladaFactory16SetLodsDistScaleEf
    leave
    ret
.size \name,.-\name
.endm
producer _ZN6CLevel7PreInitEi,0x11c
producer _ZN6CLevel6UpdateEv,0x24ee
producer _ZN6CLevel16SetLodsDistScaleEf,0x22

.globl _ZN21CCustomColladaFactory16SetLodsDistScaleEf
.type _ZN21CCustomColladaFactory16SetLodsDistScaleEf,@function
_ZN21CCustomColladaFactory16SetLodsDistScaleEf:
    subl $28,%esp
    xorps %xmm1,%xmm1
    movaps %xmm1,(%esp)
    movl 32(%esp),%eax
    movss 36(%esp),%xmm0
    mulss %xmm0,%xmm0
    movss %xmm0,8(%eax)
    addl $28,%esp
    ret
.size _ZN21CCustomColladaFactory16SetLodsDistScaleEf,.-_ZN21CCustomColladaFactory16SetLodsDistScaleEf
