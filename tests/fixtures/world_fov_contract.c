#include "world_fov_contract.h"
extern double tan(double);
extern void fov_audit(unsigned), fov_reset_callback(unsigned), fov_weapon_callback(unsigned,unsigned,unsigned);
#define U(p,o) (*(unsigned *)((unsigned)(p)+(o)))
#define F(p,o) (*(float *)((unsigned)(p)+(o)))
#define B(p,o) (*(unsigned char *)((unsigned)(p)+(o)))
WorldFovContract fov_scene;
unsigned fov_eligibility_mp __asm__("_ZN13GameMpManager9SingletonE");
unsigned fov_eligibility_app __asm__("_ZZN11Application11GetInstanceEvE3app");
unsigned fov_eligibility_level __asm__("_ZN6CLevel10m_instanceE");
unsigned fov_eligibility_vtable[4] __asm__("_ZTV9GS_InGame");
extern void fov_eligibility_ctor(unsigned) __asm__("_ZN13GameMpManagerC1Ev");
extern void native_camera_render(unsigned,unsigned) __asm__("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv");
unsigned fov_vtable[128] __asm__("_ZTV22CCustomCameraSceneNode")={ [6]=(unsigned)&native_camera_render };
int fov_width __asm__("g_ScreenW")=1280;
int fov_height __asm__("g_ScreenH")=720;
float fov_default __asm__("_ZN11CGameCamera11DEFAULT_FOVE")=70;
unsigned fov_manager __asm__("g_sceneManager");
unsigned fov_root_present=1;
unsigned fov_component_present=1;
unsigned fov_bob_node_table, fov_bob_audit_enabled;
unsigned fov_tweak __asm__("_ZN16WeaponsTweakable10s_instanceE");
extern float fov_site_raw(unsigned), fov_site_save(unsigned), fov_site_enable(unsigned);
extern float fov_site_weapon_begin(unsigned);
extern float fov_site_update(unsigned), fov_site_helper(unsigned);
extern void fov_site_pseudo(unsigned,float), fov_site_jeep_write(unsigned,float);
extern void fov_site_vertical(unsigned,float);
extern void fov_site_weapon_write(unsigned,float), fov_site_fast(unsigned);
extern void fov_site_weapon_restore(unsigned,float);
extern float fov_site_diff(unsigned);
extern float fov_site_level_first(unsigned), fov_site_level_accumulate(unsigned), fov_site_render_read(unsigned);
extern void fov_site_render_write(unsigned,float);
extern float native_get(unsigned) __asm__("_ZNK6glitch5scene16CCameraSceneNode6getFOVEv");
extern void native_projection(unsigned) __asm__("_ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv");
extern void native_matrices(unsigned) __asm__("_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv");
extern unsigned native_get_projection(unsigned) __asm__("_ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv");
extern unsigned native_frustum(unsigned) __asm__("_ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv");
extern void native_weapons(unsigned) __asm__("_ZN17CNovaSceneManager11drawWeaponsEv");
extern void native_camera_render(unsigned,unsigned) __asm__("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv");
extern void native_ctor(unsigned,unsigned,unsigned) __asm__("_ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE");
unsigned fov_body_level(void) { fov_audit(1); ++fov_scene.queries; return fov_root_present ? (unsigned)fov_scene.level : 0; }
unsigned fov_body_player(unsigned level) { fov_audit(1); ++fov_scene.queries; return level && fov_root_present ? (unsigned)fov_scene.player : 0; }
unsigned fov_body_jeep(unsigned level) { fov_audit(4); ++fov_scene.queries; return level && fov_component_present ? (unsigned)fov_scene.component : 0; }
void fov_body_level_ctor(unsigned level) { F(level,0x2dc)=0; B(level,0x2e0)=0; }
void fov_body_level_reset(unsigned level) {
    ++fov_scene.reset_calls; F(level,0x2dc)=0; B(level,0x2e0)=0;
    fov_reset_callback(level); // Native Fog callback occurs after the zero write.
}
float fov_body_get(unsigned p) { return F(p,0x128); }
void fov_body_set(unsigned p,float value) {
    ++fov_scene.setters;
    if(fov_scene.noop_set && F(p,0x128)==value) return;
    F(p,0x128)=value; U(p,0x300)|=2;
}
void fov_body_projection(unsigned p) {
    if(p==fov_scene.eligibility_poison_node) { ++fov_scene.eligibility_poison_reads; return; }
    ++fov_scene.projections;
    const float vertical=F(p,0x128), aspect=F(p,0x12c), near=F(p,0x130), far=F(p,0x134);
    unsigned i; for(i=0;i<16;++i) F(p,0x200+i*4)=0;
    F(p,0x214)=(float)(1/tan(vertical*.5)); F(p,0x200)=F(p,0x214)/aspect;
    F(p,0x228)=-(far+near)/(far-near); F(p,0x22c)=-1;
    F(p,0x238)=-2*near*far/(far-near); U(p,0x300)&=~2u;
    fov_audit(7); // Actual builder return can reenter through a virtual/native callback.
}
void fov_body_matrices(unsigned p) {
    if(p==fov_scene.eligibility_poison_node) { ++fov_scene.eligibility_poison_reads; return; }
    ++fov_scene.matrices;
    if(U(p,0x300)&2) native_projection(p);
    // Authored synthetic view faces -Z: side plane/ray slope and far box bounds.
    F(p,0x13c)=1/F(p,0x200); F(p,0x140)=1/F(p,0x214);
    F(p,0x144)=F(p,0x134)/F(p,0x200); F(p,0x148)=F(p,0x134)/F(p,0x214);
    if(fov_bob_audit_enabled) fov_audit(9);
}
unsigned fov_bob_registration_base(unsigned p,unsigned context) {
    (void)context;native_matrices(p);return 0xa6ebc901u;
}
void fov_body_camera_render(unsigned p,unsigned context) {
    if(p==fov_scene.eligibility_poison_node) { ++fov_scene.eligibility_poison_reads; return; }
    ++fov_scene.camera_setups; fov_scene.render_words[0]=p; fov_scene.render_words[1]=context;
    if(U(p,0x300)&2) native_projection(p);
    fov_audit(5); // Final original-body callback before the checked projection CALL.
}
// The assembly now reaches the actual checked direct publication CALL before
// this authored driver sink. No lazy getter can repair the first observation.
void fov_body_transform(unsigned driver,unsigned type,unsigned matrix,unsigned flags)
    __asm__("_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj");
