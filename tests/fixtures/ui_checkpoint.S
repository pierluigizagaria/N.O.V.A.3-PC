/* Synthetic machine-state contract for the post-parent menu checkpoint. */
.text
.globl checkpoint_probe
.type checkpoint_probe,@function
checkpoint_probe:
    push %ebp
    mov %esp,%ebp
    push %ebx
    push %esi
    push %edi
    mov 8(%ebp),%ebx
    mov $0x11223344,%ecx
    mov $0x55667788,%edx
    mov $0x12345678,%esi
    mov $0x76543210,%edi
    movss 0x104(%ebx),%xmm0
    fld1
    stc
    .org checkpoint_probe+64,0x90
    mov 0x100(%ebx),%eax
    setc (%ebx)
    mov %ecx,4(%ebx)
    mov %edx,8(%ebx)
    mov %esi,12(%ebx)
    mov %edi,16(%ebx)
    movss %xmm0,20(%ebx)
    fstps 24(%ebx)
    pop %edi
    pop %esi
    pop %ebx
    leave
    ret
.size checkpoint_probe,.-checkpoint_probe
