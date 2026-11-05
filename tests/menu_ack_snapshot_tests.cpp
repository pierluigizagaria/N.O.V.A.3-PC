// Host-only field, consumption and Script fixture. No guest-entered callbacks.
#include "menu/menu_ack_snapshot.h"
#include "input/script.h"
#include <map>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cstring>
using namespace nova::diagnostic::t09h;
static int failures=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"menu-ack check line %d: %s\n",__LINE__,#x);++failures;}}while(false)
struct Reader {
 std::map<uint32_t,std::vector<unsigned char>> memory;
 uint32_t address(uint32_t a,size_t=1)const{return a;}
 // Host-only mocks; never registered or entered from guest/native code.
 unsigned transition_query=0,level_calls=0,player_calls=0;
 bool transitioned=false;
 mutable unsigned stale_splash_reads=0;
 void switch_to_menu(){put<uint32_t>(0x201873c,0x3001000);transitioned=true;}
 uint32_t app()const{return 0x2000000;}
 uint32_t level(){++level_calls;if(transition_query==1)switch_to_menu();return transition_query?0x5000000:0;}
 uint32_t player(uint32_t){++player_calls;if(transition_query==2)switch_to_menu();return 0;}
 uint32_t field(uint32_t p,uint32_t offset)const{if(!p || uint64_t(p)+offset>UINT32_MAX)throw std::runtime_error("bad field");return p+offset;}
 template<class T> void put(uint32_t p,T v){auto& b=memory[p];b.resize(sizeof v);std::memcpy(b.data(),&v,sizeof v);}
 template<class T> T read(uint32_t p)const {if(transitioned && p>=0x3000000 && p<0x300004c)++stale_splash_reads;auto i=memory.find(p);if(i==memory.end() || i->second.size()!=sizeof(T))throw std::runtime_error("missing field");T v;std::memcpy(&v,i->second.data(),sizeof v);return v;}
};
int main(){
 Reader r;r.put<int32_t>(0x201876c,1);r.put<uint32_t>(0x201873c,0x3000000);r.put<uint32_t>(0x2018738,0x3001000);
 r.put<uint32_t>(0x3000000,0x11079c8);r.put<uint32_t>(0x3001000,0x1108488);
 r.put<uint8_t>(0x3000028,0);r.put<uint8_t>(0x3000029,0);
 for(uint32_t p=0x115f838;p<=0x115f83a;++p)r.put<uint8_t>(p,0);
 r.put<uint32_t>(0x11568c4,0x4000000);r.put<uint32_t>(0x4000000,0x1107e28);
 r.put<std::array<int16_t,8>>(0x4000008,{0,0,1920,1080,0,0,1920,1080});
 nova::menu::NativeMenuObserver::Snapshot cpu;cpu.enabled=cpu.complete=true;
 nova::menu::RttBackingCapture::Snapshot backing;backing.enabled=true;
 for(unsigned i=0;i<2;++i){auto& t=backing.targets[i];t.registered=true;t.manager=9;t.generation=1;t.receiver=i+1;}
 auto s=read_snapshot(r,534,false,cpu,backing);CHECK(s.width==1920 && s.height==1080);
 CHECK(s.state.underlying_menu && s.state.acknowledgement_clear && s.state.moga_clear);
 Gate g;CHECK(g.observe(s.state).action==Action::dispatch_balanced_pair);
 // Both original query sites may invalidate current state while old storage stays readable.
 for(unsigned query=1;query<=2;++query){
  Reader switched=r;switched.transition_query=query;switched.level_calls=switched.player_calls=0;
  const auto fresh=read_snapshot(switched,535,false,cpu,backing);
  CHECK(switched.level_calls==1 && switched.player_calls==1 && switched.transitioned);
  CHECK(fresh.state.state==State::menu && fresh.state.splash_identity==0);
  CHECK(switched.stale_splash_reads==0);
  Gate reject;CHECK(reject.observe(fresh.state).action==Action::stop_failed);
  // Do not erase or poison old storage: the stale-read bug must fail on readable memory.
  CHECK(switched.read<uint32_t>(0x3000000)==0x11079c8);
  CHECK(switched.read<uint8_t>(0x3000028)==0 && switched.read<uint8_t>(0x3000029)==0);
 }
 r.put<uint8_t>(0x115f839,1);s=read_snapshot(r,534,false,cpu,backing);CHECK(!s.state.moga_clear);
 r.put<uint8_t>(0x115f839,0);r.memory.erase(0x3000028);bool threw=false;
 try{(void)read_snapshot(r,534,false,cpu,backing);}catch(const std::runtime_error&){threw=true;}CHECK(threw);
 struct Receipt {uint64_t down,up;bool held;};
 CHECK(balanced_pair(Receipt{0,0,false},Receipt{1,1,false},2));
 CHECK(!balanced_pair(Receipt{0,0,false},Receipt{1,0,true},1));
 CHECK(!balanced_pair(Receipt{0,0,false},Receipt{1,1,true},2));
 CHECK(!balanced_pair(Receipt{0,0,false},Receipt{2,1,false},2));
 std::istringstream text("frame 0\ndown 640 360\nup 640 360\n");
 auto script=nova::input::Script::parse(text);CHECK(script.size()==2 && script.last_frame()==0 && script.scheduled(0));
 nova::input::Script empty;CHECK(empty.size()==0 && !empty.scheduled(0));
 return failures?1:0;
}
