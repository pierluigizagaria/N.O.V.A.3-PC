.text
/* Every bridge uses MOVAPS at an aligned outgoing stack. These are authored
   fixture prologues that intentionally match the installer's checked entries. */
.macro simple name,body,nargs=2
.p2align 4
.globl \name
.type \name,@function
\name:
 pushl %ebp
 movl %esp,%ebp
 subl $24,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
.if \nargs == 4
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
 movl 20(%ebp),%eax
 movl %eax,12(%esp)
.endif
 call \body
 leave
 ret
.size \name,.-\name
.endm
simple fixture_consume,fixture_consume_body
/* The D0 one-argument entry must not read a nonexistent second argument. */
.p2align 4
.globl _ZN15PlayerComponentD0Ev
.type _ZN15PlayerComponentD0Ev,@function
_ZN15PlayerComponentD0Ev:
 pushl %ebp
 movl %esp,%ebp
 subl $24,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 movl $0,4(%esp)
 call fixture_lifetime_body
 leave
 ret
.size _ZN15PlayerComponentD0Ev,.-_ZN15PlayerComponentD0Ev
.macro saved name,ctor=0
.p2align 4
.globl \name
.type \name,@function
\name:
 pushl %ebp
 movl %esp,%ebp
 pushl %edi
 pushl %esi
 pushl %ebx
 subl $28,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 xorl %eax,%eax
.if \ctor
 movl 12(%ebp),%eax
.endif
 movl %eax,4(%esp)
 call fixture_lifetime_body
 addl $28,%esp
 popl %ebx
 popl %esi
 popl %edi
 popl %ebp
 ret
.size \name,.-\name
.endm
saved _ZN15PlayerComponentC1EP11CGameObject,1
saved _ZN15PlayerComponentC2EP11CGameObject,1
saved _ZN15PlayerComponentD1Ev
saved _ZN15PlayerComponentD2Ev
.p2align 4
.globl _ZN15PlayerComponent14ResetComponentEv
.type _ZN15PlayerComponent14ResetComponentEv,@function
_ZN15PlayerComponent14ResetComponentEv:
 pushl %ebp
 movl %esp,%ebp
 andl $-16,%esp
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 movl $0,4(%esp)
 call fixture_lifetime_body
 leave
 ret
.size _ZN15PlayerComponent14ResetComponentEv,.-_ZN15PlayerComponent14ResetComponentEv


.p2align 4
.globl _ZN15PlayerComponent6EnableEb
.type _ZN15PlayerComponent6EnableEb,@function
_ZN15PlayerComponent6EnableEb:
 pushl %ebp
 movl %esp,%ebp
 andl $-16,%esp
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 call fixture_enable_body
 leave
 ret
.size _ZN15PlayerComponent6EnableEb,.-_ZN15PlayerComponent6EnableEb

/* Checked caller site is exactly +0x1d2. Only this authored ordinary caller
   goes through the installed production GuestCall; the foreign drain does not. */
.p2align 4
.globl _ZN11Application7_UpdateEii
.type _ZN11Application7_UpdateEii,@function
_ZN11Application7_UpdateEii:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %edi
 subl $16,%esp
 call fixture_before_drain
 movl %eax,%esi
 movl 8(%ebp),%eax
 movl %eax,(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 .org _ZN11Application7_UpdateEii+0x1d2,0x90
 call _ZN12EventManager12UpdateEventsEi
 movl %esi,(%esp)
 movl 12(%ebp),%edx
 movl %edx,4(%esp)
 movl %eax,8(%esp)
 call fixture_after_drain
 addl $16,%esp
 popl %edi
 popl %esi
 popl %ebp
 ret
.size _ZN11Application7_UpdateEii,.-_ZN11Application7_UpdateEii
.section .note.GNU-stack,"",@progbits