void fov_body_transform(unsigned driver,unsigned type,unsigned matrix,unsigned flags) {
    (void)flags;
    if(driver==fov_scene.eligibility_poison_node) { ++fov_scene.eligibility_poison_reads; return; }
    if(type==1) ++fov_scene.eligibility_driver_calls;
    if(type==0) ++fov_scene.eligibility_view_calls;
    for(unsigned i=0;i<16;++i) {
        if(type==1) fov_scene.driver_projection[i]=F(matrix,i*4);
        if(type==0) fov_scene.driver_view[i]=F(matrix,i*4);
    }
    fov_scene.driver_node=driver;
}
unsigned fov_body_get_projection(unsigned p) { if(U(p,0x300)&2) native_projection(p); return p+0x200; }
unsigned fov_body_frustum(unsigned p) { return p+0x13c; }
void fov_body_custom(unsigned p,unsigned matrix,unsigned orthogonal) {
    unsigned i; ++fov_scene.custom_sets;
    for(i=0;i<16;++i) U(p,0x200+i*4)=U(matrix,i*4);
    B(p,0x109)=(unsigned char)orthogonal; U(p,0x300)&=~2u;
}
void fov_body_ctor1(unsigned p,unsigned database,unsigned camera) {
    fov_audit(2);
    unsigned i; for(i=0;i<0x350;++i) B(p,i)=0;
    U(p,0)=(unsigned)fov_vtable+16; F(p,0x128)=.7f;
    F(p,0x12c)=(float)fov_width/fov_height; F(p,0x130)=.1f; F(p,0x134)=100;
    U(p,0x300)=2;
    for(i=0;i<16;++i) F(p,0x1c0+i*4)=(i%5==0) ? 1.f : 0.f;
    F(p,0x1f0)=7;
    fov_scene.ctor_words[0]=p; fov_scene.ctor_words[1]=database; fov_scene.ctor_words[2]=camera;
}
void fov_body_ctor2(unsigned p,unsigned vtt,unsigned database,unsigned camera) {
    fov_body_ctor1(p,database,camera); fov_scene.ctor_words[3]=vtt;
}
void fov_body_dtor(unsigned p,unsigned vtt) {
    fov_scene.dtor_words[0]=p; fov_scene.dtor_words[1]=vtt; U(p,0)=0;
}
void fov_body_component_ctor(unsigned p,unsigned actor) {
    fov_scene.component_words[0]=p; fov_scene.component_words[1]=actor;
    B(p,0xd)=0;
}
void fov_fixture_pseudo(unsigned p,float pseudo) {
    fov_site_pseudo(p,pseudo*((float)fov_height/fov_width)*.017453292519943295f);
}
// Authored fixture for the observed Fovy applicator contract. Values already
// use the native getter/setter's vertical radians; weights are dimensionless.
void fov_fixture_animation(unsigned p,const float *values,const float *weights,unsigned count) {
    float output=0;
    if(count==1) output=values[0];
    else for(unsigned i=0;i<count;++i) {
        volatile float product=values[i]*weights[i];
        volatile float sum=output+product; output=sum;
    }
    fov_site_vertical(p,output);
}
void fov_fixture_delta_raw(unsigned p,float target,unsigned accumulate) {
    const float raw=accumulate ? fov_site_level_accumulate(p) : fov_site_level_first(p);
    F(fov_scene.level,0x2dc)=accumulate ? target+F(fov_scene.level,0x2dc)-raw : target-raw;
}
void fov_fixture_delta(unsigned p,float target_pseudo,unsigned accumulate) {
    fov_fixture_delta_raw(p,target_pseudo*((float)fov_height/fov_width)*.017453292519943295f,accumulate);
}
void fov_fixture_render(unsigned p) {
    const float raw=fov_site_render_read(p);
    fov_site_render_write(p,raw+F(fov_scene.level,0x2dc));
    B(fov_scene.level,0x2e0)=0;
}
static void sample_inside(unsigned p) {
    const unsigned matrix=native_get_projection(p), frustum=native_frustum(p);
    fov_scene.inside_x=F(matrix,0); fov_scene.inside_frustum=F(frustum,0);
}
static float result(unsigned component,float before,int dt,int force) {
    const unsigned config=U(component,0x108);
    if(B(component,0x1ac) && F(config,0x6c)>0 && F(config,0x70)>0) {
        const float target=F(config,0x6c)*.017453292519943295f;
        volatile float rate=.001f*F(config,0x74); volatile float a=rate*(float)dt;
        return (force&255) ? target : before+a*(target-before);
    }
    return F(component,0x178);
}
void fov_body_enable(unsigned component,unsigned enabled) {
    ++fov_scene.original_calls;
    if(!enabled) { U(fov_scene.camera,0x26c)=(unsigned)fov_scene.nodes[0]; B(component,0xd)=0; return; }
    const unsigned p=(unsigned)fov_scene.nodes[fov_scene.next_node];
    U(component,0x11c)=p; U(fov_scene.camera,0x26c)=p;
    fov_fixture_pseudo(p,70);
    F(component,0x178)=fov_site_save(p);
    const float before=fov_site_enable(p), output=result(component,before,0,1);
    if(before!=output) fov_site_jeep_write(p,output);
    B(component,0xd)=1; sample_inside(p);
}
void fov_body_helper(unsigned component,int dt,unsigned force) {
    const unsigned p=U(component,0x11c); const float before=fov_site_helper(p);
    const float output=result(component,before,dt,(int)force);
    if(before!=output) fov_site_jeep_write(p,output);
    sample_inside(p);
}
void fov_fixture_update(unsigned component,int dt) {
    const unsigned p=U(component,0x11c); const float before=fov_site_update(p);
    const float output=result(component,before,dt,0);
    if(before!=output) fov_site_jeep_write(p,output);
    unsigned node=p;
    fov_site_fast((unsigned)&node); // Original geometry must observe no-op completion.
    sample_inside(p);
}
void fov_body_fast(unsigned pointer) {
    ++fov_scene.fast_nodes;
    if(fov_scene.fast_mutate) F(fov_scene.config,0x74)=2; // First effect precedes intercepted geometry.
    native_matrices(U(pointer,0));
}
void fov_body_weapons(unsigned manager) {
    const unsigned depth=fov_scene.weapon_depth++, mode=fov_scene.weapon_mode[depth];
    fov_weapon_callback(depth,0,0);
    if(mode==3) { --fov_scene.weapon_depth; return; } // Actual empty-list return.
    const float diff=fov_site_diff(U(fov_scene.player,0x170));
    fov_scene.forwarded_sample=diff;
    fov_weapon_callback(depth,4,0);
    const unsigned p=U(manager,0xdc);
    float saved=0;
    if(mode==0) { // Reached only after native type/camera gates.
        saved=fov_site_weapon_begin(p);
        fov_weapon_callback(depth,5,p);
        fov_site_weapon_write(p,saved+diff);
    }
    // O09 synthetic late native transition: after the raw setter, BEFORE the
    // first direct camera/driver sink. Existing callbacks ignore phase6.
    fov_weapon_callback(depth,6,p);
    native_camera_render(p,0);
    fov_weapon_callback(depth,1,p);
    // Observe the real draw sink first: no lazy projection getter may mask it.
    fov_scene.weapon_gpu_x[depth]=fov_scene.driver_projection[0];
    fov_scene.weapon_gpu_view[depth]=fov_scene.driver_view[12];
    fov_scene.first_cpu_x[depth]=F(p,0x200); fov_scene.first_frustum[depth]=F(p,0x13c);
    fov_scene.first_raw[depth]=F(p,0x128);
    fov_scene.weapon_fov[depth]=native_get(p);
    fov_scene.weapon_x[depth]=F(native_get_projection(p),0);
    fov_scene.weapon_frustum[depth]=F(native_frustum(p),0);
    fov_scene.weapon_box[depth]=F(p,0x144);
    fov_scene.weapon_marker[depth]=F(p,0x200)*.25f;
    if(fov_scene.nested_weapon && depth<(fov_scene.weapon_limit ? fov_scene.weapon_limit : 1)) {
        native_weapons(manager);
        fov_weapon_callback(depth,3,p);
        fov_scene.after_gpu_x[depth]=fov_scene.driver_projection[0];
        fov_scene.after_view[depth]=fov_scene.driver_view[12];
        fov_scene.after_cpu_x[depth]=F(p,0x200);
        fov_scene.after_frustum[depth]=F(p,0x13c);
    }
    if(mode==0) fov_site_weapon_restore(p,saved);
    fov_weapon_callback(depth,2,p);
    if(fov_scene.replace_weapon && depth==0) {
        U(fov_scene.camera,0x26c)=(unsigned)fov_scene.nodes[1];
        U(manager,0xdc)=(unsigned)fov_scene.nodes[1];
    }
    --fov_scene.weapon_depth;
}
void fov_fixture_setup(void) {
    unsigned i;
    for(i=0;i<128;++i) fov_vtable[i]=0;
    fov_vtable[(16+0x120)/4]=(unsigned)&native_get;
    fov_vtable[(16+8)/4]=(unsigned)&native_camera_render;
    extern void native_set(unsigned,float) __asm__("_ZN6glitch5scene16CCameraSceneNode6setFOVEf");
    fov_vtable[(16+0x134)/4]=(unsigned)&native_set;
    U(fov_scene.player,4)=(unsigned)fov_scene.eligibility_actor;
    U(fov_scene.player,0x178)=(unsigned)fov_scene.camera;
    B(fov_scene.player,0xd)=1; U(fov_scene.player,0x130)=0;
    U(fov_scene.camera,0x290)=(unsigned)fov_scene.eligibility_actor;
    U(fov_scene.eligibility_actor,0x118)=(unsigned)fov_scene.eligibility_health;
    U(fov_scene.eligibility_health,4)=(unsigned)fov_scene.eligibility_actor;
    F(fov_scene.eligibility_health,0x14)=100;
    U(fov_scene.level,0x24c)=(unsigned)fov_scene.eligibility_actor;
    U(fov_scene.level,0x178)=U(fov_scene.level,0x17c)=(unsigned)fov_scene.level+0x600;
    fov_eligibility_level=(unsigned)fov_scene.level;
    fov_eligibility_app=(unsigned)fov_scene.eligibility_app;
    U(fov_scene.eligibility_app,0x18738)=(unsigned)fov_scene.eligibility_state;
    U(fov_scene.eligibility_app,0x1876c)=0;
    U(fov_scene.eligibility_state,0)=(unsigned)fov_eligibility_vtable+8;
    U(fov_scene.eligibility_state,0x28)=(unsigned)fov_scene.level;
    fov_eligibility_ctor((unsigned)fov_scene.eligibility_mp);
    U(fov_scene.player,0x170)=(unsigned)fov_scene.weapon_manager;
    U(fov_scene.weapon_manager,0x148)=(unsigned)fov_scene.inventory;
    U(fov_scene.weapon_manager,0x14c)=(unsigned)fov_scene.inventory+24;
    U(fov_scene.inventory,12)=(unsigned)fov_scene.weapon_component;
    U(fov_scene.weapon_component,0x14)=(unsigned)fov_scene.weapon_config;
    F(fov_scene.weapon_config,0x11c)=90; F(fov_scene.weapon_config,0x120)=80;
    F(fov_scene.weapon_component,0x298)=20.f*720.f/1280.f*.017453292519943295f;
    F(fov_scene.weapon_component,0x29c)=10.f*720.f/1280.f*.017453292519943295f;
    fov_scene.diff_sample=.2f; fov_tweak=(unsigned)fov_scene.tweak;
    U(fov_scene.camera,0x26c)=(unsigned)fov_scene.nodes[0];
    fov_manager=(unsigned)fov_scene.manager;
    U(fov_scene.manager,0xdc)=(unsigned)fov_scene.nodes[0];
    U(fov_scene.component,0x108)=(unsigned)fov_scene.config;
    U(fov_scene.component,0x11c)=(unsigned)fov_scene.nodes[0];
    B(fov_scene.component,0x1ac)=1; F(fov_scene.config,0x6c)=60;
    F(fov_scene.config,0x70)=70; F(fov_scene.config,0x74)=1;
    fov_scene.next_node=1;
    extern void level_ctor(unsigned) __asm__("_ZN6CLevelC1Ev");
    level_ctor((unsigned)fov_scene.level);
    for(i=0;i<3;++i) native_ctor((unsigned)fov_scene.nodes[i],111,222);
}
float fov_body_diff(unsigned manager) {
    ++fov_scene.diff_calls; fov_scene.diff_manager=manager; fov_audit(6);
    if(fov_tweak && B(fov_tweak,0x58)) {
        volatile float q=F(fov_tweak,0x5c)-fov_default;
        volatile float product=q*(float)fov_height;
        volatile float quotient=product/(float)fov_width;
        return quotient*.017453292519943295f;
    }
    return fov_scene.diff_sample;
}

