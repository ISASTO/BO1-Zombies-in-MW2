#pragma once
#include "bo1_zombie_rules.hpp"
#include "ai_placement.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace mod {
// The Test38 playable zombies adapter: settings chosen in the F4 menu and the
// snapshot the overlay HUD draws. Native work happens on the server thread.
struct ZombieSettings {
    int startingRound=1;
    bool hellhounds=false;      // BO1 dog rounds, when the map has a dog spawner
    bool removeAllies=true;     // campaign allies are deleted like campaign enemies
};
// Every MW2 campaign level in mission order (the "spmap" names).
struct CampaignMap { const char* name; const char* title; };
inline constexpr std::array<CampaignMap,19> kCampaignMaps{{
    {"trainer","S.S.D.D."},{"roadkill","Team Player"},{"cliffhanger","Cliffhanger"},{"airport","No Russian"},
    {"favela","Takedown"},{"invasion","Wolverines!"},{"favela_escape","The Hornet's Nest"},{"arcadia","Exodus"},
    {"oilrig","The Only Easy Day"},{"gulag","The Gulag"},{"dcburning","Of Their Own Accord"},{"contingency","Contingency"},
    {"dcemp","Second Sun"},{"dc_whitehouse","Whiskey Hotel"},{"estate","Loose Ends"},{"boneyard","The Enemy of My Enemy"},
    {"af_caves","Just Like Old Times"},{"af_chase","Endgame"},{"ending","Museum"}}};
inline int CampaignMapIndex(const char* name) {
    if(!name) return -1;
    for(std::size_t n=0;n<kCampaignMaps.size();++n) if(!std::strcmp(kCampaignMaps[n].name,name)) return static_cast<int>(n);
    return -1;
}
inline bool SafeMapName(const char* name) {
    if(!name || !name[0] || std::strlen(name)>32) return false;
    for(const char* c=name;*c;++c) if(!((*c>='a' && *c<='z') || (*c>='0' && *c<='9') || *c=='_')) return false;
    return true;
}
struct ZombieDropView { bo1::Powerup type=bo1::Powerup::None; AiPoint origin{}; bool visible=true; };
struct ZombieGameStatus {
    bool available=false,running=false,pending=false;
    char pendingMap[33]{};
    ZombieSettings settings;
    bo1::ControllerStatus round;
    int points=0,pointsTotal=0;
    unsigned kills=0,headshots=0;
    int doublePointsMs=0,instaKillMs=0;
    bool dogSpawners=false;
    int nextDogRound=0;
    std::array<ZombieDropView,8> drops{};
    int dropCount=0;
    char weapon[32]{};
    char event[64]{};
    int eventAgeMs=1000000;
    int gameOverRound=0;
    int lastPointsDelta=0,pointsDeltaAgeMs=1000000;
};
} // namespace mod
