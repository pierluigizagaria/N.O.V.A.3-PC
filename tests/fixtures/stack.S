// Original synthetic i386 ELF: deliberately no Windows stack probes.
.text
.p2align 4
.globl stack_unprobed
.type stack_unprobed,@function
stack_unprobed:
    pushl %ebp
    movl %esp,%ebp
    pushl %ebx
    pushl %esi
    subl $8,%esp
    movl 8(%ebp),%esi
    leal 8(%ebp),%eax
    andl $15,%eax
    movl %eax,8(%esi)
    movl %esp,%ebx
    movl %esp,%edx
    subl (%esi),%edx
    subl $4,%edx
    // Record the actual future stack target before the unprobed subtraction.
    subl $24,%esp
    movl %esi,(%esp)
    movl %ebx,4(%esp)
    movl %edx,8(%esp)
    call *4(%esi)
    addl $24,%esp
    subl (%esi),%esp
    call .Lframe_helper
    movl $1,12(%esi)
    leal -8(%ebp),%esp
    popl %esi
    popl %ebx
    popl %ebp
    ret
.Lframe_helper:
    xorps %xmm0,%xmm0
    movaps %xmm0,4(%esp)
    movl $0x51a7,%eax
    ret
.size stack_unprobed,.-stack_unprobed

.p2align 4
.globl stack_callback
.type stack_callback,@function
stack_callback:
    movl 8(%esp),%eax
    movl %eax,4(%esp)
    jmp stack_unprobed
.size stack_callback,.-stack_callback
.section .note.GNU-stack,"",@progbits
