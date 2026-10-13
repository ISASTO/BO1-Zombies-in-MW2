#include "combat_feedback.hpp"
#include <cstdlib>
#include <iostream>

void Check(bool ok,const char* message) {if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    AiEntity ally;ally.valid=ally.inUse=true;ally.address=0x1000;ally.actor=0x2000;ally.sentient=0x3000;
    ally.number=10;ally.serial=4;ally.type=kActorType;ally.team=kAlliesTeam;ally.health=100;
    const auto owned=AiIdentity::capture(ally);
    Check(ProtectSpawnedFriendly(true,owned,ally,kAlliesTeam),"A current spawned ally must be protected");
    Check(!ProtectSpawnedFriendly(false,owned,ally,kAlliesTeam),"OFF must pass damage through");
    Check(!ProtectSpawnedFriendly(true,{},ally,kAlliesTeam),"Campaign allies must not inherit spawned ally protection");
    Check(!ProtectSpawnedFriendly(true,owned,ally,kAxisTeam),"Owned enemies must not become invincible");
    ++ally.serial;Check(!ProtectSpawnedFriendly(true,owned,ally,kAlliesTeam),"A recycled slot must lose protection");--ally.serial;
    ally.actor+=8;Check(!ProtectSpawnedFriendly(true,owned,ally,kAlliesTeam),"A replaced actor brain must lose stale protection");ally.actor-=8;
    ally.health=0;Check(!ProtectSpawnedFriendly(true,owned,ally,kAlliesTeam),"Corpses must stay removable");ally.health=100;
    Check(ConfirmedPlayerActorHit(true,20,ally,ally.serial,80),"Player damage must produce feedback");
    Check(ConfirmedPlayerActorHit(true,150,ally,ally.serial,-50),"A lethal hit must still produce feedback");
    Check(!ConfirmedPlayerActorHit(true,20,ally,ally.serial,100),"Blocked damage and invincible hits must not show a marker");
    Check(!ConfirmedPlayerActorHit(false,20,ally,ally.serial,80),"NPC hits must not produce player feedback");
    Check(!ConfirmedPlayerActorHit(true,20,ally,ally.serial+1,80),"A recycled target must not inherit a hit");
    Check(!ConfirmedPlayerActorHit(true,0,ally,ally.serial,80),"A zero-damage callback must not create feedback");
    Check(!HitmarkerAlpha(0,1000) && !HitmarkerAlpha(1000,999),"No timestamp or reversed clock must remain invisible");
    Check(HitmarkerAlpha(1000,1000)==255 && HitmarkerAlpha(1000,1001)<255,"MP hitmarkers must begin their linear fade immediately");
    Check(HitmarkerAlpha(1000,1500)==127,"MP hitmarkers must be halfway faded at 500 ms");
    Check(!HitmarkerAlpha(1000,2000) && !HitmarkerAlpha(1000,2000),"Expired markers must not require overlay rendering");
    std::cout<<"Owned friendly protection and confirmed-hit feedback tests passed\n";
}
