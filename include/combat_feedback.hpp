#pragma once
#include "ai_controls.hpp"

namespace mod {
inline bool ProtectSpawnedFriendly(bool enabled,const AiIdentity& owned,const AiEntity& target,int playerTeam) {
    return enabled && owned.matches(target) && target.type==kActorType && target.number>1 && target.health>0
        && target.team==playerTeam && (playerTeam==kAxisTeam || playerTeam==kAlliesTeam);
}
inline bool ConfirmedPlayerActorHit(bool playerAttacker,int requestedDamage,const AiEntity& before,
    std::uint32_t currentSerial,int currentHealth) {
    return playerAttacker && requestedDamage>0 && before.valid && before.inUse && before.type==kActorType
        && before.health>0 && before.serial==currentSerial && currentHealth<before.health;
}
inline constexpr std::uint64_t kHitmarkerMilliseconds=160;
inline std::uint8_t HitmarkerAlpha(std::uint64_t lastHit,std::uint64_t now) {
    if(!lastHit || now<lastHit || now-lastHit>=kHitmarkerMilliseconds)return 0;
    const auto remaining=kHitmarkerMilliseconds-(now-lastHit);
    return static_cast<std::uint8_t>(remaining>=80?255:remaining*255/80);
}
} // namespace mod
