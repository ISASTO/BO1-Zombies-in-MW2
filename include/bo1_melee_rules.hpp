#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace mod::bo1 {
// Scoped behavior port: ordinary BO1 zombie versus player, not AI-vs-AI,
// banzai/special actors or damage resolution. Source: patched zombie_melee.gsc
// CanMeleeInternal and zombie_init.gsc range setup. Inputs are adapter results;
// no T5/IW4 actor casts or native addresses belong in this portable rule.
inline constexpr float kMeleeRangeSquared=64.f*64.f;
inline constexpr float kChargeRangeSquared=200.f*200.f;
inline constexpr float kLongChargeRangeSquared=512.f*512.f;
enum class MeleeCheck { Normal, AlreadyStarted, AnyRange, LongRange };
enum class MeleeDecision { Allowed, TargetInvalid, TargetDead, Disabled, Facing,
    EnemyDistanceGuard, Shielded, BlockedPath, NeedsPathAdapter, OutOfRange,
    RequiresCharge, RecentMiss, TargetReserved, InvalidInput };
struct MeleeInputs {
    bool sentient=false,alive=false,disableMeleeDefined=false,magicBulletShield=false;
    float yawDegrees=0.f,distanceSquared=0.f;
    std::optional<float> closestEnemySquared;
    std::optional<std::int64_t> millisecondsSinceMiss;
    bool targetAvailable=true,checkChargePath=false;
    std::optional<bool> pathClear;
};
inline MeleeDecision CanMeleePlayer(const MeleeInputs& i,MeleeCheck state) {
    if(!std::isfinite(i.yawDegrees) || !std::isfinite(i.distanceSquared) || i.distanceSquared<0.f
        || (i.closestEnemySquared && (!std::isfinite(*i.closestEnemySquared) || *i.closestEnemySquared<0.f)))
        return MeleeDecision::InvalidInput;
    if(!i.sentient) return MeleeDecision::TargetInvalid;
    if(!i.alive) return MeleeDecision::TargetDead;
    if(i.disableMeleeDefined) return MeleeDecision::Disabled;
    const float yaw=std::abs(i.yawDegrees);
    if((yaw>60.f && state!=MeleeCheck::AlreadyStarted) || yaw>110.f) return MeleeDecision::Facing;
    // Preserve the actual native-distance/script comparison, not its prose comment.
    if(i.closestEnemySquared && *i.closestEnemySquared-.1f>i.distanceSquared) return MeleeDecision::EnemyDistanceGuard;
    if(i.magicBulletShield) return MeleeDecision::Shielded;
    const auto pathDecision=[&]() {
        if(!i.pathClear) return MeleeDecision::NeedsPathAdapter;
        return *i.pathClear?MeleeDecision::Allowed:MeleeDecision::BlockedPath;
    };
    if(i.distanceSquared<=kMeleeRangeSquared) return pathDecision();
    if(state!=MeleeCheck::AnyRange) {
        const float bound=state==MeleeCheck::LongRange?kLongChargeRangeSquared:kChargeRangeSquared;
        if(i.distanceSquared>bound) return MeleeDecision::OutOfRange;
    }
    if(state==MeleeCheck::AlreadyStarted) return MeleeDecision::RequiresCharge;
    if(i.millisecondsSinceMiss && *i.millisecondsSinceMiss<3000) return MeleeDecision::RecentMiss;
    if(!i.targetAvailable) return MeleeDecision::TargetReserved;
    if(i.checkChargePath) return pathDecision();
    return MeleeDecision::Allowed;
}
} // namespace mod::bo1
