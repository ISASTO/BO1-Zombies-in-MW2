#include "ai_placement.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
void Check(bool ok,const char* why) {if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    const AiPoint player{100,200,10};
    auto candidates=SpawnCandidates(player,true);
    Check(candidates.size()==24 && PointDistanceSquared(candidates.front(),player)==96*96,"friendly placement leaves room around the player's body");
    Check(SpawnCandidates(player,false).front()==player,"enemy retries begin at the actual closest spawn point");
    int bodies=0;
    auto trace=[&](const AiPoint& start,const AiPoint& end,const AiBounds& bounds) {
        AiTraceResult r; r.valid=true; r.normal={0,0,1};
        if(bounds.halfSize[2]==0) r.fraction=(start[2]-10)/(start[2]-end[2]);
        else {r.fraction=1; if(++bodies==1){r.startSolid=true;r.fraction=0;}}
        return r;
    };
    auto result=FindSpawnPlacement(player,false,trace);
    Check(result && result->attempts==2 && result->position[2]==11 && result->position!=player,"occupied enemy point retries nearby ground with full actor clearance");
    auto cliff=[](const AiPoint&,const AiPoint&,const AiBounds&) {AiTraceResult r;r.valid=true;return r;};
    Check(!FindSpawnPlacement(player,true,cliff),"no ground never permits a floating spawn");
    auto steep=[](const AiPoint&,const AiPoint&,const AiBounds&) {AiTraceResult r;r.valid=true;r.fraction=.5;r.normal={.9f,0,.1f};return r;};
    Check(!FindSpawnPlacement(player,false,steep),"nonwalkable surface rejected");
    auto solid=[](const AiPoint&,const AiPoint&,const AiBounds&) {AiTraceResult r;r.valid=true;r.fraction=.5;r.normal={0,0,1};r.allSolid=true;return r;};
    Check(!FindSpawnPlacement(player,true,solid),"solid ground trace cannot be treated as placement");
    auto farFloor=[](const AiPoint&,const AiPoint&,const AiBounds&) {AiTraceResult r;r.valid=true;r.fraction=.9;r.normal={0,0,1};return r;};
    Check(!FindSpawnPlacement(player,true,farFloor),"friendly cannot spawn on another floor far below the player");
    AiPoint bad=player;bad[0]=std::numeric_limits<float>::quiet_NaN();
    Check(SpawnCandidates(bad,true).empty(),"invalid world coordinates cannot enter native traces");
    std::array<std::uint8_t,48> raw{};float fraction=.25f;std::memcpy(raw.data(),&fraction,4);float normal=1;std::memcpy(raw.data()+12,&normal,4);
    Check(DecodeAiTrace(raw).valid && DecodeAiTrace(raw).normal[2]==1,"native fraction/normal decode");
    raw[0x2d]=1;Check(DecodeAiTrace(raw).startSolid,"native actor initializer's confirmed startsolid field");
    raw[0x2d]=2;Check(!DecodeAiTrace(raw).valid,"invalid trace boolean fails closed");
    std::cout<<"Ground, occupied-space retry, near-player floor and native trace fixtures passed\n";
}
