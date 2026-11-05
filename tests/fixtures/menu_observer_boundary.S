.text
# Original synthetic boundaries. Production prologue shapes, not game bodies.

.p2align 4
.globl obs_c1
.type obs_c1,@function
obs_c1:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_c1_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_c1,.-obs_c1

.p2align 4
.globl obs_c2
.type obs_c2,@function
obs_c2:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_c2_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_c2,.-obs_c2

.p2align 4
.globl obs_d1
.type obs_d1,@function
obs_d1:
 pushl %ebp
 movl %esp,%ebp
 pushl %edi
 pushl %esi
 pushl %ebx
 subl $44,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_d1_body
 addl $44,%esp
 popl %ebx
 popl %esi
 popl %edi
 popl %ebp
 ret
.size obs_d1,.-obs_d1

.p2align 4
.globl obs_d2
.type obs_d2,@function
obs_d2:
 pushl %ebp
 movl %esp,%ebp
 pushl %edi
 pushl %esi
 pushl %ebx
 subl $44,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_d2_body
 addl $44,%esp
 popl %ebx
 popl %esi
 popl %edi
 popl %ebp
 ret
.size obs_d2,.-obs_d2

.p2align 4
.globl obs_unload
.type obs_unload,@function
obs_unload:
 pushl %ebp
 movl %esp,%ebp
 pushl %edi
 pushl %esi
 pushl %ebx
 subl $44,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_unload_body
 addl $44,%esp
 popl %ebx
 popl %esi
 popl %edi
 popl %ebp
 ret
.size obs_unload,.-obs_unload

.p2align 4
.globl obs_load
.type obs_load,@function
obs_load:
 pushl %ebp
 movl %esp,%ebp
 subl $168,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 call obs_load_body
 leave
 ret
.size obs_load,.-obs_load

.p2align 4
.globl obs_resource
.type obs_resource,@function
obs_resource:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
 movl 20(%ebp),%eax
 movl %eax,12(%esp)
 movl 24(%ebp),%eax
 movl %eax,16(%esp)
 movl 28(%ebp),%eax
 movl %eax,20(%esp)
.globl obs_resource_site
obs_resource_site:
 call obs_resource_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_resource,.-obs_resource

.p2align 4
.globl obs_select
.type obs_select,@function
obs_select:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
 movl 20(%ebp),%eax
 movl %eax,12(%esp)
 movl 24(%ebp),%eax
 movl %eax,16(%esp)
 movl 28(%ebp),%eax
 movl %eax,20(%esp)
 movl 32(%ebp),%eax
 movl %eax,24(%esp)
.globl obs_select_site
obs_select_site:
 call obs_select_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_select,.-obs_select

.p2align 4
.globl obs_width
.type obs_width,@function
obs_width:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
.globl obs_width_site
obs_width_site:
 call obs_width_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_width,.-obs_width

.p2align 4
.globl obs_height
.type obs_height,@function
obs_height:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
.globl obs_height_site
obs_height_site:
 call obs_height_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_height,.-obs_height

.p2align 4
.globl obs_rtt0
.type obs_rtt0,@function
obs_rtt0:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
.globl obs_rtt0_site
obs_rtt0_site:
 call obs_rtt0_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_rtt0,.-obs_rtt0

.p2align 4
.globl obs_rtt1
.type obs_rtt1,@function
obs_rtt1:
 pushl %ebp
 movl %esp,%ebp
 pushl %esi
 pushl %ebx
 subl $32,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 movl 16(%ebp),%eax
 movl %eax,8(%esp)
.globl obs_rtt1_site
obs_rtt1_site:
 call obs_rtt1_body
 addl $32,%esp
 popl %ebx
 popl %esi
 popl %ebp
 ret
.size obs_rtt1,.-obs_rtt1


# Exact newly authenticated prologue shapes, with alignment probes after entry.
.p2align 4
.globl obs_begin
.type obs_begin,@function
obs_begin:
 pushl %ebp
 movl %esp,%ebp
 subl $40,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 movl 12(%ebp),%eax
 movl %eax,4(%esp)
 call obs_begin_body
 leave
 ret
.size obs_begin,.-obs_begin

.p2align 4
.globl obs_rtt_d1
.type obs_rtt_d1,@function
obs_rtt_d1:
 pushl %ebp
 movl %esp,%ebp
 subl $72,%esp
 movaps %xmm0,(%esp)
 movl 8(%ebp),%eax
 movl %eax,0(%esp)
 call obs_rtt_d1_body
 leave
 ret
.size obs_rtt_d1,.-obs_rtt_d1

.section .note.GNU-stack,"",@progbits
