void camera_bob_connected(const nova::ElfImage& image,const std::vector<LensCallSite>& sites) {
    using namespace nova::movement_wobble;
    CodeSnapshot code(image);
    {
        NativeWorldFov live(image,100.f,&sites,nullptr,90.f,true);live.bind_render_thread();
        Harness h(image);h.reset();O09NativeBinding binding(live,h);binding.ordinary();
        RequestedStrength request(Strength::from_percent(0));NativeCameraBob bob(image,request,true);bob.bind_render_thread();
        std::array<uint8_t,0x144> parent{},basis{};
        std::array<uint8_t,0x108> object{};
        std::array<uint8_t,0x40> animation{},slot{},timeline{};
        const char camera_uid[]="Camera01-node",basis_uid[]="dummy_camera-node";
        uint32_t parent_metadata=ptr(camera_uid),basis_metadata=ptr(basis_uid);
        const auto p=h.node(),weapon=ptr(h.scene.weapon_component),table=image.symbol("fov_bob_node_table");
        at(ptr(parent))=at(ptr(basis))=table;at(ptr(parent),0x134)=ptr(parent_metadata);at(ptr(basis),0x134)=ptr(basis_metadata);
        at(p,0xd0)=ptr(parent);at(ptr(parent),0xd0)=ptr(basis);at(ptr(h.scene.camera),0x270)=ptr(basis);
        at(weapon,4)=ptr(object);at(ptr(object),0x100)=ptr(animation);at(ptr(animation),4)=ptr(object);
        at(ptr(animation),0x14)=1;at(ptr(animation),0x20)=ptr(slot);at(weapon,0xf4)=0;
        at(ptr(slot))=at(ptr(timeline),0x30)=56;at(ptr(slot),0x28)=ptr(timeline);at(ptr(slot),0x34)=0x1000;
        const auto untouched_animation=animation,untouched_slot=slot,untouched_timeline=timeline;
        const CameraView reference{{0,0,0},{0,1,0},{0,0,1}};
        auto put=[&](const CameraView& value){
            std::memcpy(reinterpret_cast<void*>(p+0x40),value.eye.data(),12);
            std::memcpy(reinterpret_cast<void*>(p+0x10c),value.target.data(),12);
            std::memcpy(reinterpret_cast<void*>(p+0x118),value.up.data(),12);
        };
        auto observe=[&]{CameraView v;std::memcpy(v.eye.data(),reinterpret_cast<void*>(p+0x40),12);
            std::memcpy(v.target.data(),reinterpret_cast<void*>(p+0x10c),12);std::memcpy(v.up.data(),reinterpret_cast<void*>(p+0x118),12);return v;};
        at(image.symbol("fov_bob_audit_enabled"))=1;
        for(unsigned step=0;step<5;++step) {
            const int percent=step==2 ? 50 : step==3 ? 100 : 0;
            request.set(Strength::from_percent(percent));
            if(step==4)at(ptr(slot))=at(ptr(timeline),0x30)=7; // Action animation: exact Native camera.
            const ViewMatrix matrix=step==0 ? ViewMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1} :
                ViewMatrix{0,1,0,0,-1,0,0,0,0,0,1,0,20,30,4,1};
            std::memcpy(basis.data()+0x10,matrix.data(),64);
            CameraView local=reference;if(step){local.eye[0]=.1f;local.target[0]=.2f;local.up[0]=.03f;}
            const auto native=transform_view(matrix,local);
            const auto expected=step==4 ? native : reduce_camera_view(native,reference,matrix,percent);
            put(native);bob.prepare_step();unsigned seen=0;
            query_callback=[&](uint32_t kind){if(kind==9){++seen;const auto actual=observe();
                for(unsigned i=0;i<3;++i){close_value(actual.eye[i],expected.eye[i],"bob first native VIEW eye");
                    close_value(actual.target[i],expected.target[i],"bob first native VIEW target");
                    close_value(actual.up[i],expected.up[i],"bob first native VIEW up");}}};
            check(invoke(h.fn("fov_bob_registration"),{p,0})==0xa6ebc901u,"camera registration preserves original EAX/AL");
            h.matrices(p); // A subsequent FOV rebuild must retain the same camera correction.
            query_callback={};check(seen>=2,"camera registration and subsequent native rebuild observed");
            check(observe()==native,"camera scope restores native scene inputs exactly");bob.finish_step();
            if(step<4)check(animation==untouched_animation && slot==untouched_slot && timeline==untouched_timeline,
                "camera bob never edits animation state/timeline/buffers");
        }
        at(image.symbol("fov_bob_audit_enabled"))=0;
        check(bob.counters().admitted==3 && bob.counters().matrices>=6,"connected zero/half routing actually reached native builders");
    }
    code.unchanged();std::puts("Connected camera bob: native registration, zero/half/100, action bypass, FOV rebuild and exact restoration passed");
}
