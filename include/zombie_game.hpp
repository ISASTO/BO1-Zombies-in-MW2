#pragma once
#include "bo1_zombie_rules.hpp"
#include "ai_placement.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace mod {
// The playable zombies adapter: settings chosen in the F4 menu and the
// snapshot the overlay HUD draws. Native work happens on the server thread.
struct ZombieSettings {
    int startingRound=1;
    bool hellhounds=false;      // BO1 dog rounds, when the map has a dog spawner
    bool removeAllies=true;     // campaign allies are deleted like campaign enemies
    bool autoRestart=true;      // after GAME OVER the Spec Ops map is loaded again
};
// Test39: zombies are played on the Spec Ops maps, which are campaign
// locations cut down to a small play area (the zone file is so_<mission>).
// Excluded: the two co-op-only missions (AC-130 and helicopter gunner) and
// the two snowmobile races, which put the player in a vehicle.
struct SpecOpsMap { const char* name; const char* title; };
inline constexpr std::array<SpecOpsMap,19> kSpecOpsMaps{{
    {"so_killspree_trainer","Training pit"},{"so_hidden_so_ghillies","Pripyat (Hidden)"},
    {"so_forest_contingency","Contingency forest"},{"so_rooftop_contingency","Contingency rooftops"},
    {"so_crossing_so_bridge","Bridge crossing"},{"so_demo_so_bridge","Bridge demolition"},
    {"so_killspree_favela","Favela"},{"so_juggernauts_favela","Favela (Juggernauts)"},
    {"so_defuse_favela_escape","Favela rooftops (Bomb Squad)"},{"so_killspree_invasion","Invasion"},
    {"so_defense_invasion","Invasion (Homeland Security)"},{"so_download_arcadia","Arcadia (Wardriving)"},
    {"so_escape_airport","Airport (Terminal)"},{"so_showers_gulag","Gulag showers"},
    {"so_intel_boneyard","Boneyard"},{"so_takeover_estate","Estate"},
    {"so_takeover_oilrig","Oil rig deck"},{"so_assault_oilrig","Oil rig (Wetwork)"},
    {"so_sabotage_cliffhanger","Cliffhanger base"}}};
inline int SpecOpsMapIndex(const char* name) {
    if(!name) return -1;
    for(std::size_t n=0;n<kSpecOpsMaps.size();++n) if(!std::strcmp(kSpecOpsMaps[n].name,name)) return static_cast<int>(n);
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
    // Test39: BO1 animation import and Spec Ops preparation of this level.
    char bo1Assets[16]{};        // READY, LOADING, MISSING, FAILED
    int bo1AnimsFound=0,bo1AnimsWanted=0,animSlotsApplied=0,animSlotsWanted=0;
    int specOpsGuards=0;         // Spec Ops end paths disabled (3 on a prepared level)
    bool levelPrepared=false;    // this level was loaded by a zombies launch
    // Test40: BO1 character models generated from the Black Ops install.
    char bo1Models[16]{};        // READY, BUILDING, MISSING, FAILED
    int modelsPrecached=0;       // BO1 bodies + heads usable on this level
    unsigned zombiesDressed=0;
    int restartInMs=-1;          // GAME OVER relaunch countdown, -1 when none
};
} // namespace mod
