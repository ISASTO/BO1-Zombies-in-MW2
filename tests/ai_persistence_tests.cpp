#include "ai_persistence.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

void Check(bool ok,const char* why) {if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    Check(OwnedActorHelper(OwnedActorTag(27))==27,"saved ownership uses entity numbers, independent of relocated pointers/string IDs");
    for(const auto* text:{"ghost","bo1mw2_actor_v1_","bo1mw2_actor_v1_0","bo1mw2_actor_v1_1",
        "bo1mw2_actor_v1_-2","bo1mw2_actor_v1_02","bo1mw2_actor_v1_2046","bo1mw2_actor_v1_27tail",
        "bo1mw2_actor_v1_999999999999999999999999999999"})
        Check(!OwnedActorHelper(text),"malformed/mission tags and reserved entity slots cannot claim checkpoint ownership");
    AiEntity actor;actor.valid=actor.inUse=true;actor.type=kActorType;actor.number=10;actor.health=100;actor.team=kAlliesTeam;
    AiEntity helper;helper.valid=helper.inUse=true;helper.type=kActorSpawnerType;helper.number=27;helper.team=kAlliesTeam;
    helper.classname="actor_enemy_soldier";
    Check(RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"tagged native actor/helper pair can recover across a process restart");
    actor.inUse=false;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"inactive allocation cannot be restored as a living squad member");actor.inUse=true;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(28),kOwnedSpawnerTag),"saved helper index mismatch rejects recycled slots");
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),"mission_spawn"),"ordinary mission helper cannot be claimed by an actor tag alone");
    helper.type=kActorType;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"helper slot reused as a living NPC is excluded");helper.type=kActorSpawnerType;
    helper.classname="actor_ally_hero";
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"hero VM sources remain excluded after reload");helper.classname="actor_enemy_soldier";
    helper.team=kAxisTeam;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"native team mismatch prevents adopting an unrelated saved allocation");helper.team=kAlliesTeam;
    actor.number=helper.number;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"actor and helper must be distinct allocations");actor.number=10;
    actor.health=0;
    Check(!RestorableOwnedActor(actor,helper,OwnedActorTag(27),kOwnedSpawnerTag),"dead actor cannot be adopted as a living squad member");
    std::cout<<"Checkpoint tag parsing, reserved slots, helper reuse and saved native pair fixtures passed\n";
}
