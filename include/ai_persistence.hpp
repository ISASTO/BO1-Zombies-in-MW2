#pragma once
#include "ai_controls.hpp"
#include <optional>
#include <string>

namespace mod {
inline constexpr const char* kOwnedSpawnerTag="bo1mw2_spawner_v1";
inline constexpr const char* kOwnedActorTagPrefix="bo1mw2_actor_v1_";
// The user-1 interner acquires a shared engine-user reference only the first
// time that user requests a string. Repeated lookups return the same borrowed
// id without another temporary reference. Scr_SetString acquires the field's
// reference; releasing the returned id would cancel that field ownership.
template<class Intern,class Setter> bool AssignOwnedAiMarker(std::uintptr_t field,
    const std::string& marker,Intern intern,Setter set) {
    const auto id=intern(marker.c_str());
    if(!id || id>0xffff)return false;
    set(field,id);return true;
}
inline std::string OwnedActorTag(unsigned helper) {return std::string(kOwnedActorTagPrefix)+std::to_string(helper);}
inline std::optional<unsigned> OwnedActorHelper(const std::string& text) {
    const std::string prefix=kOwnedActorTagPrefix;
    if(text.compare(0,prefix.size(),prefix) || text.size()==prefix.size())return {};
    unsigned value=0;
    for(std::size_t n=prefix.size();n<text.size();++n) {
        if(text[n]<'0' || text[n]>'9')return {};
        value=value*10+static_cast<unsigned>(text[n]-'0');
        if(value>=kAiEntityCapacity-2)return {};
    }
    if(value<2 || OwnedActorTag(value)!=text)return {};
    return value;
}
inline bool RestorableOwnedActor(const AiEntity& actor,const AiEntity& helper,const std::string& marker,
    const std::string& helperMarker) {
    const auto number=OwnedActorHelper(marker);
    return number && *number==helper.number && actor.number!=helper.number && actor.valid && actor.inUse && actor.type==kActorType
        && actor.health>0 && actor.team==helper.team && HumanSpawner(helper) && helperMarker==kOwnedSpawnerTag;
}
} // namespace mod
