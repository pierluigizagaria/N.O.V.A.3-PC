.text
.macro input_entry name, body
.p2align 4
.globl Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_\name
.type Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_\name,@function
Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp \body
.endm
input_entry touchEvent, input_touch
input_entry OnKeyDown, input_down
input_entry OnKeyUp, input_up
input_entry OnKeyboardFinish, input_finish

.macro normal_release_entry name, body
.p2align 4
.globl \name
.type \name,@function
\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp \body
.endm
normal_release_entry normal_release_event, normal_release_event_body
normal_release_entry normal_release_aim, normal_release_aim_body
.section .note.GNU-stack,"",@progbits

.text
.p2align 4
.globl hook_subject
.hidden hook_subject
.type hook_subject,@function
hook_subject:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    movl 8(%ebp),%eax
    addl $7,%eax
    leave
    ret
.size hook_subject,.-hook_subject

.globl guest_boolean
.type guest_boolean,@function
guest_boolean:
    movl 4(%esp),%eax
    andl $1,%eax
    orl $0xa5a50000,%eax
    ret
.size guest_boolean,.-guest_boolean
.globl hook_relative
.type hook_relative,@function
hook_relative:
    call 1f
1:  popl %eax
    ret
.size hook_relative,.-hook_relative

# Synthetic input consumer: a queued absolute yaw wins over actor state.
# This models the ordering hazard without any game code or game dependency.
.globl guest_consume_look
.hidden guest_consume_look
.type guest_consume_look,@function
guest_consume_look:
    movl 4(%esp),%eax
    movl 8(%esp),%edx
    cmpb $0,16(%eax)
    je 1f
    movss 12(%eax),%xmm0
    movss %xmm0,(%edx)
    movb $0,16(%eax)
1:  movss 4(%edx),%xmm0
    addss 24(%eax),%xmm0
    movss %xmm0,4(%edx)
    movl $0,24(%eax)
    ret
.size guest_consume_look,.-guest_consume_look

# Check aligned entry before the independently written full look model.
.globl guest_consume_look_contract
.hidden guest_consume_look_contract
.type guest_consume_look_contract,@function
guest_consume_look_contract:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp contract_consume_body
.size guest_consume_look_contract,.-guest_consume_look_contract
.text
.macro snapshot_entry name
.p2align 4
.globl snapshot_\name
.type snapshot_\name,@function
snapshot_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp snapshot_\name\()_body
.size snapshot_\name,.-snapshot_\name
.endm
snapshot_entry control
snapshot_entry ammo
snapshot_entry can_use
snapshot_entry power_type

.text
# Exact-site CALL wrappers preserve eight words and aligned callee entry.
.macro draw_site name
.p2align 4
.globl draw_\name\()_site
.hidden draw_\name\()_site
.type draw_\name\()_site,@function
draw_\name\()_site:
    pushl %ebp
    movl %esp,%ebp
    subl $40,%esp
    .set i,0
    .rept 8
    movl 8+i*4(%ebp),%eax
    movl %eax,i*4(%esp)
    .set i,i+1
    .endr
.globl draw_\name\()_call
.hidden draw_\name\()_call
.type draw_\name\()_call,@function
draw_\name\()_call:
    call draw_paint
.size draw_\name\()_call,.-draw_\name\()_call
    leave
    ret
.size draw_\name\()_site,.-draw_\name\()_site
.endm
draw_site rush
draw_site other
.macro draw_entry name
.p2align 4
.globl draw_\name
.hidden draw_\name
.type draw_\name,@function
draw_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp draw_\name\()_body
.size draw_\name,.-draw_\name
.endm
draw_entry owner
draw_entry paint

# Original fixture for the exact GCC realigning entry accepted by T07b.
# MOVAPS also checks the realigned trampoline body and all saved arguments.
.text
.p2align 4
.globl mech_consume
.hidden mech_consume
.type mech_consume,@function
mech_consume:
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
    call mech_consume_body
    movl -4(%ebp),%ecx
    leave
    leal -4(%ecx),%esp
    ret
.size mech_consume,.-mech_consume
.macro mech_entry name
.p2align 4
.globl mech_\name
.hidden mech_\name
.type mech_\name,@function
mech_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp mech_\name\()_body
.size mech_\name,.-mech_\name
.endm
mech_entry event
mech_entry reset
mech_entry stop
mech_entry set_draw

.macro mech_stop_site name
.p2align 4
.globl mech_\name\()_stop
.hidden mech_\name\()_stop
.type mech_\name\()_stop,@function
mech_\name\()_stop:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl 12(%ebp),%eax
    movl %eax,4(%esp)
    movl 16(%ebp),%eax
    movl %eax,8(%esp)
.globl mech_\name\()_stop_call
.hidden mech_\name\()_stop_call
.type mech_\name\()_stop_call,@function
mech_\name\()_stop_call:
    call mech_stop
.size mech_\name\()_stop_call,.-mech_\name\()_stop_call
    leave
    ret
.size mech_\name\()_stop,.-mech_\name\()_stop
.endm
mech_stop_site primary
mech_stop_site secondary

.globl unsupported_lea
.type unsupported_lea,@function
unsupported_lea:
    leal 8(%esp),%ecx
    andl $-16,%esp
    ret
.size unsupported_lea,.-unsupported_lea

# The machine-gun entry uses the already-supported PUSH/MOV/AND realignment.
.p2align 4
.globl machinegun_consume
.hidden machinegun_consume
.type machinegun_consume,@function
machinegun_consume:
    pushl %ebp
    movl %esp,%ebp
    andl $-16,%esp
    subl $32,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,16(%esp)
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl 12(%ebp),%eax
    movl %eax,4(%esp)
    call machinegun_consume_body
    leave
    ret
