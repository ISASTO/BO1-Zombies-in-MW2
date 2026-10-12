#include "friendly_fire.hpp"
#include <cstdlib>
#include <iostream>

void Check(bool condition,const char* message) {if(!condition){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace mod;
    AiEntity ally;ally.valid=true;ally.inUse=true;ally.type=kActorType;ally.number=10;ally.health=100;ally.team=kAlliesTeam;
    Check(BypassFriendlyFirePenalty(true,ally,true,kAlliesTeam),"Player damage to a living ally must bypass the friendly-fire penalty");
    Check(!BypassFriendlyFirePenalty(false,ally,true,kAlliesTeam),"OFF must retain ordinary campaign attribution");
    Check(!BypassFriendlyFirePenalty(true,ally,true,kAxisTeam),"Enemy damage must retain player attribution");
    Check(!BypassFriendlyFirePenalty(true,ally,false,kAlliesTeam),"NPC damage must retain its attacker attribution");
    ally.team=3;Check(!BypassFriendlyFirePenalty(true,ally,true,3),"Neutral-team damage must retain attribution");
    ally.team=kAlliesTeam;ally.number=0;Check(!BypassFriendlyFirePenalty(true,ally,true,kAlliesTeam),"Player and self damage must retain attribution");
    ally.number=10;ally.type=kActorSpawnerType;Check(!BypassFriendlyFirePenalty(true,ally,true,kAlliesTeam),"Spawners must not be treated as living allies");
    ally.type=kActorType;ally.health=0;Check(!BypassFriendlyFirePenalty(true,ally,true,kAlliesTeam),"Corpses must not be treated as living allies");
    ally.health=100;ally.valid=false;Check(!BypassFriendlyFirePenalty(true,ally,true,kAlliesTeam),"Unvalidated actors must not alter damage attribution");
    std::cout<<"Friendly-fire damage attribution checks passed\n";
}
