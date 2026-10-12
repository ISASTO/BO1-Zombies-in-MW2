#pragma once
#include "control_extensions.hpp"
#include "player_controls.hpp"
#include <vector>

namespace mod {
inline constexpr std::size_t kOwnedWeapons=0x220;
inline constexpr std::size_t kWeaponMetadata=0x25c;
inline constexpr std::size_t kWeaponMetadataStride=5;
inline constexpr std::size_t kClipAmmo=0x350;
inline constexpr std::size_t kLastWeaponHand=0x2d4;
inline constexpr std::size_t kGunModels=0x8;
inline constexpr std::size_t kWeaponClass=0x58;
inline constexpr std::size_t kInventoryType=0x60;
inline constexpr std::size_t kAmmoIndex=0x348;
inline constexpr std::size_t kClipIndex=0x358;
inline constexpr std::size_t kClipSize=0x34;
inline constexpr std::size_t kAlternateWeapon=0x58;
inline constexpr std::size_t kNoDualWield=0x873;
inline constexpr std::size_t kRightWeaponAnims=0x18;
inline constexpr std::size_t kLeftWeaponAnims=0x20;
inline constexpr std::size_t kNormalWeaponAnims=0x20;
inline constexpr std::size_t kWeaponAnimCount=37;
inline constexpr std::size_t kWeaponAnimCacheStride=24;
struct ClipPool { std::uint32_t id=0; int count[2]{}; };
static_assert(sizeof(ClipPool)==12,"Native clip pool is ID and two counts");
struct InventoryData {
    std::array<std::uint32_t,15> weapons{};
    std::array<std::array<std::uint8_t,5>,15> metadata{};
    std::array<AmmoPool,15> reserve{};
    std::array<ClipPool,15> clips{};
    std::uint32_t current=0;
};
struct WeaponCandidate {
    WeaponFields weapon;
    bool modelsValid=false;
    int inventoryType=-1,weaponClass=-1,clipSize=0;
    unsigned ammoId=0,clipId=0;
    unsigned alternate=0;
    bool noDualWield=true;
};
inline bool PrimaryGun(const WeaponCandidate& w) {
    return w.weapon.valid && w.weapon.type==0 && w.modelsValid && w.inventoryType==0
        && w.weaponClass>=0 && w.weaponClass<8 && w.clipSize>0 && w.clipSize<=1000
        && w.ammoId>0 && w.ammoId<kWeaponCapacity && w.clipId>0 && w.clipId<kWeaponCapacity;
}
inline bool UsableGun(const WeaponCandidate& w) { return PrimaryGun(w) && !w.alternate; }
inline bool OwnsWeapon(const InventoryData& d,unsigned index) {
    return std::find(d.weapons.begin(),d.weapons.end(),index)!=d.weapons.end();
}
template<class Pool> inline int PoolSlot(const std::array<Pool,15>& pools,unsigned id) {
    for (int n=0;n<15;++n) if (pools[n].id==id) return n;
    for (int n=0;n<15;++n) if (!pools[n].id) return n;
    return -1;
}
struct WeaponBank { InventoryData data; int pages=1,page=0,added=0,eligible=0; };
struct WeaponBankPreview {
    bool known=false;
    int pages=0;
    std::uint64_t generation=0;
    std::array<std::uint32_t,15> weapons{};
    bool needsRefresh(const InventoryData& basis,std::uint64_t revision) const {
        return !known || generation!=revision || weapons!=basis.weapons;
    }
    void publish(const InventoryData& basis,std::uint64_t revision,int count) {
        known=true; weapons=basis.weapons; generation=revision; pages=std::max(1,count);
    }
    void clear() { *this={}; }
};
inline WeaponBank BuildWeaponBank(const InventoryData& original,const std::vector<WeaponCandidate>& catalog,int requested) {
    WeaponBank bank; bank.data=original;
    std::vector<WeaponCandidate> available;
    for (const auto& w:catalog) if (UsableGun(w) && !OwnsWeapon(original,w.weapon.index)) available.push_back(w);
    const int slots=static_cast<int>(std::count(original.weapons.begin(),original.weapons.end(),0u));
    bank.eligible=static_cast<int>(available.size());
    bank.pages=slots?std::max(1,(bank.eligible+slots-1)/slots):1;
    bank.page=std::clamp(requested,0,bank.pages-1);
    auto fill=[&](const WeaponCandidate& w,bool add) {
        const int stock=PoolSlot(bank.data.reserve,w.ammoId),clip=PoolSlot(bank.data.clips,w.clipId);
        if (stock<0 || clip<0) return;
        if (add) {
            auto it=std::find(bank.data.weapons.begin(),bank.data.weapons.end(),0u);
            if (it==bank.data.weapons.end()) return;
            const auto n=static_cast<std::size_t>(it-bank.data.weapons.begin());
            *it=w.weapon.index; bank.data.metadata[n]={}; ++bank.added;
        }
        bank.data.reserve[stock]={w.ammoId,std::min(999,w.clipSize*5)};
        int left=0;
        for (int n=0;n<15;++n) if (original.weapons[n]==w.weapon.index && original.metadata[n][1]) left=w.clipSize;
        bank.data.clips[clip]={w.clipId,{w.clipSize,left}};
    };
    for (const auto& w:catalog) if (UsableGun(w) && OwnsWeapon(original,w.weapon.index)) fill(w,false);
    const int begin=bank.page*slots,end=std::min(begin+slots,bank.eligible);
    for (int n=begin;n<end;++n) fill(available[n],true);
    return bank;
}
struct InventoryOwnership {
    bool owned=false,baselineValid=false;
    std::uintptr_t client=0;
    int commandTime=0,page=0,pages=1,added=0;
    char map[96]{};
    InventoryData original,applied;
    std::array<bool,kWeaponCapacity> introduced{},reserveIds{},clipIds{};
    void capture(std::uintptr_t at,int time,const char* level,const InventoryData& data) {
        *this={}; owned=true; baselineValid=true; client=at; commandTime=time;
        std::copy_n(level,std::min(std::strlen(level),sizeof(map)-1),map); original=data;
    }
    bool sameLifetime(std::uintptr_t at,int time,const char* level) const {
        return owned && client==at && time>=commandTime && std::strcmp(map,level)==0;
    }
    bool sameWeapons(const InventoryData& data) const { return owned && data.weapons==applied.weapons; }
    bool sameScope(std::uintptr_t at,const char* level) const { return baselineValid && client==at && std::strcmp(map,level)==0; }
    bool hasIntroduced(const InventoryData& data) const {
        for (const auto w:data.weapons) if (w<kWeaponCapacity && introduced[w] && !OwnsWeapon(original,w)) return true;
        return false;
    }
    void recordApplied(const InventoryData& data) {
        owned=true; applied=data;
        for (const auto w:data.weapons) if (w && w<kWeaponCapacity && !OwnsWeapon(original,w)) introduced[w]=true;
        for (const auto p:data.reserve) if (p.id<kWeaponCapacity) reserveIds[p.id]=true;
        for (const auto p:data.clips) if (p.id<kWeaponCapacity) clipIds[p.id]=true;
    }
    void clear() { *this={}; }
};
inline InventoryData BuildInventoryRestore(const InventoryOwnership& lease,const InventoryData& live) {
    bool foreign=false;
    for (const auto w:live.weapons) if (w && !OwnsWeapon(lease.original,w)
        && (w>=kWeaponCapacity || !lease.introduced[w])) foreign=true;
    if (!foreign) return lease.original;
    auto result=live;
    for (int n=0;n<15;++n) {
        const auto w=result.weapons[n];
        if (w && w<kWeaponCapacity && lease.introduced[w] && !OwnsWeapon(lease.original,w)) {
            result.weapons[n]=0; result.metadata[n]={};
        } else if (w) for (int k=0;k<15;++k)
            if (lease.original.weapons[k]==w) result.metadata[n]=lease.original.metadata[k];
        auto& stock=result.reserve[n]; bool found=false;
        for (const auto& p:lease.original.reserve) if (stock.id && p.id==stock.id) { stock=p; found=true; break; }
        if (!found && stock.id<kWeaponCapacity && lease.reserveIds[stock.id]) stock={};
        auto& clip=result.clips[n]; found=false;
        for (const auto& p:lease.original.clips) if (clip.id && p.id==clip.id) { clip=p; found=true; break; }
        if (!found && clip.id<kWeaponCapacity && lease.clipIds[clip.id]) clip={};
    }
    // Restore missing original ammo by identity without replacing foreign pools.
    for (const auto& p:lease.original.reserve) if (p.id) {
        const int n=PoolSlot(result.reserve,p.id); if (n>=0) result.reserve[n]=p;
    }
    for (const auto& p:lease.original.clips) if (p.id) {
        const int n=PoolSlot(result.clips,p.id); if (n>=0) result.clips[n]=p;
    }
    if (lease.original.current && OwnsWeapon(result,lease.original.current)) result.current=lease.original.current;
    else if (!OwnsWeapon(result,result.current)) {
        const auto it=std::find_if(result.weapons.begin(),result.weapons.end(),[](unsigned w) { return w!=0; });
        result.current=it==result.weapons.end()?0:*it;
    }
    return result;
}
struct DualCompatibilityPlan { bool ready=false,fallback=false; };
// The exact native builder iterates slots 1..36, substitutes idle (1) for
// empty optional names, and never reads unused slot 0.
template<class StringReader>
bool UsableAnimationNames(const std::array<std::uintptr_t,kWeaponAnimCount>& names,StringReader read) {
    char text[128]{};
    for(std::size_t n=1;n<names.size();++n) {
        if(!read(names[n],text,sizeof(text)) || (n==1 && !text[0])) return false;
    }
    return true;
}
enum class DualBlocked { None, UnsupportedGun, CacheUnreadable, MissingDefinition, InvalidNames, InvalidFields, ChangedAsset, WriteFailed };
inline const char* DualBlockedText(DualBlocked value) {
    switch(value) {
        case DualBlocked::None: return "NONE";
        case DualBlocked::UnsupportedGun: return "UNSUPPORTED_GUN";
        case DualBlocked::CacheUnreadable: return "CACHE_UNREADABLE";
        case DualBlocked::MissingDefinition: return "MISSING_NATIVE_TREE";
        case DualBlocked::InvalidNames: return "ANIMATION_NAMES";
        case DualBlocked::InvalidFields: return "ASSET_FIELDS";
        case DualBlocked::ChangedAsset: return "ASSET_CHANGED";
        case DualBlocked::WriteFailed: return "WRITE_FAILED";
    }
    return "UNKNOWN";
}
inline DualCompatibilityPlan DualPlan(bool primary,bool normalAnims,bool registered,bool authored,bool pairedRegistered) {
    const bool ready=primary && normalAnims && registered;
    return {ready,ready && (!authored || !pairedRegistered)};
}
struct DualSlotOwnership { bool owned=false; unsigned weapon=0; std::uint8_t original=0; };
struct DualAmmoOwnership { bool owned=false; unsigned pool=0; int original=0; };
inline bool HasDualPlayerEdits(const std::array<DualSlotOwnership,15>& slots,const std::array<DualAmmoOwnership,15>& ammo,bool handOwned) {
    return handOwned || std::any_of(slots.begin(),slots.end(),[](const auto& s) { return s.owned; })
        || std::any_of(ammo.begin(),ammo.end(),[](const auto& s) { return s.owned; });
}
} // namespace mod
