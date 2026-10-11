#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace mod {
enum class ExtraFeature { GiveAll, RapidFire, ExplosiveBullets, SlowMotion, FreezeAI, Gravity, ForceDualWield, Count };
inline const char* ExtraName(ExtraFeature feature) {
    switch (feature) {
        case ExtraFeature::GiveAll: return "GIVE_ALL_LOADED";
        case ExtraFeature::RapidFire: return "RAPID_FIRE";
        case ExtraFeature::ExplosiveBullets: return "EXPLOSIVE_BULLETS";
        case ExtraFeature::SlowMotion: return "SLOW_MOTION";
        case ExtraFeature::FreezeAI: return "FREEZE_AI";
        case ExtraFeature::Gravity: return "GRAVITY";
        case ExtraFeature::ForceDualWield: return "FORCE_DUAL_WIELD";
        case ExtraFeature::Count: break;
    }
    return "UNKNOWN";
}
struct ExtraPreferences {
    std::array<bool,static_cast<std::size_t>(ExtraFeature::Count)> enabled{};
    float timeScale=.5f;
    int gravity=400;
    bool wanted(ExtraFeature feature) const { return enabled[static_cast<std::size_t>(feature)]; }
    void toggle(ExtraFeature feature) { const auto n=static_cast<std::size_t>(feature); enabled[n]=!enabled[n]; }
    bool adjust(ExtraFeature feature,int direction) {
        if (!direction) return false;
        if (feature==ExtraFeature::SlowMotion) {
            const float next=std::clamp(timeScale+(direction>0?.25f:-.25f),.25f,1.f);
            const bool changed=next!=timeScale; timeScale=next; return changed;
        }
        if (feature==ExtraFeature::Gravity) {
            const int next=std::clamp(gravity+(direction>0?100:-100),100,1600);
            const bool changed=next!=gravity; gravity=next; return changed;
        }
        return false;
    }
    void resetWeapons() {
        for (const auto f:{ExtraFeature::GiveAll,ExtraFeature::RapidFire,ExtraFeature::ExplosiveBullets,ExtraFeature::ForceDualWield}) enabled[static_cast<std::size_t>(f)]=false;
    }
    void resetWorld() {
        for (const auto f:{ExtraFeature::SlowMotion,ExtraFeature::FreezeAI,ExtraFeature::Gravity}) enabled[static_cast<std::size_t>(f)]=false;
        timeScale=.5f; gravity=400;
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
inline constexpr int kWeaponCapacity = 200;
inline constexpr float kDefaultGravityLiteral = 800.5f;
inline bool ValidGravityLiteral(float value) {
    const float gravity=value-.5f;
    return gravity>=100.f && gravity<=1600.f && gravity==100.f*static_cast<int>(gravity/100.f);
}
inline int RapidFireTime(int milliseconds) { return std::max(20,milliseconds/2); }
struct WeaponFields {
    bool valid=false;
    unsigned index=0;
    std::uintptr_t complete=0;
    std::uintptr_t definition=0;
    int type=-1;
    int fireTime=0;
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
} // namespace mod
