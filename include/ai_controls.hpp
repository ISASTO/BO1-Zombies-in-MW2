#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <algorithm>
#include "ai_placement.hpp"

namespace mod {
// Partial exact-retail AMD64 fields. No historical IW4/T5 actor struct casts.
inline constexpr std::size_t kAiEntityStride=0x2a0;
inline constexpr int kAiEntityCapacity=2048;
inline constexpr std::size_t kAiType=0,kAiUseCount=0x78,kAiNumber=0x7e,kAiInUse=0xb0;
inline constexpr std::size_t kAiOrigin=0xe8,kAiClient=0x108,kAiActor=0x110,kAiSentient=0x118;
// The script-visible classname is at 0x13e. The internal name at 0x13c is distinct.
inline constexpr std::size_t kAiHandler=0x13b,kAiClassname=0x13e,kAiSpawnFlags=0x14c,kAiHealth=0x164;
inline constexpr std::size_t kAiSpawnerTeam=0x178,kAiLastSpawn=0x17c,kAiSpawnCount=0x184;
inline constexpr std::size_t kAiTarget=0x144,kAiTargetname=0x146;
inline constexpr std::uint8_t kActorType=13,kActorSpawnerType=14,kActorCorpseType=15,kActorHandler=2;
inline constexpr int kAxisTeam=1,kAlliesTeam=2;
inline constexpr int kAiNativeActorCapacity=32,kAiSpawnerListLimit=kAiEntityCapacity,kAiRequestExpiryMilliseconds=10000;
struct AiEntity {
    bool valid=false,inUse=false;
    std::uintptr_t address=0,actor=0,sentient=0;
    std::uint32_t serial=0,flags=0;
    std::uint16_t number=0,classnameId=0;
    std::uint8_t type=0,handler=0;
    int health=0,team=0,spawnCount=0,lastSpawn=0;
    std::array<float,3> origin{};
    std::string classname;
};
template<class Reader> AiEntity ReadAiEntity(std::uintptr_t address,int index,Reader read) {
    AiEntity out; out.address=address;
    if(!address || (address&7) || index<0 || index>=kAiEntityCapacity) return out;
    std::array<std::uint8_t,0x188> data{};
    if(!read(address,data.data(),data.size())) return out;
    auto field=[&](std::size_t offset,auto& value) { std::memcpy(&value,data.data()+offset,sizeof(value)); };
    field(kAiType,out.type); field(kAiUseCount,out.serial); field(kAiNumber,out.number);
    std::uint8_t active=0; field(kAiInUse,active); out.inUse=active==1;
    if(!out.inUse || out.number!=index || (out.type!=kActorType && out.type!=kActorSpawnerType)) return out;
    field(kAiHandler,out.handler); field(kAiClassname,out.classnameId); field(kAiSpawnFlags,out.flags);
    field(kAiOrigin,out.origin); field(kAiHealth,out.health);
    for(const float coordinate:out.origin) if(!std::isfinite(coordinate) || std::abs(coordinate)>1000000.f) return out;
    if(out.type==kActorType) {
        std::uintptr_t client=0; field(kAiClient,client);
        field(kAiActor,out.actor); field(kAiSentient,out.sentient);
        std::uintptr_t actorSelf=0,sentientSelf=0;
        if(client || !out.actor || (out.actor&7) || !out.sentient || (out.sentient&7) || out.handler!=kActorHandler
            || out.health<0 || out.health>1000000 || !read(out.actor,&actorSelf,sizeof(actorSelf))
            || !read(out.sentient,&sentientSelf,sizeof(sentientSelf)) || actorSelf!=address || sentientSelf!=address
            || !read(out.sentient+8,&out.team,sizeof(out.team)) || out.team<1 || out.team>5) return out;
    } else {
        field(kAiSpawnerTeam,out.team); field(kAiSpawnCount,out.spawnCount); field(kAiLastSpawn,out.lastSpawn);
        if(out.team<1 || out.team>5 || out.spawnCount<-1000000 || out.spawnCount>1000000) return out;
    }
    std::uint32_t liveSerial=0;
    if(!read(address+kAiUseCount,&liveSerial,sizeof(liveSerial)) || liveSerial!=out.serial) return out;
    out.valid=true; return out;
}
inline bool EligibleSpawner(const AiEntity& e,int team) {
    std::string name=e.classname;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){ return c>='A' && c<='Z'?c+('a'-'A'):c; });
    return e.valid && e.inUse && e.type==kActorSpawnerType && e.number>1 && e.team==team
        && (team==kAxisTeam || team==kAlliesTeam) && !(e.flags&2)
        && name.rfind("actor_",0)==0 && name.find("_hero")==std::string::npos;
}
inline bool EligibleAiTemplate(const AiEntity& e) {
    if(e.type==kActorSpawnerType) return EligibleSpawner(e,e.team);
    return e.valid && e.inUse && e.type==kActorType && e.number>1 && e.health>0
        && (e.team==kAxisTeam || e.team==kAlliesTeam) && e.classname.rfind("actor_",0)==0
        && e.classname.find("_hero")==std::string::npos;
}
struct SpawnerBudgetLease {
    int original=0,submitted=0;
    static SpawnerBudgetLease plan(int count) { return {count,count==0?1:count}; }
    bool canRestore(int current) const {
        return current==submitted || (submitted>0 && current==submitted-1);
    }
};
struct AiIdentity {
    std::uintptr_t address=0,actor=0,sentient=0;
    std::uint32_t serial=0;
    std::uint16_t number=0;
    std::uint8_t type=0;
    static AiIdentity capture(const AiEntity& e) { return {e.address,e.actor,e.sentient,e.serial,e.number,e.type}; }
    bool matches(const AiEntity& e) const {
        return e.valid && e.inUse && address==e.address && serial==e.serial && number==e.number && type==e.type
            && actor==e.actor && sentient==e.sentient;
    }
};
inline bool HumanSpawner(const AiEntity& e) {
    // Live actors contain runtime VM/animation state, not safe spawner data.
    auto copy=e;copy.flags&=~2u;
    std::string name=e.classname;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A' && c<='Z'?c+('a'-'A'):c;});
    return e.type==kActorSpawnerType && EligibleSpawner(copy,e.team)
        && name.find("dog")==std::string::npos && name.find("animal")==std::string::npos;
}
struct AiAllocation {
    bool valid=false,inUse=false,client=false;
    std::uint32_t serial=0;std::uint16_t number=0;std::uint8_t type=0;
};
template<class Reader> AiAllocation ReadAiAllocation(const AiIdentity& identity,Reader read) {
    AiAllocation a;std::uintptr_t client=0;std::uint8_t active=0;
    a.valid=identity.address && read(identity.address+kAiUseCount,&a.serial,4)
        && read(identity.address+kAiNumber,&a.number,2) && read(identity.address+kAiType,&a.type,1)
        && read(identity.address+kAiInUse,&active,1) && active<=1
        && read(identity.address+kAiClient,&client,sizeof(client));
    a.inUse=active!=0;a.client=client!=0;return a;
}
inline bool SameAiAllocation(const AiIdentity& identity,const AiAllocation& a) {
    return a.valid && a.inUse && !a.client && a.number==identity.number && a.serial==identity.serial;
}
enum class AiOperation { SpawnEnemy, SpawnFriendly, RemoveOwned, KillEnemies };
enum class AiRequestResult { None, Queued, Ready, NotReady, Busy, NoSpawner, SpawnPaused, NativeRefused,
    Created, Deleted, LifetimeChanged, Expired, IdentityChanged, Protected, HookUnavailable, NativeReturnedInvalid, Limit,
    NoClearPosition, ModelUnavailable, Killed, ResourceGuard };
