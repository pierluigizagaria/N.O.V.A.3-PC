// Original synthetic cdecl/x87 camera boundaries; no game code or assets.
.text
.globl _ZNK6glitch5scene16CCameraSceneNode6getFOVEv
.type _ZNK6glitch5scene16CCameraSceneNode6getFOVEv,@function
_ZNK6glitch5scene16CCameraSceneNode6getFOVEv:
.byte 0x55,0x89,0xe5,0x8b,0x45,0x8
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_get
.size _ZNK6glitch5scene16CCameraSceneNode6getFOVEv,.-_ZNK6glitch5scene16CCameraSceneNode6getFOVEv
.globl _ZN6glitch5scene16CCameraSceneNode6setFOVEf
.type _ZN6glitch5scene16CCameraSceneNode6setFOVEf,@function
_ZN6glitch5scene16CCameraSceneNode6setFOVEf:
.byte 0x55,0x89,0xe5,0x8b,0x45,0x8
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_set
.size _ZN6glitch5scene16CCameraSceneNode6setFOVEf,.-_ZN6glitch5scene16CCameraSceneNode6setFOVEf
.globl _ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv
.type _ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv,@function
_ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv:
.byte 0x55,0x89,0xe5,0x83,0xe4,0xf0
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_projection
.size _ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv,.-_ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv
.globl _ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv
.type _ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv,@function
_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv:
.byte 0x55,0x89,0xe5,0x83,0xe4,0xf0
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_matrices
.size _ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv,.-_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv
.globl _ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv
.type _ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv,@function
_ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv:
.byte 0x55,0x89,0xe5,0x56,0x83,0xec,0x14
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_get_projection
.size _ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv,.-_ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv
.globl _ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv
.type _ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv,@function
_ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv:
.byte 0x55,0x89,0xe5,0x8b,0x45,0x8
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_frustum
.size _ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv,.-_ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv
.globl _ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb
.type _ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb,@function
_ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb:
.byte 0x55,0x89,0xe5,0x8b,0x55,0xc
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_custom
.size _ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb,.-_ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb
.globl _ZN17CNovaSceneManager11drawWeaponsEv
.type _ZN17CNovaSceneManager11drawWeaponsEv,@function
_ZN17CNovaSceneManager11drawWeaponsEv:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_weapons
.size _ZN17CNovaSceneManager11drawWeaponsEv,.-_ZN17CNovaSceneManager11drawWeaponsEv
.globl _ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE
.type _ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE,@function
_ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE:
.byte 0x55,0x89,0xe5,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_ctor1
.size _ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE,.-_ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE
.globl _ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE
.type _ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE,@function
_ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE:
.byte 0x55,0x89,0xe5,0x83,0xec,0x28
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_ctor2
.size _ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE,.-_ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE
.globl _ZN6glitch7collada16CCameraSceneNodeD2Ev
.type _ZN6glitch7collada16CCameraSceneNodeD2Ev,@function
_ZN6glitch7collada16CCameraSceneNodeD2Ev:
.byte 0x55,0x89,0xe5,0x57,0x56
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_dtor
.size _ZN6glitch7collada16CCameraSceneNodeD2Ev,.-_ZN6glitch7collada16CCameraSceneNodeD2Ev
.globl _ZN20CPlayerJeepComponent6EnableEb
.type _ZN20CPlayerJeepComponent6EnableEb,@function
_ZN20CPlayerJeepComponent6EnableEb:
.byte 0x8d,0x4c,0x24,0x4,0x83,0xe4,0xf0
leal -4(%ecx),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
leal -4(%ecx),%esp
jmp fov_body_enable
.size _ZN20CPlayerJeepComponent6EnableEb,.-_ZN20CPlayerJeepComponent6EnableEb
.globl _ZNK20CPlayerJeepComponent15UpdateCameraFOVEib
.type _ZNK20CPlayerJeepComponent15UpdateCameraFOVEib,@function
_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib:
.byte 0x55,0x89,0xe5,0x83,0xe4,0xf0
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_helper
.size _ZNK20CPlayerJeepComponent15UpdateCameraFOVEib,.-_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib
.globl _ZN20CPlayerJeepComponentC1EP11CGameObject
.type _ZN20CPlayerJeepComponentC1EP11CGameObject,@function
_ZN20CPlayerJeepComponentC1EP11CGameObject:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_component_ctor
.size _ZN20CPlayerJeepComponentC1EP11CGameObject,.-_ZN20CPlayerJeepComponentC1EP11CGameObject
.globl _ZN20CPlayerJeepComponentC2EP11CGameObject
.type _ZN20CPlayerJeepComponentC2EP11CGameObject,@function
_ZN20CPlayerJeepComponentC2EP11CGameObject:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_component_ctor
.size _ZN20CPlayerJeepComponentC2EP11CGameObject,.-_ZN20CPlayerJeepComponentC2EP11CGameObject
.globl _ZN6CLevelC1Ev
.type _ZN6CLevelC1Ev,@function
_ZN6CLevelC1Ev:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_level_ctor
.size _ZN6CLevelC1Ev,.-_ZN6CLevelC1Ev
.globl _ZN6CLevelC2Ev
.type _ZN6CLevelC2Ev,@function
_ZN6CLevelC2Ev:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_level_ctor
.size _ZN6CLevelC2Ev,.-_ZN6CLevelC2Ev
.globl _ZN6CLevel5ResetEv
.type _ZN6CLevel5ResetEv,@function
_ZN6CLevel5ResetEv:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl %ebp,%esp
popl %ebp
jmp fov_body_level_reset
.size _ZN6CLevel5ResetEv,.-_ZN6CLevel5ResetEv
.globl _ZN6CLevel8GetLevelEv
.type _ZN6CLevel8GetLevelEv,@function
_ZN6CLevel8GetLevelEv:
jmp fov_body_level
.size _ZN6CLevel8GetLevelEv,.-_ZN6CLevel8GetLevelEv
.globl _ZN6CLevel18GetPlayerComponentEv
.type _ZN6CLevel18GetPlayerComponentEv,@function
_ZN6CLevel18GetPlayerComponentEv:
jmp fov_body_player
.size _ZN6CLevel18GetPlayerComponentEv,.-_ZN6CLevel18GetPlayerComponentEv
.globl _ZN6CLevel22GetPlayerJeepComponentEv
.type _ZN6CLevel22GetPlayerJeepComponentEv,@function
_ZN6CLevel22GetPlayerJeepComponentEv:
jmp fov_body_jeep
.size _ZN6CLevel22GetPlayerJeepComponentEv,.-_ZN6CLevel22GetPlayerJeepComponentEv
.globl _Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE
.type _Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE,@function
_Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE:
jmp fov_body_fast
.size _Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE,.-_Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE
.globl fov_site_pseudo
.type fov_site_pseudo,@function
fov_site_pseudo:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_pseudo_call
fov_site_pseudo_call:
call *0x134(%eax)
leave
ret
.size fov_site_pseudo,.-fov_site_pseudo
.globl fov_site_vertical
.type fov_site_vertical,@function
fov_site_vertical:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_vertical_call
fov_site_vertical_call:
call *0x134(%eax)
leave
ret
.size fov_site_vertical,.-fov_site_vertical
.macro animation_writer name
.globl fov_site_\name
.type fov_site_\name,@function
fov_site_\name:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_\name\()_call
fov_site_\name\()_call:
call *0x134(%eax)
leave
ret
.size fov_site_\name,.-fov_site_\name
.endm
animation_writer vertical_direct
animation_writer vertical_blended
animation_writer vertical_key
animation_writer vertical_interpolated
.globl fov_bob_registration
.type fov_bob_registration,@function
fov_bob_registration:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%eax
movl %eax,4(%esp)
.globl fov_bob_registration_call
fov_bob_registration_call:
call fov_bob_registration_base
leave
ret
.size fov_bob_registration,.-fov_bob_registration
.globl fov_site_raw
.type fov_site_raw,@function
fov_site_raw:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_raw_call
fov_site_raw_call:
call *0x120(%eax)
leave
ret
.size fov_site_raw,.-fov_site_raw
.globl fov_site_save
.type fov_site_save,@function
fov_site_save:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_save_call
fov_site_save_call:
call *0x120(%eax)
leave
ret
.size fov_site_save,.-fov_site_save
.globl fov_site_enable
.type fov_site_enable,@function
fov_site_enable:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_enable_call
fov_site_enable_call:
call *0x120(%eax)
leave
ret
.size fov_site_enable,.-fov_site_enable
.globl fov_site_update
.type fov_site_update,@function
fov_site_update:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_update_call
fov_site_update_call:
call *0x120(%eax)
leave
ret
.size fov_site_update,.-fov_site_update
.globl fov_site_helper
.type fov_site_helper,@function
fov_site_helper:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_helper_call
fov_site_helper_call:
call *0x120(%eax)
leave
ret
.size fov_site_helper,.-fov_site_helper
.globl fov_site_jeep_write
.type fov_site_jeep_write,@function
fov_site_jeep_write:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_jeep_write_call
fov_site_jeep_write_call:
call *0x134(%eax)
leave
ret
.size fov_site_jeep_write,.-fov_site_jeep_write
.globl fov_site_weapon_write
.type fov_site_weapon_write,@function
fov_site_weapon_write:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_weapon_write_call
fov_site_weapon_write_call:
call *0x134(%eax)
leave
ret
.size fov_site_weapon_write,.-fov_site_weapon_write
.globl fov_site_weapon_restore
.type fov_site_weapon_restore,@function
fov_site_weapon_restore:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_weapon_restore_call
fov_site_weapon_restore_call:
call *0x134(%eax)
leave
ret
.size fov_site_weapon_restore,.-fov_site_weapon_restore
.globl fov_site_diff
.type fov_site_diff,@function
fov_site_diff:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
.globl fov_site_diff_call
fov_site_diff_call:
call _ZN14CWeaponManager23GetCurrentWeaponFovDiffEv
leave
ret
.size fov_site_diff,.-fov_site_diff
.globl _ZN14CWeaponManager23GetCurrentWeaponFovDiffEv
.type _ZN14CWeaponManager23GetCurrentWeaponFovDiffEv,@function
_ZN14CWeaponManager23GetCurrentWeaponFovDiffEv:
pushl %ebp
movl %esp,%ebp
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
leave
jmp fov_body_diff
.size _ZN14CWeaponManager23GetCurrentWeaponFovDiffEv,.-_ZN14CWeaponManager23GetCurrentWeaponFovDiffEv
.globl fov_site_level_first
.type fov_site_level_first,@function
fov_site_level_first:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_level_first_call
fov_site_level_first_call:
call *0x120(%eax)
leave
ret
.size fov_site_level_first,.-fov_site_level_first
.globl fov_site_level_accumulate
.type fov_site_level_accumulate,@function
fov_site_level_accumulate:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_level_accumulate_call
fov_site_level_accumulate_call:
call *0x120(%eax)
leave
ret
.size fov_site_level_accumulate,.-fov_site_level_accumulate
.globl fov_site_render_read
.type fov_site_render_read,@function
fov_site_render_read:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_render_read_call
fov_site_render_read_call:
call *0x120(%eax)
leave
ret
.size fov_site_render_read,.-fov_site_render_read
.globl fov_site_render_write
.type fov_site_render_write,@function
fov_site_render_write:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%edx
movl %edx,4(%esp)
movl (%eax),%eax
.globl fov_site_render_write_call
fov_site_render_write_call:
call *0x134(%eax)
leave
ret
.size fov_site_render_write,.-fov_site_render_write
.globl fov_site_fast
.type fov_site_fast,@function
fov_site_fast:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
.globl fov_site_fast_call
fov_site_fast_call:
call _Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE
leave
ret
.size fov_site_fast,.-fov_site_fast
.globl fov_bad_eax_offset
.type fov_bad_eax_offset,@function
fov_bad_eax_offset:
.byte 0x55,0x89,0xe5,0x8b,0x45,0xc
leave
ret
.size fov_bad_eax_offset,.-fov_bad_eax_offset
.globl fov_bad_edx_offset
.type fov_bad_edx_offset,@function
fov_bad_edx_offset:
.byte 0x55,0x89,0xe5,0x8b,0x55,0x10
leave
ret
.size fov_bad_edx_offset,.-fov_bad_edx_offset
// Isolated trampoline tests consume the copied register load itself.
.globl fov_eax_load_return
.type fov_eax_load_return,@function
fov_eax_load_return:
.byte 0x55,0x89,0xe5,0x8b,0x45,0x08
leave
ret
.size fov_eax_load_return,.-fov_eax_load_return
.globl fov_edx_load_return
.type fov_edx_load_return,@function
fov_edx_load_return:
.byte 0x55,0x89,0xe5,0x8b,0x55,0x0c
movl %edx,%eax
leave
ret
.size fov_edx_load_return,.-fov_edx_load_return
// Exact native setup entry and cdecl two-word/aligned forwarding contract.
.globl _ZN6glitch5scene16CCameraSceneNode14renderInternalEPv
.type _ZN6glitch5scene16CCameraSceneNode14renderInternalEPv,@function
_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv:
.byte 0x55,0x89,0xe5,0x81,0xec,0xe8,0,0,0
leal 4(%ebp),%eax
andl $15,%eax
cmpl $12,%eax
je 1f
ud2
1:
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%eax
movl %eax,4(%esp)
call fov_body_camera_render
movl 8(%ebp),%eax
movl %eax,(%esp)
addl $0x200,%eax
movl %eax,8(%esp)
movl $1,4(%esp)
movl $0,12(%esp)
.globl fov_eligibility_projection_call
fov_eligibility_projection_call:
call _ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj
movl 8(%ebp),%eax
movl %eax,(%esp)
addl $0x1c0,%eax
movl %eax,8(%esp)
movl $0,4(%esp)
movl $0,12(%esp)
call _ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj
.globl fov_eligibility_camera_epilogue
fov_eligibility_camera_epilogue:
leave
ret
.size _ZN6glitch5scene16CCameraSceneNode14renderInternalEPv,.-_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv
.globl fov_site_weapon_begin
.type fov_site_weapon_begin,@function
fov_site_weapon_begin:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl %eax,(%esp)
movl (%eax),%eax
.globl fov_site_weapon_begin_call
fov_site_weapon_begin_call:
call *0x120(%eax)
leave
ret
.size fov_site_weapon_begin,.-fov_site_weapon_begin
.section .note.GNU-stack,"",@progbits

