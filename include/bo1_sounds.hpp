#pragma once
// Test43: the BO1 zombie sounds, generated as a MW2 fastfile from the owner's
// own Black Ops install (zone\Common\common_zombie.ff for the sound bank and
// its loaded sounds, zone\Common\zombie_theater.ff and main\*.iwd for the
// round music) on a background thread, then cached in bo1mw2\cache\ until a
// source file changes. See include/bo1_sound.hpp for the formats.
#include <array>
#include <string>
#include <string_view>

namespace mod::bo1sounds {
enum class State {Idle,Building,Ready,Missing,Failed};
struct Status {
    State state=State::Idle;
    bool cached=false;
    std::string detail;
    std::string file;
    unsigned lists=0,sounds=0;
    std::size_t bytes=0;
    double seconds=0.0;
    bool skipped=false;                  // crash guard holds the sounds back
    std::string skipReason;
    int bedMilliseconds=0;               // Test45: the ambience bed's length (bo1_zombie_sounds.info)
};
const char* StateText(State state);
void StartBuild();
bool WaitReady(unsigned milliseconds);
Status GetStatus();

// Crash guard, as for the models (bo1_models.hpp): the marker
// bo1mw2\cache\bo1_zombie_sounds.loading exists from the moment the zone joins
// a level load until zombie sounds have been playing for kGuardConfirmMs (or
// MW2 exits normally). A marker left by an earlier run skips the sounds for
// one run.
inline constexpr unsigned long long kGuardConfirmMs=45000;
bool GuardAllowsLoad(std::string& reason);
void GuardBegin(const std::string& level);
void GuardLoaded();
void GuardUnused();
bool GuardConfirm();
void GuardProcessExit();
inline constexpr const char* kZoneName="..\\..\\bo1mw2\\cache\\bo1_zombie_sounds";

// The BO1 alias lists the zombie game plays (names as in BO1's sound bank).
namespace alias {
inline constexpr const char* kAmbience="zmb_vocals_zombie_ambience";
inline constexpr const char* kSprint="zmb_vocals_zombie_sprint";
inline constexpr const char* kAttack="zmb_vocals_zombie_attack";
inline constexpr const char* kBehind="zmb_vocals_zombie_behind";
inline constexpr const char* kDeath="zmb_vocals_zombie_death";
inline constexpr const char* kRise="zmb_zombie_spawn";
inline constexpr const char* kSwing="zmb_attack_whoosh";
inline constexpr const char* kStepPrefix="fly_step_zombie_";
inline constexpr const char* kStepDefault="fly_step_zombie_default";
inline constexpr const char* kDogBolt="zmb_hellhound_bolt";
inline constexpr const char* kDogSpawn="zmb_hellhound_spawn";
inline constexpr const char* kDogExplode="zmb_hellhound_explode";
inline constexpr const char* kDogAmbient="zmb_hellhound_vocals_amb";
inline constexpr const char* kDogClose="zmb_hellhound_vocals_close";
inline constexpr const char* kPowerupSpawn="zmb_spawn_powerup";
inline constexpr const char* kPowerupGrab="zmb_powerup_grabbed";
inline constexpr const char* kMaxAmmo="zmb_full_ammo";
inline constexpr const char* kInstaKill="zmb_insta_kill";
inline constexpr const char* kNukeFlash="evt_nuke_flash";
inline constexpr const char* kNuked="evt_nuked";
inline constexpr const char* kDogRoundStinger="zmb_dog_round_start";
inline constexpr const char* kRoundStart="mus_zombie_round_start";
inline constexpr const char* kRoundOver="mus_zombie_round_over";
inline constexpr const char* kDogRoundStart="mus_zombie_dog_start";
inline constexpr const char* kDogRoundOver="mus_zombie_dog_end";
inline constexpr const char* kGameOver="mus_zombie_game_over";
// Test45: ambience (MW2-side names: see kTheaterLists), the announcer and
// dismemberment.
inline constexpr const char* kAmbienceBed="bo1_zombie_ambience_bed";
inline constexpr std::array<const char*,4> kAmbienceOneShots={"bo1_amb_wind_howl","bo1_amb_wood_groan","bo1_amb_whispers","bo1_amb_metal_clangs"};
inline constexpr const char* kAnnMaxAmmo="zmb_vox_ann_maxammo";
inline constexpr const char* kAnnInstaKill="zmb_vox_ann_instakill";
inline constexpr const char* kAnnDoublePoints="zmb_vox_ann_doublepoints";
inline constexpr const char* kAnnNuke="zmb_vox_ann_nuke";
inline constexpr const char* kAnnCarpenter="zmb_vox_ann_carpenter";
inline constexpr const char* kAnnDogStart="zmb_vox_ann_dogstart";
inline constexpr const char* kHeadGib="zmb_zombie_head_gib";
inline constexpr const char* kLimbGib="zmb_death_gibs";
} // namespace alias
inline constexpr std::array<const char*,38> kRuntimeAliases={alias::kAmbience,alias::kSprint,alias::kAttack,alias::kBehind,alias::kDeath,
    alias::kRise,alias::kSwing,alias::kDogBolt,alias::kDogSpawn,alias::kDogExplode,alias::kDogAmbient,alias::kDogClose,alias::kPowerupSpawn,
    alias::kPowerupGrab,alias::kMaxAmmo,alias::kInstaKill,alias::kNukeFlash,alias::kNuked,alias::kDogRoundStinger,alias::kRoundStart,
    alias::kRoundOver,alias::kDogRoundStart,alias::kDogRoundOver,alias::kGameOver,alias::kStepDefault,
    alias::kAmbienceBed,alias::kAmbienceOneShots[0],alias::kAmbienceOneShots[1],alias::kAmbienceOneShots[2],alias::kAmbienceOneShots[3],
    alias::kAnnMaxAmmo,alias::kAnnInstaKill,alias::kAnnDoublePoints,alias::kAnnNuke,alias::kAnnCarpenter,alias::kAnnDogStart,
    alias::kHeadGib,alias::kLimbGib};
// MW2 surface names (animscripts play "step_run_" + self.groundtype).
inline constexpr std::array<const char*,31> kSurfaces={"asphalt","bark","brick","carpet","ceramic","cloth","concrete","cushion","default",
    "dirt","flesh","foliage","fruit","glass","grass","gravel","ice","metal","mud","paintedmetal","paper","plaster","plastic","rock",
    "rubber","sand","snow","water","wood","riotshield","slush"};
// Typical lengths (BO1 vocals): a zombie does not start another moan while one plays.
inline constexpr int kAmbienceMs=1500,kSprintMs=4200,kAttackMs=1100;

// Test45: what a one-argument "self playsound(alias)" from MW2's animscripts
// becomes on a BO1 zombie. Step: "step_run_<surface>" turns into the BO1
// zombie step on that surface. Keep: a body falling ("bodyfall_*"). The
// rest is soldier foley or voice and is dropped: Gear ("gear_rattle_*", every
// running footstep), Scrape ("step_scrape_*"), Land ("land_*"), Weapon
// ("weap_*", reloads; "ai_shotgun_pump"), Voice ("generic_*", "*_death"),
// Other (knife, dive...).
enum class SoldierSound : int {Step,Keep,Gear,Scrape,Land,Weapon,Voice,Other,Count};
inline bool StartsWithNoCase(std::string_view text,std::string_view prefix) {
    if(text.size()<prefix.size()) return false;
    for(std::size_t n=0;n<prefix.size();++n) {
        const char a=static_cast<char>(text[n]>='A' && text[n]<='Z'?text[n]-'A'+'a':text[n]);
        if(a!=prefix[n]) return false;
    }
    return true;
}
inline SoldierSound ClassifySoldierSound(std::string_view alias) {
    if(StartsWithNoCase(alias,"step_run_")) return SoldierSound::Step;
    if(StartsWithNoCase(alias,"bodyfall_")) return SoldierSound::Keep;
    if(StartsWithNoCase(alias,"gear_rattle")) return SoldierSound::Gear;
    if(StartsWithNoCase(alias,"step_scrape_")) return SoldierSound::Scrape;
    if(StartsWithNoCase(alias,"land_")) return SoldierSound::Land;
    if(StartsWithNoCase(alias,"weap_") || StartsWithNoCase(alias,"ai_shotgun")) return SoldierSound::Weapon;
    if(StartsWithNoCase(alias,"generic_")) return SoldierSound::Voice;
    if(alias.size()>=6 && alias.substr(alias.size()-6)=="_death") return SoldierSound::Voice;
    return SoldierSound::Other;
}
inline const char* SoldierSoundText(SoldierSound s) {
    switch(s) {
        case SoldierSound::Step: return "step";case SoldierSound::Keep: return "keep";case SoldierSound::Gear: return "gear";
        case SoldierSound::Scrape: return "scrape";case SoldierSound::Land: return "land";case SoldierSound::Weapon: return "weapon";
        case SoldierSound::Voice: return "voice";case SoldierSound::Other: return "other";default: return "?";
    }
}
} // namespace mod::bo1sounds
