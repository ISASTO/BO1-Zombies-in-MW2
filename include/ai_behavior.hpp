#pragma once
#include "ai_controls.hpp"
#include <limits>
#include <vector>
namespace mod {
struct AiTarget {std::uintptr_t entity=0;AiPoint position{};float squaredDistance=std::numeric_limits<float>::max();};
inline bool LiveAiTarget(const AiEntity& cached,const AiEntity& live,int ownTeam) {
    return AiIdentity::capture(cached).matches(live) && live.type==kActorType && live.health>0
        && live.team!=ownTeam && (live.team==kAxisTeam || live.team==kAlliesTeam);
}
inline AiTarget ChooseAiTarget(const AiEntity& self,int playerTeam,std::uintptr_t player,const AiPoint& playerPosition,const std::vector<AiEntity>& actors) {
    AiTarget out;out.position=playerPosition;
    if(self.team!=playerTeam) {out.entity=player;out.squaredDistance=PointDistanceSquared(self.origin,playerPosition);}
    for(const auto& other:actors) {
        if(!other.valid || other.type!=kActorType || other.health<=0 || other.number==self.number || other.team==self.team
            || (other.team!=kAxisTeam && other.team!=kAlliesTeam))continue;
        const auto d=PointDistanceSquared(self.origin,other.origin);
        if(d<out.squaredDistance && (self.team!=playerTeam || d<2500.f*2500.f))out={other.address,other.origin,d};
    }
    return out;
}
inline bool NeedsAiGoal(bool hadGoal,const AiPoint& old,const AiPoint& desired) {
    return FinitePoint(desired) && (!hadGoal || PointDistanceSquared(old,desired)>96.f*96.f);
}
// Test38 squad following. Allies use the player as their native goal entity
// (the engine re-reads the player position), and this radius bounds where the
// native combat code may move them: wider for larger squads, and stretched
// toward the chosen enemy so allies leave the clump and engage it.
inline constexpr float kSquadMinimumRadius=384.f,kSquadCrowdRadius=1024.f,kSquadEngageRadius=1536.f;
inline float SquadFollowRadius(std::size_t followers,bool hasTarget,float targetFromPlayerSquared) {
    const float crowd=std::clamp(256.f+32.f*std::sqrt(static_cast<float>(followers)),kSquadMinimumRadius,kSquadCrowdRadius);
    if(!hasTarget || !std::isfinite(targetFromPlayerSquared) || targetFromPlayerSquared<0.f) return crowd;
    return std::clamp(crowd+.75f*std::sqrt(targetFromPlayerSquared),crowd,kSquadEngageRadius);
}
inline bool RadiusNeedsUpdate(float current,float wanted) {
    return !std::isfinite(current) || std::abs(current-wanted)>32.f;
}
// Per-frame squad service: at least the minimum, then up to the cap while the
// frame budget lasts. 256 actors are revisited within about half a second.
inline constexpr std::size_t kFollowMinimumPerTick=4,kFollowMaximumPerTick=32;
inline constexpr double kFollowBudgetMicroseconds=1500.0;
inline bool ContinueFollowBatch(std::size_t done,std::size_t available,double elapsedUs) {
    if(done>=available || done>=kFollowMaximumPerTick) return false;
    return done<kFollowMinimumPerTick || elapsedUs<kFollowBudgetMicroseconds;
}
inline bool BodiesOverlap(const AiPoint& a,const AiPoint& b) {
    return FinitePoint(a) && FinitePoint(b) && std::abs(a[0]-b[0])<34.f && std::abs(a[1]-b[1])<34.f && std::abs(a[2]-b[2])<74.f;
}
} // namespace mod
