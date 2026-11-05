.globl host_data
.type host_data,@object
.text
.p2align 4
.globl alignment_probe
.type alignment_probe,@function
alignment_probe:
    movl %esp, %eax
    andl $15, %eax
    subl $28, %esp
    xorps %xmm0, %xmm0
    movaps %xmm0, (%esp)
    addl $28, %esp
    ret
.size alignment_probe, .-alignment_probe

.globl once_callback
.type once_callback,@function
once_callback:
    subl $28, %esp
    xorps %xmm0, %xmm0
    movaps %xmm0, (%esp)
    addl $28, %esp
    call 1f
1:  popl %eax
    addl $_GLOBAL_OFFSET_TABLE_+(.-1b), %eax
    movl once_count@GOT(%eax), %edx
    lock incl (%edx)
    ret
.size once_callback, .-once_callback

.globl crash_site
.type crash_site,@function
crash_site:
    movl $42, 1
    ret
.size crash_site, .-crash_site
.section .note.GNU-stack,"",@progbits

.globl absolute_export
.type absolute_export,@object
.set absolute_export, 0x100
