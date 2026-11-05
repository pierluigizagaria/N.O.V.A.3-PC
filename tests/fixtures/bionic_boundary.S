.text
.p2align 4
.globl aligned_thread
.type aligned_thread,@function
aligned_thread:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp thread_body
.size aligned_thread,.-aligned_thread
.globl aligned_destructor
.type aligned_destructor,@function
aligned_destructor:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp destructor_body
.size aligned_destructor,.-aligned_destructor
.globl aligned_compare
.type aligned_compare,@function
aligned_compare:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    movl 4(%esp),%eax
    movl 8(%esp),%edx
    movl (%eax),%eax
    subl (%edx),%eax
    ret
.size aligned_compare,.-aligned_compare
.globl aligned_native_long
.type aligned_native_long,@function
aligned_native_long:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp native_long_body
.size aligned_native_long,.-aligned_native_long
.globl aligned_native_float
.type aligned_native_float,@function
aligned_native_float:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp native_float_body
.size aligned_native_float,.-aligned_native_float
.globl aligned_native_double
.type aligned_native_double,@function
aligned_native_double:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp native_double_body
.size aligned_native_double,.-aligned_native_double
.globl aligned_native_object
.type aligned_native_object,@function
aligned_native_object:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp native_object_body
.size aligned_native_object,.-aligned_native_object
.section .note.GNU-stack,"",@progbits
