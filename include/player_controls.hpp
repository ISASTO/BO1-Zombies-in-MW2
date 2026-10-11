#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "player_fields.hpp"

namespace mod {
// Exact AMD64 retail profile only. These are partial fields, never a full gclient cast.
inline constexpr std::size_t kEntityClient = 0x108;
inline constexpr std::size_t kEntityTakeDamage = 0x139;
inline constexpr std::size_t kClientCommandTime = 0x0;
inline constexpr std::size_t kClientMoveType = 0x4;
inline constexpr std::size_t kClientOtherFlags = 0x10;
inline constexpr std::size_t kClientHealth = 0x14c;
inline constexpr std::size_t kClientConnected = 0xaca4;
inline constexpr std::size_t kClientCheatFlags = 0xad54;
inline constexpr std::size_t kReserveAmmo = 0x2d8;
inline constexpr int kAmmoPoolCount = 15;
inline constexpr int kInfiniteReserve = 999;
inline constexpr int kDefaultSpeed = 190;
inline constexpr int kSuperSpeed = 380;
inline constexpr float kDefaultJump = 39.f;
inline constexpr float kSuperJump = 117.f;

enum class PlayerFeature { God, InfiniteAmmo, NoReload, Noclip, SuperSpeed, SuperJump, Count };
inline const char* FeatureName(PlayerFeature feature) {
    switch (feature) {
        case PlayerFeature::God: return "GOD";
        case PlayerFeature::InfiniteAmmo: return "INFINITE_AMMO";
        case PlayerFeature::NoReload: return "NO_RELOAD";
        case PlayerFeature::Noclip: return "NOCLIP";
        case PlayerFeature::SuperSpeed: return "SUPER_SPEED";
        case PlayerFeature::SuperJump: return "SUPER_JUMP";
        case PlayerFeature::Count: break;
    }
    return "UNKNOWN";
}
struct PlayerPreferences {
    std::array<bool, static_cast<std::size_t>(PlayerFeature::Count)> enabled{};
    bool wanted(PlayerFeature feature) const { return enabled[static_cast<std::size_t>(feature)]; }
    void toggle(PlayerFeature feature) { const auto n=static_cast<std::size_t>(feature); enabled[n]=!enabled[n]; }
    void reset() { enabled.fill(false); }
};
// Absolute state reconciliation, unlike XOR: a checkpoint can restore a flag
// after the UI has already selected OFF. Preserve every bit outside our mask.
inline std::uint32_t DesiredFlags(std::uint32_t current, std::uint32_t mask, std::uint32_t wanted) {
    return (current & ~mask) | (wanted & mask);
}
struct ClientFields {
    bool valid=false;
    std::uintptr_t address=0;
    int connected=-1;
    int commandTime=0;
    int moveType=-1;
    int health=0;
    std::uint32_t otherFlags=0;
    std::uint32_t cheatFlags=0;
};
template<class Reader>
ClientFields ReadClientFields(std::uintptr_t entity, Reader read) {
    ClientFields c;
    if (!read(entity+kEntityClient,&c.address,sizeof(c.address)) || !c.address || (c.address&7)) return c;
    auto value=[&](std::size_t offset, auto& out) { return read(c.address+offset,&out,sizeof(out)); };
    // Slot identity was already checked on the entity. +0x34 is kinematic
    // state in this executable, not a safe client-number guard.
    if (!value(kClientConnected,c.connected) || c.connected!=2
        || !value(kClientMoveType,c.moveType) || c.moveType<0 || c.moveType>7
        || !value(kClientHealth,c.health) || c.health<0 || c.health>100000
        || !value(kClientCommandTime,c.commandTime) || c.commandTime<0
        || !value(kClientOtherFlags,c.otherFlags) || !value(kClientCheatFlags,c.cheatFlags)) return c;
    c.valid=true; return c;
}
struct AmmoPool { std::uint32_t id=0; std::int32_t count=0; };
static_assert(sizeof(AmmoPool)==8,"Native reserve entries are eight bytes");
using ReserveTable = std::array<AmmoPool,kAmmoPoolCount>;
inline std::uint64_t PackReserve(const AmmoPool& pool) {
    return static_cast<std::uint64_t>(pool.id) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pool.count))<<32);
}
inline bool ValidReserveTable(const ReserveTable& pools) {
    for (const auto& pool:pools) {
        if (!pool.id) continue; // Native lookup treats ID zero as an unused entry.
        if (pool.id>4095 || pool.count<0 || pool.count>100000) return false;
    }
    return true;
}
// Save only counts we actually replace. Never invent pool IDs, change clips,
// overwrite another pool, or restore a count after the engine has changed it.
struct ReserveOwnership {
    struct Entry { std::uint32_t id=0; int original=0; bool modified=false; };
    std::array<Entry,kAmmoPoolCount> entries{};
    void clear() { entries={}; }
    void record(int slot, const AmmoPool& pool) {
        auto& e=entries[slot];
        if (!e.modified || e.id!=pool.id) e={pool.id,pool.count,true};
    }
    bool canRestore(int slot, const AmmoPool& pool) const {
        const auto& e=entries[slot];
        return e.modified && e.id==pool.id && pool.count==kInfiniteReserve;
    }
};
// A recycled static client pointer does not identify a checkpoint generation.
// A server gap, changed map/client, or command-time rewind discards ownership.
struct PlayerLifetime {
    bool active=false;
    std::uintptr_t client=0;
    int commandTime=0;
    char map[96]{};
    bool observe(bool ready, std::uintptr_t nextClient, int nextTime, const char* nextMap) {
        if (!ready) { active=false; return false; }
        const bool newLifetime=!active || client!=nextClient || nextTime<commandTime || std::strcmp(map,nextMap)!=0;
        active=true; client=nextClient; commandTime=nextTime;
        std::strncpy(map,nextMap,sizeof(map)-1); map[sizeof(map)-1]='\0';
        return newLifetime;
    }
};
} // namespace mod
