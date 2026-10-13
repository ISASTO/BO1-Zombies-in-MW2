#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace mod {
enum class ExtraFeature { GiveAll, RapidFire, ExplosiveBullets, WorldSpeed, FreezeAI, Gravity, ForceDualWield, NoRecoil, FriendlyFire, Hitmarkers, SemiAutoFullAuto, InvincibleFriendlies, Count };
inline const char* ExtraName(ExtraFeature feature) {
    switch (feature) {
        case ExtraFeature::GiveAll: return "GIVE_ALL_LOADED";
        case ExtraFeature::RapidFire: return "RAPID_FIRE";
        case ExtraFeature::ExplosiveBullets: return "EXPLOSIVE_BULLETS";
        case ExtraFeature::WorldSpeed: return "WORLD_SPEED";
        case ExtraFeature::FreezeAI: return "FREEZE_AI";
        case ExtraFeature::Gravity: return "GRAVITY";
        case ExtraFeature::ForceDualWield: return "FORCE_DUAL_WIELD";
        case ExtraFeature::NoRecoil: return "NO_RECOIL";
        case ExtraFeature::FriendlyFire: return "ALLOW_FRIENDLY_FIRE";
        case ExtraFeature::Hitmarkers: return "HITMARKERS";
        case ExtraFeature::SemiAutoFullAuto: return "SEMI_AUTO_FULL_AUTO";
        case ExtraFeature::InvincibleFriendlies: return "INVINCIBLE_SPAWNED_FRIENDLIES";
        case ExtraFeature::Count: break;
    }
    return "UNKNOWN";
}
struct ExtraPreferences {
    std::array<bool,static_cast<std::size_t>(ExtraFeature::Count)> enabled{};
    float timeScale=1.f;
    int gravity=400;
    int rapidFireMultiplier=2;
    bool wanted(ExtraFeature feature) const { return enabled[static_cast<std::size_t>(feature)]; }
    void toggle(ExtraFeature feature) { const auto n=static_cast<std::size_t>(feature); enabled[n]=!enabled[n]; }
    bool adjust(ExtraFeature feature,int direction) {
        if (!direction) return false;
        if (feature==ExtraFeature::RapidFire) {
            const int next=std::clamp(rapidFireMultiplier+(direction>0?1:-1),1,10);
            const bool changed=next!=rapidFireMultiplier; rapidFireMultiplier=next; return changed;
        }
        if (feature==ExtraFeature::WorldSpeed) {
            const int tenths=static_cast<int>(std::lround(timeScale*10.f));
            const float next=std::clamp(tenths+(direction>0?1:-1),1,100)/10.f;
            const bool changed=next!=timeScale; timeScale=next; return changed;
        }
        if (feature==ExtraFeature::Gravity) {
            const int next=std::clamp(gravity+(direction>0?100:-100),100,1600);
            const bool changed=next!=gravity; gravity=next; return changed;
        }
        return false;
    }
    void resetWeapons() {
        for (const auto f:{ExtraFeature::GiveAll,ExtraFeature::RapidFire,ExtraFeature::ExplosiveBullets,ExtraFeature::ForceDualWield,ExtraFeature::NoRecoil,ExtraFeature::Hitmarkers,ExtraFeature::SemiAutoFullAuto}) enabled[static_cast<std::size_t>(f)]=false;
        rapidFireMultiplier=2;
    }
    void resetWorld() {
        for (const auto f:{ExtraFeature::WorldSpeed,ExtraFeature::FreezeAI,ExtraFeature::Gravity,ExtraFeature::FriendlyFire}) enabled[static_cast<std::size_t>(f)]=false;
        timeScale=1.f; gravity=400;
    }
};
// Ownership follows the last successful write. Editing a strength retains its
// native baseline, while an intervening engine value becomes a new baseline.
template<class T> struct ScalarOwnership {
    bool owned=false;
    std::uintptr_t dvar=0;
    T original{};
    T injected{};
    void applied(std::uintptr_t next,T before,T after) {
        if (!owned || dvar!=next || before!=injected) original=before;
        owned=true; dvar=next; injected=after;
    }
    bool canRestore(std::uintptr_t at,T current) const { return owned && dvar==at && current==injected; }
    void clear() { *this={}; }
};
inline constexpr std::size_t kCurrentWeapon = 0x2b4;
inline constexpr std::size_t kPlayerGravity = 0x54;
inline constexpr std::size_t kCompleteDefinition = 0x8;
inline constexpr std::size_t kWeaponFireTime = 0x3c;
inline constexpr std::size_t kWeaponType = 0x54;
// The native held-shot gate at 0x83f90 reads this field. Mode 1 stops after
// one shot per press; mode 0 bypasses that gate and preserves native timing.
inline constexpr std::size_t kWeaponFireMode = 0x64;
inline constexpr int kAutomaticFireMode=0,kSemiAutomaticFireMode=1;
inline constexpr int kWeaponCapacity = 200;
inline constexpr float kDefaultGravityLiteral = 800.5f;
inline bool ValidGravityLiteral(float value) {
    const float gravity=value-.5f;
    return gravity>=100.f && gravity<=1600.f && gravity==100.f*static_cast<int>(gravity/100.f);
}
inline int RapidFireTime(int milliseconds,int multiplier=2) { return std::max(1,milliseconds/std::clamp(multiplier,1,10)); }
// Independently checked against the native player_recoilscaleon/off methods.
inline constexpr std::size_t kClientWeaponFlags=0x2bc,kClientRecoilScale=0xac98;
inline constexpr unsigned kRecoilScaleFlag=0x400;
// SHA256-gated registration and independent consumer bindings, checked by
// tools/verify_squad_bindings.py. The distance controls the friendly aim block;
// the damage scale controls damage to a player from a client-bearing attacker.
inline constexpr std::uintptr_t kFriendlyFireDistanceSlotRva=0xd9c4e8;
inline constexpr std::uintptr_t kFriendlyFireScaleSlotRva=0xd9c4f0;
struct WeaponFields {
    bool valid=false;
    unsigned index=0;
    std::uintptr_t complete=0;
    std::uintptr_t definition=0;
    int type=-1;
    int fireTime=0;
    int fireMode=-1;
    char name[128]{};
};
struct RapidOwnership {
    bool owned=false;
    WeaponFields identity;
    int original=0;
    int injected=0;
    bool sameIdentity(const WeaponFields& w) const {
        return owned && w.valid && w.index==identity.index && w.complete==identity.complete
            && w.definition==identity.definition && w.type==identity.type && std::strcmp(w.name,identity.name)==0;
    }
    bool canRestore(const WeaponFields& w) const { return sameIdentity(w) && w.fireTime==injected; }
    void applied(const WeaponFields& w,int target) {
        if (!sameIdentity(w) || w.fireTime!=injected) original=w.fireTime;
        identity=w; injected=target; owned=true;
    }
    void clear() { *this={}; }
};
struct FireModeOwnership {
    bool owned=false;
    WeaponFields identity;
    int original=kSemiAutomaticFireMode;
    bool sameIdentity(const WeaponFields& w) const {
        return owned && w.valid && w.index==identity.index && w.complete==identity.complete
            && w.definition==identity.definition && w.type==identity.type && std::strcmp(w.name,identity.name)==0;
    }
    bool canRestore(const WeaponFields& w) const {return sameIdentity(w) && w.fireMode==kAutomaticFireMode;}
    void applied(const WeaponFields& w) {identity=w;original=w.fireMode;owned=true;}
    void clear() {*this={};}
};
} // namespace mod
