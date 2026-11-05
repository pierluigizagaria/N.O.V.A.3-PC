#include "sprite_fixture.h"
using namespace sprite_fixture;
void array(uint32_t object,unsigned offset,unsigned count,bool bytes=false,bool signed_values=false) {
    auto p=word(object,offset);std::printf("\"%x\":[",offset);
    for(unsigned i=0;i<count;++i) { if(i) std::putchar(',');
        std::printf("%d",bytes?reinterpret_cast<uint8_t*>(p)[i]:signed_values?reinterpret_cast<int16_t*>(p)[i]:reinterpret_cast<uint16_t*>(p)[i]); }
    std::putchar(']');
}
int main(int argc,char** argv) {
    if(argc<3 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2])) return 77;
    try {
        Runtime r(argv[1]);std::puts("[");bool first=true;
        for(const auto& file:std::filesystem::directory_iterator(argv[2])) if(file.path().extension()==".bsprite") {
            Sprite sprite(r,file.path());auto p=sprite.address();if(!first)std::puts(",");first=false;
            std::printf("{\"name\":\"%s\",\"counts\":[%u,%u,%u,%u],\"images\":[",file.path().filename().string().c_str(),half(p,0x98),half(p,0xe),half(p,0x34),half(p,0x48));
            for(auto s=word(p,0xa0);s<word(p,0xa4);s+=4) {if(s!=word(p,0xa0))std::putchar(',');std::printf("\"%s\"",reinterpret_cast<char*>(word(s)));}std::printf("],");
            array(p,0x14,half(p,0xe),true);std::putchar(',');array(p,0x1c,half(p,0xe),false,true);std::putchar(',');array(p,0x20,half(p,0xe),false,true);
            std::putchar(',');array(p,0x38,half(p,0x34));std::putchar(',');array(p,0x3c,half(p,0x34),true);std::putchar(',');array(p,0x40,half(p,0x34),false,true);std::putchar(',');array(p,0x44,half(p,0x34),false,true);
            std::putchar(',');array(p,0x4c,half(p,0x48),true);std::putchar(',');array(p,0x50,half(p,0x48));std::printf("}");
        }
        std::puts("\n]");check(allocated==0,"native sprite allocations balanced");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"sprite catalog FAIL %s\n",e.what());return 1;}
}

