/* Authored synthetic ABI fixture; no original game implementation. */
.text
.p2align 4
.globl _ZN11Application14EncryptAndSaveEPKciP13CMemoryStream
.type _ZN11Application14EncryptAndSaveEPKciP13CMemoryStream,@function
_ZN11Application14EncryptAndSaveEPKciP13CMemoryStream:
    pushl %ebp
    movl %esp,%ebp
    subl $0x48,%esp
    leave
    jmp fixture_save_body
.size _ZN11Application14EncryptAndSaveEPKciP13CMemoryStream,.-_ZN11Application14EncryptAndSaveEPKciP13CMemoryStream
.p2align 4
.globl _ZN11Application14DecryptAndLoadEPKciP13CMemoryStream
.type _ZN11Application14DecryptAndLoadEPKciP13CMemoryStream,@function
_ZN11Application14DecryptAndLoadEPKciP13CMemoryStream:
    pushl %ebp
    movl %esp,%ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    popl %ebx
    popl %esi
    popl %edi
    leave
fixture_save_body:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    movl 4(%esp),%edx
    incl (%edx)
    movl 8(%esp),%eax
    movl %eax,4(%edx)
    movl 12(%esp),%eax
    movl %eax,8(%edx)
    movl 16(%esp),%eax
    movl %eax,12(%edx)
    movl 16(%edx),%eax
    ret
.size _ZN11Application14DecryptAndLoadEPKciP13CMemoryStream,.-_ZN11Application14DecryptAndLoadEPKciP13CMemoryStream
.section .note.GNU-stack,"",@progbits
