#include "checked_memory.hpp"
#include "ai_controls.hpp"
#include "script_text.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
void Check(bool ok,const char* why) {if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    Check(ContainsBytes(0x1000,0x100,0x1000,0x100),"whole validated span accepted");
    Check(!ContainsBytes(0x1000,0x100,0x10ff,2) && !ContainsBytes(0x1000,0x100,0xfff,1),"out-of-range reads excluded");
    Check(!ContainsBytes(UINTPTR_MAX-7,16,UINTPTR_MAX-7,1),"overflowing span excluded");
    Check(!ContainsBytes(0x1000,16,0x1000,0),"empty reads rejected");
    std::string tag="previous";
    Check(DecodeScriptText("",1,tag) && tag.empty(),"empty root attachment string is loaded rather than a failed lookup");
    const char invalid[]={1,0};Check(!DecodeScriptText(invalid,sizeof(invalid),tag),"invalid string bytes are still rejected");
    const char unterminated[]={'a','b'};Check(!DecodeScriptText(unterminated,sizeof(unterminated),tag),"unbounded script text rejected");
    std::vector<std::uint8_t> arena(kAiEntityStride*kAiEntityCapacity);
    const std::uintptr_t base=0x100000;int fallbackReads=0;
    auto fallback=[&](auto,void*,std::size_t){++fallbackReads;return false;};
    auto read=[&](auto at,void* out,std::size_t size){return ReadArena(base,arena.data(),arena.size(),at,out,size,fallback);};
    for(int n=2;n<kAiEntityCapacity-2;++n) {
        auto* bytes=arena.data()+n*kAiEntityStride;
        const std::uint32_t serial=7;const std::uint16_t number=n;const std::uint8_t active=1;const int team=1,count=1;
        bytes[kAiType]=kActorSpawnerType;
        std::memcpy(bytes+kAiUseCount,&serial,4);std::memcpy(bytes+kAiNumber,&number,2);std::memcpy(bytes+kAiInUse,&active,1);
        std::memcpy(bytes+kAiSpawnerTeam,&team,4);std::memcpy(bytes+kAiSpawnCount,&count,4);
        Check(ReadAiEntity(base+n*kAiEntityStride,n,read).valid,"arena supplies complete spawner and serial validation");
    }
    Check(fallbackReads==0,"full 2048-entity scan does not reread serial fields through kernel");
    std::uint32_t value=0;Check(!read(base+arena.size(),&value,4) && fallbackReads==1,"outside-arena pointers use checked fallback");
    std::cout<<"Checked span bounds, overflow and full entity arena regression passed\n";
}
