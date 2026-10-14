#include "expanded_actor_layout.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
void Check(bool value,const char* why){if(!value){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int main(){
    using namespace mod::expansion;
    Check(ArenaLayoutFits(),"expanded state blocks and guard pages are disjoint");
    for(const auto& range:kArenaDataRanges)
        Check(ArenaReadable(range.rva-kArenaRva,range.bytes),"complete native resource remains readable");
    for(const auto& guard:kArenaGuards){
        const auto begin=guard.rva-kArenaRva;
        Check(!ArenaReadable(begin,1) && !ArenaReadable(begin-1,2) && !ArenaReadable(begin+4095,2),"reads into or across guard pages excluded");
        Check(ArenaReadable(begin-1,1) && ArenaReadable(begin+4096,1),"adjacent committed bytes remain usable");
    }
    Check(!ArenaReadable(std::numeric_limits<std::uintptr_t>::max(),8),"wrapped arena offset rejected");
    Check(!ArenaReadable(1,std::numeric_limits<std::size_t>::max()),"wrapped arena length rejected");
    for(std::size_t actor=0;actor<kActors;++actor)for(std::size_t sentient=0;sentient<kSentients;++sentient){
        const auto offset=PerceptionOffset(sentient);
        Check(offset && *offset>=kOldStride && *offset+kRecordBytes<=kStride,"all perception rows fit without changing native actor fields");
        Check(ArenaReadable(actor*kStride+*offset,kRecordBytes),"highest actor and sentient pair fits before the pool guard");
    }
    Check(!PerceptionOffset(kSentients),"sentient row 274 rejected");
    Check(SnapshotIndex(false,255)==255 && SnapshotIndex(true,63)==319,"combined snapshot index requires and retains nine bits");
    Check(!SnapshotIndex(false,256) && !SnapshotIndex(true,64),"snapshot allocation beyond both bounds rejected");
    std::cout<<"Expanded resource, guard, 70144 perception pairs and snapshot bounds passed\n";
}
