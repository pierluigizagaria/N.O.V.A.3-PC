// Exercise the production sample selector against authored native layouts.
// No game library, GPU, profile or copied animation data is needed.
#include "movement_wobble/native_premix_locomotion.cpp"
#include <stdexcept>

namespace nova::movement_wobble {
void camera_binding_tests() {
    auto check=[](bool value,const char *message) { if(!value)throw std::runtime_error(message); };
    auto put=[](auto &storage,size_t offset,uint32_t value) { std::memcpy(reinterpret_cast<uint8_t*>(storage.data())+offset,&value,4); };
    auto address=[](auto &storage) { return reinterpret_cast<uint32_t>(storage.data()); };
    std::array<uint8_t,0x60> set{},cookie{},layout{},main{};
    std::array<uint8_t,0x100> resource{},object{};
    std::array<uint8_t,0x144> root{};
    std::array<std::array<uint8_t,0x144>,3> nodes{};
    std::array<std::array<uint32_t,1>,3> snodes{};
    std::array<std::array<uint32_t,8>,3> descriptors{};
    std::array<std::array<uint32_t,3>,3> channels{};
    std::array<uint32_t,138*8> records{};
    std::array<uint32_t,8> mapping{};
    std::array<uint32_t,4> tracks{},targets{};
    std::array<uint16_t,4> offsets{0,16,32,48},all{0,1,2,3};
    std::array<uint16_t,1> weapon_only{0};
    std::array<uint8_t,4> sizes{12,16,12,12};
    std::array<uint32_t,4> list_all{0,0,address(all),address(all)+sizeof(all)};
    std::array<uint32_t,4> list_weapon{0,0,address(weapon_only),address(weapon_only)+sizeof(weapon_only)};
    std::array<uint32_t,2> size_vector{address(sizes),address(sizes)+sizeof(sizes)};
    uint32_t offset_pointer=address(offsets);
    std::array<float,16> samples{};
    std::array<uint32_t,4> buffer{0,address(layout),address(samples),1};
    std::array<uint32_t,2> unit{address(buffer),0};
    uint32_t cookie_pointer=address(cookie);
    constexpr uint32_t node_table=0x12340000,position_table=0x12340010,rotation_table=0x12340020;
    std::array<uint32_t,3> receivers{position_table,rotation_table,position_table};
    const char *uids[]={"Bip01_R_Hand-node","Camera01-node","dummy_camera-node"};
    const uint32_t indices[]={98,101,29};
    for(unsigned i=0;i<3;++i) {
        const uint32_t type=i==1 ? 5u : 1u,size=i==1 ? 16u : 12u;
        targets[i]=address(nodes[i]);tracks[i]=ptr(&receivers[i]);
        snodes[i][0]=ptr(uids[i]);channels[i]={ptr(uids[i]),type,0};
        descriptors[i][1]=3;descriptors[i][7]=size<<16;
        const auto record=address(records)+indices[i]*32;
        records[indices[i]*8+2]=address(descriptors[i]);
        records[indices[i]*8+4]=address(channels[i]);
        records[indices[i]*8+5]=tracks[i];
        mapping[i*2]=2;mapping[i*2+1]=record;
        put(nodes[i],0,node_table);put(nodes[i],0xd0,address(root));put(nodes[i],0x134,address(snodes[i]));
    }
    put(set,0x18,address(tracks));put(set,0x1c,address(tracks)+sizeof(tracks));put(set,0x50,address(mapping));
    put(resource,0x28,address(records));put(object,0x98,address(root));
    put(cookie,0x24,address(set));put(cookie,0x18,address(list_all));
    put(cookie,0x1c,address(list_weapon));put(cookie,0x20,address(list_weapon));
    put(cookie,0x34,address(size_vector));put(cookie,0x3c,address(targets));
    put(layout,0x34,address(size_vector));put(layout,0x38,ptr(&offset_pointer));
    Owner o;o.set=address(set);o.root=address(resource);o.object=address(object);
    auto select=[&] { return inputs(o,address(main),ptr(&cookie_pointer),address(unit),rotation_table,position_table,node_table); };
    for(uint32_t mode:{0u,2u,3u}) {
        put(cookie,0x2c,mode);const auto chosen=select();
        check(chosen.valid && chosen.count==2,"native selected list includes both camera channels only");
        check(chosen.values[0].output==address(samples)+16 && chosen.values[1].output==address(samples)+32,
              "weapon sample is excluded from the production write set");
    }
    put(cookie,0x2c,1);check(!select().valid,"mode1 uses +1c, not stale +18 camera entries");
    put(cookie,0x2c,2);put(cookie,0x10,1);check(!select().valid,"mode2 alternate uses +20");
    put(cookie,0x20,address(list_all));check(select().count==2,"live alternate list admits its actual camera cohort");
    put(nodes[1],0x134,address(snodes[0]));check(!select().valid,"camera UID must match the current target metadata");
    put(nodes[1],0x134,address(snodes[1]));put(nodes[1],0xd0,0);
    check(!select().valid,"unowned camera cannot be changed");
}
}
int main() {
    try { nova::movement_wobble::camera_binding_tests(); std::puts("Camera-only native sample binding passed");return 0; }
    catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
