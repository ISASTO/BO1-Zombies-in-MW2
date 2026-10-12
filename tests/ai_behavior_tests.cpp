#include "ai_behavior.hpp"
#include "native_hooks.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
void Check(bool value,const char* why) {if(!value){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    AiEntity self;self.valid=true;self.type=kActorType;self.number=3;self.address=0x1000;self.health=100;self.team=kAxisTeam;
    auto friendNpc=self;friendNpc.number=4;friendNpc.address=0x2000;friendNpc.team=kAlliesTeam;friendNpc.origin={100,0,0};
    auto sameTeam=self;sameTeam.number=5;sameTeam.address=0x3000;sameTeam.origin={10,0,0};
    const AiPoint player{500,0,0};
    auto target=ChooseAiTarget(self,kAlliesTeam,0x9000,player,{friendNpc,sameTeam});
    Check(target.entity==friendNpc.address,"enemy prefers nearer allied NPC and excludes own team");
    friendNpc.health=0;target=ChooseAiTarget(self,kAlliesTeam,0x9000,player,{friendNpc});
    Check(target.entity==0x9000,"enemy engages player when no nearer living ally exists");
    self.team=kAlliesTeam;sameTeam.team=kAxisTeam;sameTeam.origin={300,0,0};
    Check(ChooseAiTarget(self,kAlliesTeam,0x9000,player,{sameTeam}).entity==sameTeam.address,"friendly targets hostiles and never the player");
    sameTeam.origin={2600,0,0};
    Check(!ChooseAiTarget(self,kAlliesTeam,0x9000,player,{sameTeam}).entity,"friendly follows when threats are outside local combat range");
    Check(!NeedsAiGoal(true,{0,0,0},{20,0,0}) && NeedsAiGoal(true,{0,0,0},{100,0,0}),"small movement preserves native path instead of resetting it");
    Check(BodiesOverlap({0,0,0},{30,20,0}) && !BodiesOverlap({0,0,0},{40,0,0}) && !BodiesOverlap({0,0,0},{0,0,80}),"actor occupancy rejects overlapping bodies but permits another floor");
    AiEntity templateNpc;templateNpc.valid=true;templateNpc.inUse=true;templateNpc.type=kActorSpawnerType;
    templateNpc.number=8;templateNpc.team=kAxisTeam;templateNpc.classname="actor_enemy_test_AR";
    Check(HumanSpawner(templateNpc),"real human spawner accepted");
    templateNpc.type=kActorType;Check(!HumanSpawner(templateNpc),"live actor VM state cannot be cloned");
    templateNpc.type=kActorSpawnerType;templateNpc.classname="actor_enemy_dog";Check(!HumanSpawner(templateNpc),"dogs cannot supply default human helpers");
    std::vector<std::uint8_t> bytes(0x3000);AiIdentity identity;identity.address=0x1000;identity.serial=7;identity.number=9;
    auto put=[&](std::size_t at,const auto& value){std::memcpy(bytes.data()+at,&value,sizeof(value));};
    put(0x1000+kAiUseCount,std::uint32_t(7));put(0x1000+kAiNumber,std::uint16_t(9));put(0x1000+kAiInUse,std::uint8_t(1));
    put(0x1000+kAiType,std::uint8_t(kActorCorpseType));
    auto read=[&](std::uintptr_t at,void* out,std::size_t size){if(at+size>bytes.size())return false;std::memcpy(out,bytes.data()+at,size);return true;};
    Check(SameAiAllocation(identity,ReadAiAllocation(identity,read)),"corpse transition retains allocation ticket without brain pointer");
    put(0x1000+kAiUseCount,std::uint32_t(8));Check(!SameAiAllocation(identity,ReadAiAllocation(identity,read)),"reused slot is excluded from cleanup");
    put(0x1000+kAiUseCount,std::uint32_t(7));put(0x1000+kAiClient,std::uintptr_t(0x2000));
    Check(!SameAiAllocation(identity,ReadAiAllocation(identity,read)),"client-bearing allocation is excluded");
    std::array<std::uint8_t,5> code{};
    Check(RelativeCall(0x1000,0x2000,code) && code[0]==0xe8,"native call relay encodes near target");
    std::int32_t rel=0;std::memcpy(&rel,code.data()+1,4);Check(0x1005+rel==0x2000,"call displacement resolves to relay");
    Check(!RelativeCall(0x1000,0x900000000ULL,code),"far call is rejected until a near relay exists");
    const auto jump=AbsoluteJump(0x123456789ULL);std::uintptr_t pointer=0;std::memcpy(&pointer,jump.data()+6,8);
    Check(jump[0]==0xff && jump[1]==0x25 && pointer==0x123456789ULL,"absolute relay preserves full target pointer");
    std::cout<<"AI combat, occupancy, allocation lifetime and native relay fixtures passed\n";
}