// Authored native SP inputs for the SAME production eligibility implementation.
unsigned fov_eligibility_current_state(unsigned stack) __asm__("_ZN12gxStateStack12CurrentStateEv");
unsigned fov_eligibility_current_state(unsigned stack) {
    const int index=(int)U(stack,0x34); return index<0 ? 0 : U(stack,(unsigned)index*4);
}
unsigned fov_eligibility_is_mp(unsigned manager) __asm__("_ZN9MpManager17IsMultiplayerGameEv");
unsigned fov_eligibility_is_mp(unsigned manager) { return 0xabcd0000u | (U(manager,4)!=0); }
unsigned fov_eligibility_cinematic(unsigned player) __asm__("_ZN15PlayerComponent19IsWatchingCinematicEv");
unsigned fov_eligibility_cinematic(unsigned player) { return 0xabcd0000u | (U(player,0x130)==3); }
unsigned fov_eligibility_health_dead(unsigned health) __asm__("_ZNK16CHealthComponent6IsDeadEv");
unsigned fov_eligibility_health_dead(unsigned health) {
    ++fov_scene.eligibility_dead_calls;
    if(U(fov_eligibility_mp,4) && U(fov_eligibility_mp,0x3c00)==2)
        return 0xabcd0000u | (B(health,0xe)!=0);
    return 0xabcd0000u | (F(health,0x14)<=0);
}
void fov_body_eligibility_ctor(unsigned manager) { U(manager,4)=0; fov_eligibility_mp=manager; }
void fov_body_eligibility_dtor(unsigned manager) { (void)manager; /* Singleton deliberately remains non-null. */ }

/* V01i supplied native fixture storage, independent of original asset data. */
extern void v01_rigid_draw_entry(unsigned);
extern void v01_rigid_model(unsigned,unsigned,unsigned) __asm__("_ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE");
void v01_rigid_absolute(void) {}
unsigned v01_rigid_buffer(void) { return 0; } /* Never entered by this gate. */
unsigned v01_rigid_node_table[48] = { [2]=(unsigned)&v01_rigid_draw_entry, [46]=(unsigned)&v01_rigid_absolute };
unsigned v01_rigid_mesh_table[18] = { [5]=(unsigned)&v01_rigid_buffer, [17]=(unsigned)&v01_rigid_model };
unsigned v01_rigid_calls, v01_rigid_args[3], v01_rigid_matrix[16];
void v01_rigid_model_body(unsigned mesh,unsigned driver,unsigned model) {
    unsigned i; ++v01_rigid_calls; v01_rigid_args[0]=mesh; v01_rigid_args[1]=driver; v01_rigid_args[2]=model;
    for(i=0;i<16;++i) v01_rigid_matrix[i]=U(model,i*4);
}
