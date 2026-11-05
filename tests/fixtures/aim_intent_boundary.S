.text
.macro entry name,body
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
 call \body
 leave
 ret
.size \name,.-\name
.endm
entry aim_fixture_update,aim_fixture_update_body
entry aim_fixture_unaim,aim_fixture_unaim_body
entry aim_fixture_force,aim_fixture_force_body
entry aim_fixture_enable,aim_fixture_enable_body
entry aim_fixture_reload,aim_fixture_reload_body
.p2align 4
.globl aim_fixture_publish
.type aim_fixture_publish,@function
aim_fixture_publish:
 pushl %ebp
 movl %esp,%ebp
 subl $24,%esp
 movl 8(%ebp),%eax
 movl $0,(%eax)
 movl $1,4(%eax)
.globl aim_fixture_publish_site
aim_fixture_publish_site:
 call aim_fixture_completion
 leave
 ret
.size aim_fixture_publish,.-aim_fixture_publish
.p2align 4
.globl aim_fixture_completion
.type aim_fixture_completion,@function
aim_fixture_completion:
 pushl %ebp
 movl %esp,%ebp
 subl $24,%esp
 movaps %xmm0,(%esp)
 movl $42,%eax
 leave
 ret
.size aim_fixture_completion,.-aim_fixture_completion
.p2align 4
.globl aim_fixture_stop
.type aim_fixture_stop,@function
aim_fixture_stop:
 pushl %ebp
 movl %esp,%ebp
 subl $24,%esp
 movaps %xmm0,(%esp)
 movl $43,%eax
 leave
 ret
.size aim_fixture_stop,.-aim_fixture_stop
.section .note.GNU-stack,"",@progbits

.text
.macro checked name,target
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
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
.globl \name\()_site
\name\()_site:
 call \target
 leave
 ret
.size \name,.-\name
.endm
checked held_can_start,held_can_start_body
checked held_exit,held_exit_body
checked held_toggle,held_switch
entry held_update,held_update_body
entry held_interrupt,held_interrupt_body
entry held_enable,held_enable_body
checked held_query_touch,held_query_touch_body
checked held_hud_query,held_hud_body
checked held_main_state,held_main_state_body
checked held_reload_exit,held_exit_body
entry held_reload_begin,held_reload_begin_body
entry held_reload_publish,held_reload_publish_body
entry held_reload_commit,held_reload_commit_body
entry held_reload_end,held_reload_end_body
checked held_reload_restore,held_reload_restore_body
.section .note.GNU-stack,"",@progbits
