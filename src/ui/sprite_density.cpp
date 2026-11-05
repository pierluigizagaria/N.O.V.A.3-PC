#include "ui/sprite_density.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace nova::ui {
namespace {
uint32_t word(uint32_t p,unsigned o) {uint32_t n;std::memcpy(&n,reinterpret_cast<void*>(p+o),4);return n;}
uint16_t half(uint32_t p,unsigned o) {uint16_t n;std::memcpy(&n,reinterpret_cast<void*>(p+o),2);return n;}
template<class T> T at(uint32_t p,unsigned o,unsigned i) {return reinterpret_cast<const T*>(word(p,o))[i];}
bool image(uint32_t p,unsigned i) {return !(half(p,0xc)&1) || at<uint8_t>(p,0x10,i)==0xfc;}
std::string image_family(uint32_t p,unsigned image_index) {
    const auto begin=word(p,0xa0),end=word(p,0xa4);
    if(end<begin || image_index>=(end-begin)/4) return {};
    auto name=sprite_family(reinterpret_cast<const char*>(word(begin,image_index*4)));
    auto dot=name.rfind('.');if(dot!=std::string::npos)name.resize(dot);
    if(name.ends_with("_big"))name.resize(name.size()-4);
    return name;
}
}
std::string sprite_family(std::string name) {
    for(auto& c:name) if(c>='A' && c<='Z')c=char(c+'a'-'A');
    const auto dot=name.rfind('.'),underscore=name.rfind('_',dot);
    if(dot==std::string::npos || underscore==std::string::npos)return name;
    const auto suffix=name.substr(underscore+1,dot-underscore-1);auto x=suffix.find('x');
    if(x!=std::string::npos && x>0 && x+1<suffix.size() &&
       std::all_of(suffix.begin(),suffix.end(),[](char c){return (c>='0'&&c<='9')||c=='x';}) && suffix.find('x',x+1)==std::string::npos)
        name.erase(underscore,dot-underscore);
    return name;
}
std::vector<DenseModule> sprite_density_plan(uint32_t base,uint32_t dense) {
    const auto modules=half(base,0xe),other_modules=half(dense,0xe),frames=half(base,0x48);
    if(!modules || !other_modules || frames!=half(dense,0x48))return {};
    std::vector<int> mapping(modules,-1);
    for(unsigned f=0;f<frames;++f) {
        const auto count=at<uint8_t>(base,0x4c,f);
        if(count!=at<uint8_t>(dense,0x4c,f))continue;
        const auto start=at<uint16_t>(base,0x50,f),other=at<uint16_t>(dense,0x50,f);
        if(unsigned(start)+count>half(base,0x34) || unsigned(other)+count>half(dense,0x34))return {};
        for(unsigned n=0;n<count;++n) {
            const auto flags=at<uint8_t>(base,0x3c,start+n);
            if(flags!=at<uint8_t>(dense,0x3c,other+n) || (flags&16))continue;
            const auto a=at<uint16_t>(base,0x38,start+n),b=at<uint16_t>(dense,0x38,other+n);
            if(a>=modules || b>=other_modules)return {};
            auto& entry=mapping[a];if(entry==-1)entry=b;else if(entry!=b)entry=-2;
        }
    }
    std::vector<DenseModule> result;
    for(unsigned a=0;a<modules;++a) if(mapping[a]>=0 && image(base,a) && image(dense,mapping[a])) {
        const unsigned b=static_cast<unsigned>(mapping[a]);
        const int w=at<int16_t>(base,0x1c,a),h=at<int16_t>(base,0x20,a),dw=at<int16_t>(dense,0x1c,b),dh=at<int16_t>(dense,0x20,b);
        // Provisional T09s: only proportional authored replacements. Rounded,
        // trimmed, removed or redesigned modules retain their original art.
        if(w<=0 || h<=0 || dw<=w || dh<=h || int64_t(dw)*h!=int64_t(dh)*w)continue;
        const auto bi=at<uint8_t>(base,0x14,a),di=at<uint8_t>(dense,0x14,b);
        if(image_family(base,bi).empty() || image_family(base,bi)!=image_family(dense,di))continue;
        result.push_back({a,di,at<int32_t>(dense,0x24,b),at<int32_t>(dense,0x28,b),dw,dh});
    }
    return result;
}
}
