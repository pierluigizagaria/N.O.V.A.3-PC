
# Original T07g contracts. All native entries assert 16-byte aligned call frames.
.p2align 4
.globl bike_update
.hidden bike_update
.type bike_update,@function
bike_update:
  leal 4(%esp),%ecx
  andl $-16,%esp
  pushl -4(%ecx)
  pushl %ebp
  movl %esp,%ebp
  pushl %ecx
  subl $20,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl (%ecx),%eax
  movl %eax,(%esp)
  movl 4(%ecx),%eax
  movl %eax,4(%esp)
.globl bike_base_call
.hidden bike_base_call
.type bike_base_call,@function
bike_base_call:
  call bike_base
.size bike_base_call,.-bike_base_call
  call bike_move_body
  movl -4(%ebp),%ecx
  movl (%ecx),%eax
  cmpl $0,(%eax)
  jne 1f
  movl %eax,(%esp)
  movl $77,4(%esp)
.globl bike_visual_call
.hidden bike_visual_call
.type bike_visual_call,@function
bike_visual_call:
  call bike_visual
.size bike_visual_call,.-bike_visual_call
  movl -4(%ebp),%ecx
  movl (%ecx),%eax
  movl %eax,(%esp)
  call bike_primary_body
1:
  movl -4(%ebp),%ecx
  leave
  leal -4(%ecx),%esp
  ret
.size bike_update,.-bike_update
.p2align 4
.globl bike_base
.hidden bike_base
.type bike_base,@function
bike_base:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp bike_base_body
.size bike_base,.-bike_base
.p2align 4
.globl bike_visual
.hidden bike_visual
.type bike_visual,@function
bike_visual:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp bike_visual_body
.size bike_visual,.-bike_visual
.p2align 4
.globl bike_event
.hidden bike_event
.type bike_event,@function
bike_event:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp bike_event_body
.size bike_event,.-bike_event
.p2align 4
.globl bike_enable
.hidden bike_enable
.type bike_enable,@function
bike_enable:
  pushl %ebp
  movl %esp,%ebp
  subl $88,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  call bike_enable_body
  leave
  ret
.size bike_enable,.-bike_enable
.p2align 4
.globl bike_state
.hidden bike_state
.type bike_state,@function
bike_state:
  pushl %ebp
  movl %esp,%ebp
  subl $232,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  call bike_state_body
  leave
  ret
.size bike_state,.-bike_state
.p2align 4
.globl bike_road
.hidden bike_road
.type bike_road,@function
bike_road:
  pushl %ebp
  movl %esp,%ebp
  andl $-16,%esp
  subl $16,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  call bike_road_body
  leave
  ret
.size bike_road,.-bike_road
.p2align 4
.globl bike_configure
.hidden bike_configure
.type bike_configure,@function
bike_configure:
  pushl %ebp
  movl %esp,%ebp
  pushl %esi
  subl $20,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  call bike_configure_body
  movl -4(%ebp),%esi
  leave
  ret
.size bike_configure,.-bike_configure
.p2align 4
.globl bike_move_to
.hidden bike_move_to
.type bike_move_to,@function
bike_move_to:
  pushl %ebp
  movl %esp,%ebp
  pushl %esi
  pushl %ebx
  subl $16,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  movl 16(%ebp),%eax
  movl %eax,8(%esp)
  movl 20(%ebp),%eax
  movl %eax,12(%esp)
  call bike_move_to_body
  movl -4(%ebp),%esi
  movl -8(%ebp),%ebx
  leave
  ret
.size bike_move_to,.-bike_move_to
.p2align 4
.globl bike_reset
.hidden bike_reset
.type bike_reset,@function
bike_reset:
  pushl %ebp
  movl %esp,%ebp
  pushl %esi
  pushl %ebx
  subl $16,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  call bike_reset_body
  movl -4(%ebp),%esi
  movl -8(%ebp),%ebx
  leave
  ret
.size bike_reset,.-bike_reset
.p2align 4
.globl bike_load
.hidden bike_load
.type bike_load,@function
bike_load:
  pushl %ebp
  movl %esp,%ebp
  pushl %edi
  pushl %esi
  pushl %ebx
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,0(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  call bike_load_body
  movl -4(%ebp),%edi
  movl -8(%ebp),%esi
  movl -12(%ebp),%ebx
  leave
  ret
.size bike_load,.-bike_load

.p2align 4
.globl bike_query
.hidden bike_query
.type bike_query,@function
bike_query:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp bike_query_body
.size bike_query,.-bike_query
.p2align 4
.globl bike_save
.hidden bike_save
.type bike_save,@function
bike_save:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp bike_save_body
.size bike_save,.-bike_save

.p2align 4
.globl bike_on_event
.hidden bike_on_event
.type bike_on_event,@function
bike_on_event:
  pushl %ebp
  movl %esp,%ebp
  andl $-16,%esp
  subl $16,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,(%esp)
  movl 12(%ebp),%eax
  movl %eax,4(%esp)
  movl 16(%ebp),%eax
  movl %eax,8(%esp)
  call bike_on_event_body
  leave
  ret
.size bike_on_event,.-bike_on_event
.p2align 4
.globl bike_reset_one
.hidden bike_reset_one
.type bike_reset_one,@function
bike_reset_one:
  pushl %ebp
  xorl %edx,%edx
  movl %esp,%ebp
  subl $24,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,(%esp)
  call bike_reset_one_body
  leave
  ret
.size bike_reset_one,.-bike_reset_one
.p2align 4
.globl bike_reset_all
.hidden bike_reset_all
.type bike_reset_all,@function
bike_reset_all:
  pushl %ebp
  movl %esp,%ebp
  andl $-16,%esp
  subl $16,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  movl 8(%ebp),%eax
  movl %eax,(%esp)
  call bike_reset_all_body
  leave
  ret
.size bike_reset_all,.-bike_reset_all
