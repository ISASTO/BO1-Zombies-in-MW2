#include "ai_controls.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

void Check(bool value,const char* why) { if(!value) { std::cerr<<"FAIL: "<<why<<'\n'; std::exit(1); } }
int main() {
    using namespace mod;
    AiEntity e;
    e.valid=true; e.inUse=true; e.number=9; e.address=0x1000; e.serial=4;
    e.type=kActorSpawnerType; e.team=kAxisTeam; e.spawnCount=2; e.classname="actor_enemy_test";
    Check(EligibleSpawner(e,kAxisTeam),"loaded ordinary enemy spawner admitted");
    e.flags=2; Check(!EligibleSpawner(e,kAxisTeam),"force-spawn flags cannot evict mission actors for debug creation");
    e.flags=0; e.spawnCount=0; Check(EligibleSpawner(e,kAxisTeam),"spent mission template can be cloned with an explicit temporary budget lease");
    const auto spent=SpawnerBudgetLease::plan(0);
    Check(spent.submitted==1 && spent.canRestore(1) && spent.canRestore(0),"successful or refused spent-template spawn restores zero budget");
    Check(!spent.canRestore(5),"script-changed budget must not be overwritten after native notification");
    const auto finite=SpawnerBudgetLease::plan(2);
    Check(finite.submitted==2 && finite.canRestore(1),"debug spawn can preserve a finite mission budget by guarded restore");
    e.spawnCount=-1; Check(EligibleSpawner(e,kAxisTeam),"native unlimited spawner budget accepted");
    e.team=kAlliesTeam; Check(!EligibleSpawner(e,kAxisTeam) && EligibleSpawner(e,kAlliesTeam),"native team mapping separates enemy and friendly requests");
    e.classname="actor_ally_hero_test"; Check(!EligibleSpawner(e,kAlliesTeam),"hero templates excluded from debug controls");
    e.classname="vehicle_test"; Check(!EligibleSpawner(e,kAlliesTeam),"actor type alone cannot admit a vehicle/script template");
    e.classname="actor_ally_test"; e.type=kActorType; e.actor=0x2000; e.sentient=0x3000; e.handler=2; e.health=100;
    Check(EligibleAiTemplate(e),"live ordinary ally can supply a model without a friendly spawner");
    e.team=kAxisTeam;Check(EligibleAiTemplate(e),"loaded enemy model can be converted to a friendly through native team transfer");
    e.health=0;Check(!EligibleAiTemplate(e),"dead actor cannot seed a new native template");e.health=100;
    e.classname="actor_ally_hero_test";Check(!EligibleAiTemplate(e),"hero VM state cannot be cloned for generic helpers");e.classname="actor_ally_test";
    const auto owned=AiIdentity::capture(e);
    Check(owned.matches(e),"own spawned identity records native serial and actor/sentient pointers");
    ++e.serial; Check(!owned.matches(e),"same entity slot with a new native allocation generation is excluded");
    e.serial=4; e.actor=0x2008; Check(!owned.matches(e),"changed actor attachment excluded");
    e.actor=0x2000; e.type=kActorCorpseType; Check(!owned.matches(e),"engine-owned corpse transition retires debug ownership");
    AiScope scope{true,0x1000,0x2000,100,"estate"};
    AiRequestQueue queue;
    Check(queue.enqueue(AiOperation::SpawnEnemy,owned,scope),"one bounded native request queued");
    Check(!queue.enqueue(AiOperation::SpawnFriendly,owned,scope),"pending request cannot be overwritten by rapid input");
    auto later=scope; later.commandTime=110;
    const auto request=queue.take(later);
    Check(request && request->operation==AiOperation::SpawnEnemy && !queue.pending(),"matching lifetime consumes once");
    Check(!queue.take(later),"request cannot repeat on another actor callback");
    queue.enqueue(AiOperation::SpawnEnemy,owned,scope); later.commandTime=99;
    Check(!queue.take(later) && queue.reason()==AiRequestResult::LifetimeChanged,"checkpoint rewind cancels before native invocation");
    queue.enqueue(AiOperation::SpawnEnemy,owned,scope); later=scope; later.map="cliffhanger";
    Check(!queue.take(later),"map transition cancels");
    queue.enqueue(AiOperation::SpawnEnemy,owned,scope); later=scope; later.pool=0x4000;
    Check(!queue.take(later),"changed native entity pool cancels");
    queue.enqueue(AiOperation::SpawnEnemy,owned,scope); later=scope; later.commandTime+=kAiRequestExpiryMilliseconds+1;
    Check(!queue.take(later) && queue.reason()==AiRequestResult::Expired,"unserviced request expires in game time");
    queue.enqueue(AiOperation::SpawnEnemy,owned,scope); later=scope; later.commandTime+=100;
    Check(!queue.discardStale(later) && queue.pending(),"render polling cannot consume a valid engine-thread command");
    later.commandTime+=kAiRequestExpiryMilliseconds;
    Check(queue.discardStale(later) && !queue.pending(),"no remaining actor callbacks cannot strand an expired request");
    auto noLevel=scope; noLevel.ready=false;
    Check(!queue.enqueue(AiOperation::SpawnEnemy,owned,noLevel),"front-end action cannot enter native queue");
    std::vector<std::uint8_t> bytes(0x4000);
    const auto entity=0x1000u;
    auto put=[&](std::size_t at,const auto& value) { std::memcpy(bytes.data()+at,&value,sizeof(value)); };
    put(entity+kAiType,std::uint8_t(kActorType)); put(entity+kAiUseCount,std::uint32_t(12));
    put(entity+kAiNumber,std::uint16_t(9)); put(entity+kAiInUse,std::uint8_t(1));
    put(entity+kAiHandler,std::uint8_t(2)); put(entity+kAiHealth,int(100));
    put(entity+kAiActor,std::uintptr_t(0x2000)); put(entity+kAiSentient,std::uintptr_t(0x3000));
    put(0x2000,std::uintptr_t(entity)); put(0x3000,std::uintptr_t(entity)); put(0x3008,int(kAxisTeam));
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at>bytes.size() || size>bytes.size()-at) return false;
        std::memcpy(out,bytes.data()+at,size); return true;
    };
    Check(ReadAiEntity(entity,9,read).valid,"verified partial actor fields with self links decode");
    put(entity+kAiClient,std::uintptr_t(0x2200));
    Check(!ReadAiEntity(entity,9,read).valid,"client-bearing entity excluded from native NPC control/delete");
    put(entity+kAiClient,std::uintptr_t(0));
    put(0x3000,std::uintptr_t(0x1100));
    Check(!ReadAiEntity(entity,9,read).valid,"sentient identity mismatch fails closed");
    put(0x3000,std::uintptr_t(entity)); put(entity+kAiNumber,std::uint16_t(8));
    Check(!ReadAiEntity(entity,9,read).valid,"native entity-number guard fails closed");
    std::cout<<"AI spawner eligibility, generation ownership, request cancellation and partial native field fixtures passed\n";
}
