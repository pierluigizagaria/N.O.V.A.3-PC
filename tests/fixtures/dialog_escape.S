/* Synthetic call-site and alignment contracts, no game implementation. */
.text
.macro aligned
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
.endm
.globl _ZN12CMenuManager7onEventEPK6IEventPK12EventManager
.type _ZN12CMenuManager7onEventEPK6IEventPK12EventManager,@function
_ZN12CMenuManager7onEventEPK6IEventPK12EventManager:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    jmp 1f
    .org _ZN12CMenuManager7onEventEPK6IEventPK12EventManager+0x668,0x90
1:  call _ZN11CMenuScreen13PushYesButtonEv
    leave
    ret
.size _ZN12CMenuManager7onEventEPK6IEventPK12EventManager,.-_ZN12CMenuManager7onEventEPK6IEventPK12EventManager

.globl _ZN10GS_Confirm7onEventEPK6IEventPK12EventManager
.type _ZN10GS_Confirm7onEventEPK6IEventPK12EventManager,@function
_ZN10GS_Confirm7onEventEPK6IEventPK12EventManager:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl $1,4(%esp)
    jmp 2f
    .org _ZN10GS_Confirm7onEventEPK6IEventPK12EventManager+0xc0,0x90
2:  call _ZN12CMenuButtons9GetButtonEi
    leave
    ret
.size _ZN10GS_Confirm7onEventEPK6IEventPK12EventManager,.-_ZN10GS_Confirm7onEventEPK6IEventPK12EventManager

.macro answer name, value
.globl \name
.type \name,@function
\name:
    subl $28,%esp
    aligned
    movl $\value,%eax
    addl $28,%esp
    ret
.size \name,.-\name
.endm
answer _ZN11CMenuScreen13PushYesButtonEv,1
answer _ZN11CMenuScreen12PushNoButtonEv,0
.globl _ZN12CMenuButtons9GetButtonEi
.type _ZN12CMenuButtons9GetButtonEi,@function
_ZN12CMenuButtons9GetButtonEi:
    subl $28,%esp
    aligned
    movl 36(%esp),%eax
    addl $28,%esp
    ret
.size _ZN12CMenuButtons9GetButtonEi,.-_ZN12CMenuButtons9GetButtonEi
.globl _ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager
.type _ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager,@function
_ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager:
    pushl %ebp
    movl %esp,%ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    subl $28,%esp
    aligned
    movl 8(%ebp),%edx
    movl 12(%ebp),%ecx
    movl 4(%ecx),%eax
    movl %eax,(%edx)
    movl 8(%ecx),%eax
    movl %eax,4(%edx)
    movl 12(%ecx),%eax
    movl %eax,8(%edx)
    movl 16(%ebp),%eax
    movl %eax,12(%edx)
    addl $28,%esp
    popl %ebx
    popl %esi
    popl %edi
    popl %ebp
    ret
.size _ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager,.-_ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager
.section .note.GNU-stack,"",@progbits