.text
.globl _ZNK11CGameObject6IsDeadEv
.type _ZNK11CGameObject6IsDeadEv,@function
_ZNK11CGameObject6IsDeadEv:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%eax
movl 0x118(%eax),%eax
testl %eax,%eax
jz 1f
movl %eax,(%esp)
.globl fov_eligibility_health_call
fov_eligibility_health_call:
call _ZNK16CHealthComponent6IsDeadEv
1:
leave
ret
.size _ZNK11CGameObject6IsDeadEv,.-_ZNK11CGameObject6IsDeadEv
.globl _ZN13GameMpManagerC1Ev
.type _ZN13GameMpManagerC1Ev,@function
_ZN13GameMpManagerC1Ev:
.byte 0x55,0x89,0xe5,0x56,0x53
movl %ebp,%esp
popl %ebp
jmp fov_body_eligibility_ctor
.size _ZN13GameMpManagerC1Ev,.-_ZN13GameMpManagerC1Ev
.globl _ZN13GameMpManagerC2Ev
.set _ZN13GameMpManagerC2Ev,_ZN13GameMpManagerC1Ev
.globl _ZN13GameMpManagerD1Ev
.type _ZN13GameMpManagerD1Ev,@function
_ZN13GameMpManagerD1Ev:
.byte 0x55,0x89,0xe5,0x57,0x56,0x53
movl %ebp,%esp
popl %ebp
jmp fov_body_eligibility_dtor
.size _ZN13GameMpManagerD1Ev,.-_ZN13GameMpManagerD1Ev
.globl _ZN13GameMpManagerD2Ev
.set _ZN13GameMpManagerD2Ev,_ZN13GameMpManagerD1Ev

// V01i authored native call-site fixture; not copied game implementation.
.text
.globl v01_rigid_draw_entry
.type v01_rigid_draw_entry,@function
v01_rigid_draw_entry:
pushl %ebp
movl %esp,%ebp
subl $24,%esp
movl 8(%ebp),%edx
movl 0x108(%edx),%eax
movl %eax,(%esp)
movl 0xe8(%edx),%ecx
movl 0x10(%ecx),%ecx
movl %ecx,4(%esp)
addl $0x10,%edx
movl %edx,8(%esp)
movl (%eax),%edx
.globl v01_rigid_model_call
v01_rigid_model_call:
call *0x44(%edx)
.globl v01_rigid_model_return
v01_rigid_model_return:
leave
ret
.size v01_rigid_draw_entry,.-v01_rigid_draw_entry
.globl _ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE
.type _ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE,@function
_ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE:
.byte 0x55,0x89,0xe5,0x53,0x83,0xec,0x14
movl 8(%ebp),%eax
movl %eax,(%esp)
movl 12(%ebp),%eax
movl %eax,4(%esp)
movl 16(%ebp),%eax
movl %eax,8(%esp)
call v01_rigid_model_body
movl -4(%ebp),%ebx
leave
ret
.size _ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE,.-_ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE
