#pragma once
#include "ai_controls.hpp"
#include <limits>
#include <vector>
namespace mod {
struct AiTarget {std::uintptr_t entity=0;AiPoint position{};float squaredDistance=std::numeric_limits<float>::max();};
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
inline bool BodiesOverlap(const AiPoint& a,const AiPoint& b) {
    return FinitePoint(a) && FinitePoint(b) && std::abs(a[0]-b[0])<34.f && std::abs(a[1]-b[1])<34.f && std::abs(a[2]-b[2])<74.f;
}
} // namespace mod