inline const char* AiResultText(AiRequestResult value) {
    switch(value) {
        case AiRequestResult::None: return "NONE";
        case AiRequestResult::Queued: return "QUEUED";
        case AiRequestResult::Ready: return "READY";
        case AiRequestResult::NotReady: return "NOT_READY";
        case AiRequestResult::Busy: return "BUSY";
        case AiRequestResult::NoSpawner: return "NO_SPAWNER";
        case AiRequestResult::SpawnPaused: return "SPAWN_PAUSED";
        case AiRequestResult::NativeRefused: return "NATIVE_REFUSED";
        case AiRequestResult::Created: return "CREATED";
        case AiRequestResult::Deleted: return "DELETED";
        case AiRequestResult::LifetimeChanged: return "LIFETIME_CHANGED";
        case AiRequestResult::Expired: return "EXPIRED";
        case AiRequestResult::IdentityChanged: return "IDENTITY_CHANGED";
        case AiRequestResult::Protected: return "PROTECTED";
        case AiRequestResult::HookUnavailable: return "HOOK_UNAVAILABLE";
        case AiRequestResult::NativeReturnedInvalid: return "NATIVE_INVALID";
        case AiRequestResult::Limit: return "AT_LIMIT";
        case AiRequestResult::NoClearPosition: return "NO_CLEAR_POSITION";
        case AiRequestResult::ModelUnavailable: return "MODEL_NOT_LOADED";
        case AiRequestResult::Killed: return "KILLED";
        case AiRequestResult::ResourceGuard: return "RESOURCE_GUARD";
    }
    return "UNKNOWN";
}
struct AiScope { bool ready=false; std::uintptr_t client=0,pool=0; int commandTime=0; const char* map=""; };
struct AiRequest {
    AiOperation operation=AiOperation::SpawnEnemy;
    AiIdentity source;
    AiIdentity appearance;
    std::uintptr_t client=0,pool=0;
    int commandTime=0;
    std::array<char,96> map{};
};
class AiRequestQueue {
public:
    bool pending() const { return request_.has_value(); }
    AiRequestResult reason() const { return result_; }
    bool enqueue(AiOperation op,const AiIdentity& source,const AiScope& scope,const AiIdentity& appearance={}) {
        if(!valid(scope)) { result_=AiRequestResult::NotReady; return false; }
        if(request_) { result_=AiRequestResult::Busy; return false; }
        AiRequest r; r.operation=op; r.source=source; r.appearance=appearance; r.client=scope.client; r.pool=scope.pool; r.commandTime=scope.commandTime;
        std::strcpy(r.map.data(),scope.map); request_=r; result_=AiRequestResult::Queued; return true;
    }
    std::optional<AiRequest> take(const AiScope& scope) {
        if(!request_) return {};
        if(discardStale(scope)) return {};
        auto r=*request_; request_.reset();
        result_=AiRequestResult::Ready; return r;
    }
    bool discardStale(const AiScope& scope) {
        if(!request_) return false;
        const auto& r=*request_;
        if(!valid(scope) || r.client!=scope.client || r.pool!=scope.pool || std::strcmp(r.map.data(),scope.map)!=0
            || scope.commandTime<r.commandTime) { cancel(AiRequestResult::LifetimeChanged); return true; }
        if(static_cast<std::int64_t>(scope.commandTime)-r.commandTime>kAiRequestExpiryMilliseconds) {
            cancel(AiRequestResult::Expired); return true;
        }
        return false;
    }
    void cancel(AiRequestResult why) { request_.reset(); result_=why; }
private:
    static bool valid(const AiScope& s) { return s.ready && s.client && s.pool && s.commandTime>=0 && s.map
        && s.map[0] && std::strlen(s.map)<96; }
    std::optional<AiRequest> request_;
    AiRequestResult result_=AiRequestResult::None;
};
struct AiStatus {
    bool resolved=false,ready=false,hookInstalled=false,pending=false,spawnPaused=false,pauseWanted=false;
    int enemyAlive=0,friendlyAlive=0,otherAlive=0,nativeCount=0,owned=0;
    unsigned long long resourceShed=0;
    int enemySpawner=0,enemySpawners=0,friendlySpawner=0,friendlySpawners=0;
    std::array<char,96> enemyName{},friendlyName{};
    float enemyDistance=0.f,friendlyDistance=0.f;
    bool friendlyFallback=false;
    int engineActors=0,engineLimit=kAiNativeActorCapacity;
    std::array<char,96> lastName{};
    AiPoint lastPosition{};
    float lastSpawnDistance=0.f;
    unsigned lastEntity=0;
    int lastTeam=0,placementAttempts=0;
    AiRequestResult lastResult=AiRequestResult::None;
    unsigned long long callbacks=0,created=0,deleted=0,killed=0,reclaimed=0;
    bool characterLoaded=true;
    int recovering=0;
    unsigned long long worldReads=0,serviceUpdates=0,combatReassertions=0;
    unsigned long long followGoalEntity=0,followRadius=0,followUnfixed=0;
    int followLastBatch=0,followPeakBatch=0;
    double worldAverageUs=0,worldPeakUs=0,serviceAverageUs=0,servicePeakUs=0;
    double lastPoolUs=0,lastDecodeUs=0,lastSortUs=0;
    unsigned long long worldKernelReads=0,worldImageReads=0,nameCacheHits=0,nameCacheMisses=0;
    unsigned long nativeThread=0;
};
} // namespace mod
