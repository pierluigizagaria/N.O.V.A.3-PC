#include "net/pc_snapshot.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char *reason) { if(!value) throw std::runtime_error(reason); }
std::vector<uint8_t> unhex(const std::string &text) {
    std::vector<uint8_t> out;
    for(size_t i=0;i<text.size();i+=2) out.push_back(uint8_t(std::stoul(text.substr(i,2),nullptr,16)));
    return out;
}
}
int main() {
    try {
        using namespace nova::net::pc;
        Snapshot s{2,0x01020304,0xfffffffe,1000,{1,-2,0.5f},0x4000,0xffff};
        const auto body=encode_snapshots(std::span(&s,1));
        check(body && *body==unhex("010201020304fffffffe000003e83f800000c00000003f0000004000ffff"),"Go/C++ synthetic snapshot golden");
        auto packet=encode(snapshots,*body);
        check(packet.has_value(),"snapshot envelope");
        const auto frame=decode(*packet);
        check(frame && frame->kind==snapshots,"decode envelope");
        const auto decoded=decode_snapshots(frame->payload);
        check(decoded && decoded->size()==1 && (*decoded)[0]==s,"round trip");
        const std::array<Life,2> livesIn{{{2,0x01020304},{11,0xffffffff}}};
        const auto livesBody=encode_lives(livesIn);
        check(livesBody && *livesBody==unhex("0202010203040bffffffff"),"Go/C++ life golden");
        const auto livesPacket=encode(lives,*livesBody);
        check(livesPacket && decode(*livesPacket)->kind==lives,"life envelope");
        check(*decode_lives(*livesBody)==std::vector<Life>(livesIn.begin(),livesIn.end()),"life round trip");
        for(size_t n=0;n<livesPacket->size();++n) check(!decode(std::span(livesPacket->data(),n)),"truncated life packet");
        for(const auto &bad:{unhex("00"),unhex("010c00000001"),unhex("010000000000"),unhex("0200000000010000000002"),unhex("01000000000100")})
            check(!decode_lives(bad) && !encode(lives,bad),"invalid life declaration");
        for(size_t n=0;n<packet->size();++n) check(!decode(std::span(packet->data(),n)),"truncation accepted");
        auto wrong=*packet;wrong[4]=2;check(!decode(wrong),"unknown version accepted");
        wrong=*packet;wrong.push_back(0);check(!decode(wrong),"trailing bytes accepted");
        for(double angle:{-360.1,-0.1,0.0,0.1,1.25,90.0,359.999,360.0}) {
            auto a=encode_angle(angle);check(a && std::abs(std::remainder(decode_angle(*a)-angle,360))<=180.0/65536+1e-8,"angle precision/wrap");
        }
        check(!encode_angle(std::numeric_limits<double>::infinity()),"infinite angle accepted");
        s.position[0]=std::numeric_limits<float>::quiet_NaN();
        check(!encode_snapshots(std::span(&s,1)),"NaN position accepted");
        check(newer(0,0xffffffff) && !newer(0xffffffff,0) && !newer(0x80000000,0),"serial ordering");
        History history;history.reset(1,9);
        Snapshot a{1,9,0xfffffffe,0xfffffff0,{0,0,0},*encode_angle(359),*encode_angle(1)};
        Snapshot b=a;b.sequence=0xffffffff;b.time+=34;b.position={34,68,102};b.yaw=*encode_angle(1);b.pitch=*encode_angle(359);
        check(history.push(a) && history.push(b),"valid history rejected");
        check(!history.push(a) && !history.push(b),"stale history accepted");
        auto pose=history.sample(a.time+17+presentation_delay_ms);
        check(pose && pose->position==std::array<float,3>{17,34,51},"shared position time or clock wrap");
        check(std::abs(std::remainder(pose->yaw,360.0f))<0.01 && std::abs(std::remainder(pose->pitch,360.0f))<0.01,"angle interpolation crossed long arc");
        auto old=b;old.generation=10;check(!history.push(old),"snapshot authored a generation");
        history.reset(1,10);check(!history.sample(1000) && !history.push(b),"respawn retained old life");
        check(history.push(old),"new generation rejected");
        pose=history.sample(5000);check(pose && pose->position==old.position,"unbounded outage extrapolation");
        history.reset(1,11);check(!history.push(old),"new match retained stale generation");
        Cadence cadence;
        constexpr uint64_t epoch=123456789;
        check(cadence.due(epoch),"initial cadence sample");
        for(uint64_t n=1;n<=30*3600;++n) {
            const auto deadline=epoch+(n*1000000000+29)/30;
            check(!cadence.due(deadline-1) && cadence.due(deadline) && !cadence.due(deadline),"30 Hz drift or duplicate");
        }
        const auto stalled=epoch+7200050000000;
        check(cadence.due(stalled) && !cadence.due(stalled) && !cadence.due(epoch),"stall catch-up or backward clock");
        std::puts("PASS PC snapshot codec and coherent presentation history");
    } catch(const std::exception &e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 1; }
}