.size machinegun_consume,.-machinegun_consume
.macro machinegun_entry name
.p2align 4
.globl machinegun_\name
.hidden machinegun_\name
.type machinegun_\name,@function
machinegun_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp machinegun_\name\()_body
.size machinegun_\name,.-machinegun_\name
.endm
machinegun_entry event
machinegun_entry reset
machinegun_entry update
machinegun_entry stop
machinegun_entry release
.macro machinegun_release_site name,words
.p2align 4
.globl machinegun_primary_\name
.hidden machinegun_primary_\name
.type machinegun_primary_\name,@function
machinegun_primary_\name:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    .set i,0
    .rept \words
    movl 8+i*4(%ebp),%eax
    movl %eax,i*4(%esp)
    .set i,i+1
    .endr
.globl machinegun_primary_\name\()_call
.hidden machinegun_primary_\name\()_call
.type machinegun_primary_\name\()_call,@function
machinegun_primary_\name\()_call:
    call machinegun_\name
.size machinegun_primary_\name\()_call,.-machinegun_primary_\name\()_call
    leave
    ret
.size machinegun_primary_\name,.-machinegun_primary_\name
.endm
machinegun_release_site stop,3
machinegun_release_site release,2

# Original driver input fixtures. No native game instructions are reproduced
# beyond the generic x86 ABI entry pattern shared by ordinary compiled code.
.p2align 4
.globl driver_camera
.hidden driver_camera
.type driver_camera,@function
driver_camera:
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
    call driver_camera_body
    movl -4(%ebp),%ecx
    leave
    leal -4(%ecx),%esp
    ret
.size driver_camera,.-driver_camera
.p2align 4
.globl driver_pedals
.hidden driver_pedals
.type driver_pedals,@function
driver_pedals:
    pushl %ebp
    movl %esp,%ebp
    subl $40,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    call driver_pedals_body
    leave
    ret
.size driver_pedals,.-driver_pedals
.macro driver_entry name
.p2align 4
.globl driver_\name
.hidden driver_\name
.type driver_\name,@function
driver_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp driver_\name\()_body
.size driver_\name,.-driver_\name
.endm
driver_entry event
driver_entry fullscreen

.macro driver_draw_entry name
.p2align 4
.globl driver_\name\()_draw
.hidden driver_\name\()_draw
.type driver_\name\()_draw,@function
driver_\name\()_draw:
    pushl %ebp
    movl %esp,%ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    movl 8(%ebp),%eax
    movl %eax,(%esp)
    movl 12(%ebp),%eax
    movl %eax,4(%esp)
    call driver_draw_body
    addl $28,%esp
    popl %ebx
    popl %esi
    popl %edi
    leave
    ret
.size driver_\name\()_draw,.-driver_\name\()_draw
.endm
driver_draw_entry analog
driver_draw_entry mobile
driver_draw_entry angular

# Jeep capability fixtures use original code and checked native ABI shapes.
.p2align 4
.globl jeep_update
.hidden jeep_update
.type jeep_update,@function
jeep_update:
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
    call jeep_update_body
    movl -4(%ebp),%ecx
    leave
    leal -4(%ecx),%esp
    ret
.size jeep_update,.-jeep_update
.macro jeep_entry name
.p2align 4
.globl jeep_\name
.hidden jeep_\name
.type jeep_\name,@function
jeep_\name:
    subl $28,%esp
    xorps %xmm0,%xmm0
    movaps %xmm0,(%esp)
    addl $28,%esp
    jmp jeep_\name\()_body
.size jeep_\name,.-jeep_\name
.endm
jeep_entry event
jeep_entry reset
jeep_entry stop
jeep_entry animation
.macro jeep_release_site role,api,words
.p2align 4
.globl jeep_\role\()_\api
.hidden jeep_\role\()_\api
.type jeep_\role\()_\api,@function
jeep_\role\()_\api:
    pushl %ebp
    movl %esp,%ebp
    subl $24,%esp
    .set i,0
    .rept \words
    movl 8+i*4(%ebp),%eax
    movl %eax,i*4(%esp)
    .set i,i+1
    .endr
.globl jeep_\role\()_\api\()_call
.hidden jeep_\role\()_\api\()_call
.type jeep_\role\()_\api\()_call,@function
jeep_\role\()_\api\()_call:
    call jeep_\api
.size jeep_\role\()_\api\()_call,.-jeep_\role\()_\api\()_call
    leave
    ret
.size jeep_\role\()_\api,.-jeep_\role\()_\api
.endm
jeep_release_site primary,stop,3
jeep_release_site secondary,stop,3
jeep_release_site primary,animation,1
jeep_release_site secondary,animation,1

  # Checked T07f MG Update entry and independent aligned synthetic phase/query calls.
  .p2align 4
  .globl mounted_update
  .hidden mounted_update
  .type mounted_update,@function
mounted_update:
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
  call mounted_update_body
  movl -4(%ebp),%ecx
  leave
  leal -4(%ecx),%esp
  ret
  .size mounted_update,.-mounted_update
  .p2align 4
  .globl mounted_ai
  .hidden mounted_ai
  .type mounted_ai,@function
mounted_ai:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp mounted_ai_body
  .size mounted_ai,.-mounted_ai
  .p2align 4
  .globl mounted_query
  .hidden mounted_query
  .type mounted_query,@function
mounted_query:
  subl $28,%esp
  xorps %xmm0,%xmm0
  movaps %xmm0,(%esp)
  addl $28,%esp
  jmp mounted_query_body
  .size mounted_query,.-mounted_query
