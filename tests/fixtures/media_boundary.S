.text
.p2align 4
.globl aligned_audio_callback
.type aligned_audio_callback,@function
aligned_audio_callback:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp audio_callback_body
.size aligned_audio_callback,.-aligned_audio_callback
.section .note.GNU-stack,"",@progbits

.text
.p2align 4
.globl aligned_stream_callback
.type aligned_stream_callback,@function
aligned_stream_callback:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp stream_callback_body
.size aligned_stream_callback,.-aligned_stream_callback
.p2align 4
.globl aligned_stream_destructor
.type aligned_stream_destructor,@function
aligned_stream_destructor:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp stream_destructor_body
.size aligned_stream_destructor,.-aligned_stream_destructor
