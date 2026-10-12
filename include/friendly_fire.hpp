#pragma once
#include "ai_controls.hpp"

namespace mod {
inline bool BypassFriendlyFirePenalty(bool enabled,const AiEntity& target,bool playerAttacker,int attackerTeam) {
    return enabled && playerAttacker && target.valid && target.inUse && target.type==kActorType
        && target.number>1 && target.health>0 && target.team==attackerTeam
        && (attackerTeam==kAxisTeam || attackerTeam==kAlliesTeam);
}
} // namespace mod
