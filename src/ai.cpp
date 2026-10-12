#include "runtime.hpp"
#include <cstdio>
#include <vector>
#include <limits>
#include <intrin.h>

namespace mod {
namespace {
using ThinkFn=void(*)(std::uintptr_t);
using SpawnFn=std::uintptr_t(*)(std::uintptr_t,int,int,int);
using DeleteFn=void(*)(unsigned);
using AllocateFn=std::uintptr_t(*)();
using EntityFn=void(*)(std::uintptr_t);
using CopyFn=void(*)(std::uintptr_t,std::uintptr_t);
using TraceFn=void(*)(void*,const float*,const float*,const AiBounds*,int,int);
using OriginFn=void(*)(std::uintptr_t,const float*);
using TeamFn=void(*)(std::uintptr_t,int);
using GoalFn=void(*)(std::uintptr_t,const float*,std::uintptr_t);
using EnemyFn=void(*)(std::uintptr_t,std::uintptr_t,int,int);
using AnimationFn=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t);
using ScriptStringFn=void(*)(std::uintptr_t,unsigned);
struct AiProfile {
    std::uintptr_t base=0;
    std::size_t size=0;
    bool resolved=false;
    std::uintptr_t think=0,thinkSlot=0,poolSlot=0,countSlot=0,activeArray=0,timeSlot=0;
    std::uintptr_t spawnEnabledSlot=0,spawnDisabledSlot=0,stringPoolSlot=0,spawn=0,remove=0,protectedSlot=0,fixedPool=0;
    std::uintptr_t allocate=0,freeEntity=0,copyFields=0,copyDynamic=0,trace=0,setOrigin=0,setTeam=0,setGoal=0,setEnemy=0;
    std::uintptr_t actorCountSlot=0,actorLimitSlot=0,animationBuilder=0,completeTable=0,definitionTable=0;
    std::uintptr_t setString=0;
} aiProfile;
struct OwnedAi { AiIdentity identity,spawner; int team=0,lastGoalTime=-1; };
struct HandBuild {
    std::uint64_t ticket=0; WeaponFields weapon;
    std::uintptr_t names=0,normal=0,cache=0;
    AiRequest lifetime;
    bool pending=false;
    NativeHandBuildResult result;
};
struct AiRuntime {
    AiStatus status;
    AiRequestQueue queue;
    std::vector<OwnedAi> owned;
    std::vector<AiEntity> enemies,friends,enemyPoints;
    int enemyIndex=0,friendlyIndex=0;
    ULONGLONG nextRead=0;
    bool pauseWanted=false,scopeActive=false;
    ScalarOwnership<std::uint8_t> pause;
    ScalarOwnership<int> capacity;
    HandBuild hands;
    std::vector<HandBuild> handCache;
    std::uint64_t nextHandTicket=0;
    std::uintptr_t client=0,pool=0;
    int commandTime=0,lastServiceTime=-1;
    int nextFollowTime=-1;
    char map[96]{};
} ai;
std::mutex aiMutex;
std::atomic<ThinkFn> originalThink{nullptr};
std::atomic<bool> nativePending{false};
std::atomic<unsigned long long> nativeCallbacks{0};
std::atomic<DWORD> nativeThread{0};
thread_local bool servicing=false;

bool ReadAi(std::uintptr_t at,void* out,std::size_t length) {
    SIZE_T copied=0;
    return at && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,length,&copied) && copied==length;
}
template<class T> bool Value(std::uintptr_t at,T& out) { return ReadAi(at,&out,sizeof(out)); }
bool InHost(std::uintptr_t at,std::size_t size) {
    return at>=aiProfile.base && size<=aiProfile.size && at-aiProfile.base<=aiProfile.size-size;
}
bool CString(std::uintptr_t at,char* out,std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if(!at || size<2 || !VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info)) || info.State!=MEM_COMMIT
        || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto available=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize-at;
    const auto count=std::min(size,static_cast<std::size_t>(available));
    if(!ReadAi(at,out,count)) return false;
    for(std::size_t n=0;n<count;++n) {
        if(!out[n]) return true;
        if(static_cast<unsigned char>(out[n])<32 || static_cast<unsigned char>(out[n])>126) return false;
    }
    return false;
}
std::string ClassName(std::uint16_t id) {
    std::uintptr_t pool=0; char text[128]{};
    if(!id || !Value(aiProfile.stringPoolSlot,pool) || !pool || !CString(pool+static_cast<std::uintptr_t>(id)*16+4,text,sizeof(text))) return {};
    return text;
}
std::uintptr_t Rip(std::uintptr_t at,int offset,int displacement,int length) {
    std::int32_t rel=0;
    return Value(at+offset+displacement,rel)?at+offset+length+rel:0;
}
std::uintptr_t Call(std::uintptr_t at,int offset) {
    std::uint8_t opcode=0;
    return Value(at+offset,opcode) && opcode==0xe8?Rip(at,offset,1,5):0;
}
std::uintptr_t Jump(std::uintptr_t at,int offset) {
    std::uint8_t opcode=0;
    return Value(at+offset,opcode) && opcode==0xe9?Rip(at,offset,1,5):0;
}
std::uintptr_t Unique(std::uintptr_t text,std::size_t size,const char* name,const char* pattern) {
    const auto matches=FindPattern(reinterpret_cast<const std::uint8_t*>(text),size,ParsePattern(pattern));
    if(matches.size()!=1) { Log("AI_RESOLVE %s matches=%llu blocked",name,static_cast<unsigned long long>(matches.size())); return 0; }
    const auto address=text+matches.front();
    Log("AI_RESOLVE %s rva=0x%llx",name,static_cast<unsigned long long>(address-aiProfile.base)); return address;
}
bool MethodEntry(const char* name,std::uintptr_t function) {
    // Validate the original compiled name/function/flags table independently of code anchors.
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(aiProfile.base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(aiProfile.base+dos->e_lfanew);
    const auto* section=IMAGE_FIRST_SECTION(nt);
    unsigned matches=0;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        if(section[s].Characteristics&IMAGE_SCN_CNT_CODE) continue;
        const auto begin=aiProfile.base+section[s].VirtualAddress;
        const auto size=section[s].Misc.VirtualSize;
        if(!InHost(begin,size)) continue;
        // Only image-backed data; avoid probing huge uninitialized sections.
        const auto count=std::min<std::size_t>(size,section[s].SizeOfRawData);
        std::vector<std::uint8_t> data(count);
        if(!ReadAi(begin,data.data(),data.size())) continue;
        for(std::size_t n=8;n+8<=count;n+=8) {
            std::uintptr_t value=0,pointer=0;
            std::memcpy(&value,data.data()+n,8);
            if(value!=function) continue;
            std::memcpy(&pointer,data.data()+n-8,8);
            if(!InHost(pointer,1)) continue;
            char text[40]{}; if(CString(pointer,text,sizeof(text)) && std::strcmp(text,name)==0) ++matches;
        }
    }
    return matches==1;
}
bool TeamNames() {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(aiProfile.base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(aiProfile.base+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    static constexpr const char* names[]={"invalid","axis","allies","neutral","team3","dead"};
    struct DataSection { std::uintptr_t address=0; std::vector<std::uint8_t> bytes; };
    std::vector<DataSection> dataSections;
    std::vector<std::uintptr_t> invalidNames;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        if(sections[s].Characteristics&IMAGE_SCN_CNT_CODE) continue;
        const auto begin=aiProfile.base+sections[s].VirtualAddress;
        const auto count=std::min<std::size_t>(sections[s].Misc.VirtualSize,sections[s].SizeOfRawData);
        if(!InHost(begin,count)) continue;
        std::vector<std::uint8_t> data(count);
        if(!ReadAi(begin,data.data(),data.size())) continue;
        // Find the first literal once; don't probe every pointer-sized data word.
        constexpr char invalid[]="invalid";
        auto at=data.begin();
        while((at=std::search(at,data.end(),std::begin(invalid),std::end(invalid)))!=data.end()) {
            invalidNames.push_back(begin+static_cast<std::size_t>(at-data.begin())); ++at;
        }
        dataSections.push_back({begin,std::move(data)});
    }
    unsigned found=0;
    for(const auto& section:dataSections) {
        const auto& data=section.bytes;
        const auto count=data.size();
        for(std::size_t n=0;n+sizeof(std::uintptr_t)*6<=count;n+=8) {
            std::array<std::uintptr_t,6> pointers{};
            std::memcpy(pointers.data(),data.data()+n,sizeof(pointers));
            if(std::find(invalidNames.begin(),invalidNames.end(),pointers[0])==invalidNames.end()) continue;
            bool same=true;
            for(int k=1;k<6 && same;++k) {
                char text[24]{}; same=InHost(pointers[k],1) && CString(pointers[k],text,sizeof(text)) && !std::strcmp(text,names[k]);
            }
            if(same) ++found;
        }
    }
    return found==1;
}
bool EntityFields() {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(aiProfile.base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(aiProfile.base+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    unsigned found=0;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        if(sections[s].Characteristics&IMAGE_SCN_CNT_CODE) continue;
        const auto begin=aiProfile.base+sections[s].VirtualAddress;
        const auto count=std::min<std::size_t>(sections[s].Misc.VirtualSize,sections[s].SizeOfRawData);
        if(!InHost(begin,count)) continue;
        std::vector<std::uint8_t> data(count);
        if(!ReadAi(begin,data.data(),data.size())) continue;
        // Independent native field descriptors: name pointer, DWORD offset/type,
        // then setter/getter pointers, 32-byte stride.
        for(std::size_t n=0;n+128<=count;n+=8) {
            std::uint32_t offset=0,type=0;
            std::memcpy(&offset,data.data()+n+8,4); std::memcpy(&type,data.data()+n+12,4);
            if(offset!=kAiClassname || type!=4) continue;
            auto field=[&](std::size_t delta,const char* wanted,std::size_t member,unsigned expectedType) {
                std::uintptr_t pointer=0; std::uint32_t at=0,kind=0; char name[24]{};
                std::memcpy(&pointer,data.data()+n+delta,8);
                std::memcpy(&at,data.data()+n+delta+8,4); std::memcpy(&kind,data.data()+n+delta+12,4);
                return at==member && kind==expectedType && InHost(pointer,1) && CString(pointer,name,sizeof(name)) && !std::strcmp(name,wanted);
            };
            if(field(0,"classname",kAiClassname,4) && field(32,"origin",kAiOrigin,6) && field(96,"spawnflags",kAiSpawnFlags,0)) ++found;
        }
    }
    return found==1;
}
struct DvarRead { bool valid=false; std::uintptr_t address=0; std::uint8_t current=0; };
DvarRead Bool(std::uintptr_t slot,const char* expected) {
    DvarRead out; std::uintptr_t name=0; std::uint8_t type=255; char text[64]{};
    out.valid=Value(slot,out.address) && out.address && Value(out.address,name) && Value(out.address+0xc,type) && type==0
        && CString(name,text,sizeof(text)) && !std::strcmp(text,expected) && Value(out.address+0x10,out.current) && out.current<=1;
    return out;
}
bool Writable(std::uintptr_t at) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
bool SetByte(std::uintptr_t at,std::uint8_t before,std::uint8_t after) {
    if(before==after) return true;
    if(!Writable(at)) return false;
#if defined(_MSC_VER)
    return static_cast<std::uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(at),
        static_cast<CHAR>(after),static_cast<CHAR>(before)))==before;
#else
    auto expected=before;
    return __atomic_compare_exchange_n(reinterpret_cast<volatile std::uint8_t*>(at),&expected,after,false,
        __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
#endif
}
bool SetCount(std::uintptr_t at,int before,int after) {
    return before==after || (!(at&3) && Writable(at) && InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(at),after,before)==before);
}
void Pause(const Snapshot& s) {
    const auto d=Bool(aiProfile.spawnDisabledSlot,"ai_disableSpawn");
    if(!d.valid) return;
    if(ai.pauseWanted && s.state==LevelState::Ready) {
        if(d.current!=1 && SetByte(d.address+0x10,d.current,1)) {
            ai.pause.applied(d.address,d.current,1); Log("AI_SPAWN_PAUSE applied original=%u",d.current);
        }
    } else if(ai.pause.owned) {
        if(!ai.pause.canRestore(d.address,d.current)) ai.pause.clear();
        else if(SetByte(d.address+0x10,d.current,ai.pause.original)) { Log("AI_SPAWN_PAUSE restored=%u",ai.pause.original); ai.pause.clear(); }
    }
    ai.status.spawnPaused=d.current!=0; ai.status.pauseWanted=ai.pauseWanted;
}
AiScope Scope(const Snapshot& s) {
    std::uintptr_t pool=0;
    const bool valid=aiProfile.resolved && Value(aiProfile.poolSlot,pool) && pool==aiProfile.fixedPool
        && s.supported && s.state==LevelState::Ready && s.client.valid;
    return {valid,s.client.address,pool,s.client.commandTime,s.map};
}
AiEntity Entity(const AiIdentity& identity,std::uintptr_t pool,bool name=true) {
    const auto address=pool+static_cast<std::uintptr_t>(identity.number)*kAiEntityStride;
    if(address!=identity.address) return {};
    auto e=ReadAiEntity(address,identity.number,ReadAi);if(name)e.classname=ClassName(e.classnameId);return e;
}
int OwnCount() { return static_cast<int>(ai.owned.size()); }
int PlayerTeam(const Snapshot& s) {
    std::uintptr_t sentient=0,self=0; int team=0;
    if(Value(s.entity+kAiSentient,sentient) && sentient && Value(sentient,self) && self==s.entity
        && Value(sentient+8,team) && (team==kAxisTeam || team==kAlliesTeam)) return team;
    return kAlliesTeam;
}
void WorkWanted() { nativePending=ai.queue.pending() || ai.hands.pending || !ai.owned.empty(); }
void RestoreCapacity();
void Lifetime(const AiScope& s) {
    const bool changed=!s.ready || (ai.scopeActive && (ai.client!=s.client || ai.pool!=s.pool || std::strcmp(ai.map,s.map)
        || s.commandTime<ai.commandTime));
    if(changed) {
        RestoreCapacity();
        if(ai.queue.pending()) { ai.queue.cancel(AiRequestResult::LifetimeChanged); ai.status.lastResult=AiRequestResult::LifetimeChanged; }
        nativePending=false; ai.status.pending=false; ai.owned={}; ai.scopeActive=false;
        ai.enemies.clear(); ai.friends.clear(); ai.enemyPoints.clear(); ai.nextRead=0; ai.lastServiceTime=-1;ai.nextFollowTime=-1;
        ai.status.lastEntity=0;ai.status.lastPosition={};ai.status.lastName={};ai.status.placementAttempts=0;ai.status.lastSpawnDistance=0.f;
        ai.hands.pending=false; ai.hands.result.state=NativeHandBuildState::Canceled; ai.handCache.clear();
    }
    if(s.ready) {
        ai.scopeActive=true; ai.client=s.client; ai.pool=s.pool; ai.commandTime=s.commandTime;
        std::strncpy(ai.map,s.map,sizeof(ai.map)-1); ai.map[sizeof(ai.map)-1]='\0';
    }
}
void Finish(AiRequestResult result) {
    if(ai.owned.empty())RestoreCapacity();
    ai.status.lastResult=result; ai.status.pending=ai.queue.pending();
    WorkWanted(); ai.nextRead=0;
    Log("AI_RESULT result=%s native_thread=%lu owned=%d",AiResultText(result),GetCurrentThreadId(),OwnCount());
}
struct IntDvarRead { bool valid=false; std::uintptr_t address=0; int current=0; };
IntDvarRead ActorLimit() {
    IntDvarRead d; std::uintptr_t name=0; std::uint8_t type=255; char text[32]{};
    d.valid=Value(aiProfile.actorLimitSlot,d.address) && d.address && Value(d.address,name)
        && Value(d.address+0xc,type) && type==5 && CString(name,text,sizeof(text)) && !std::strcmp(text,"ai_count")
        && Value(d.address+0x10,d.current) && d.current>=0 && d.current<=kAiNativeActorCapacity;
    return d;
}
void RestoreCapacity() {
    if(!ai.capacity.owned) return;
    const auto d=ActorLimit();
    if(!d.valid) return;
    if(!ai.capacity.canRestore(d.address,d.current)) ai.capacity.clear();
    else if(SetCount(d.address+0x10,d.current,ai.capacity.original)) {
        Log("AI_CAPACITY restored=%d",ai.capacity.original); ai.capacity.clear();
    }
}
AiTraceResult Trace(const AiPoint& start,const AiPoint& end,const AiBounds& bounds) {
    alignas(16) std::array<std::uint8_t,48> output{};
    if(!FinitePoint(start) || !FinitePoint(end)) return {};
    reinterpret_cast<TraceFn>(aiProfile.trace)(output.data(),start.data(),end.data(),&bounds,0x7ff,kAiPlacementMask);
    return DecodeAiTrace(output);
}
bool SameWeapon(const WeaponFields& a,const WeaponFields& b) {
    return a.valid && b.valid && a.index==b.index && a.complete==b.complete && a.definition==b.definition
        && a.type==b.type && !std::strcmp(a.name,b.name);
}
bool HandScope(const HandBuild& h,const AiScope& scope) {
    const auto& r=h.lifetime;
    return scope.ready && r.client==scope.client && r.pool==scope.pool && !std::strcmp(r.map.data(),scope.map)
        && scope.commandTime>=r.commandTime && static_cast<std::int64_t>(scope.commandTime)-r.commandTime<=kAiRequestExpiryMilliseconds;
}
bool HandInputs(const HandBuild& h) {
    std::uintptr_t complete=0,definition=0,names=0,normal=0; std::uint8_t veto=255;
    std::array<std::uintptr_t,kWeaponAnimCount> strings{};
    if(!Value(aiProfile.completeTable+h.weapon.index*8,complete) || complete!=h.weapon.complete
        || !Value(aiProfile.definitionTable+h.weapon.index*8,definition) || definition!=h.weapon.definition
        || !Value(complete+kNormalWeaponAnims,names) || names!=h.names || !Value(h.cache,normal) || normal!=h.normal
        || !Value(definition+kNoDualWield,veto) || veto || !ReadAi(names,strings.data(),sizeof(strings))) return false;
    return UsableAnimationNames(strings,[](std::uintptr_t at,char* out,std::size_t count){return CString(at,out,count);});
}
void ServiceHands(const AiScope& scope) {
    auto& h=ai.hands;
    if(!h.pending) return;
    h.pending=false;
    if(!HandScope(h,scope) || !HandInputs(h)) {h.result.state=NativeHandBuildState::Canceled;return;}
    for(const auto& cached:ai.handCache) if(SameWeapon(cached.weapon,h.weapon) && cached.names==h.names && cached.normal==h.normal) {
        h.result=cached.result; Log("DUAL_NATIVE reuse index=%u",h.weapon.index); return;
    }
    Log("DUAL_NATIVE_BEGIN index=%u name=%s native_thread=%lu normal_tree=%p",h.weapon.index,h.weapon.name,
        GetCurrentThreadId(),reinterpret_cast<void*>(h.normal));
    // The exact native registration caller uses (WeaponDef*, names**). Keep the
    // known loaded normal table; do not resolve unverified authored asset names.
    const auto build=reinterpret_cast<AnimationFn>(aiProfile.animationBuilder);
    const auto right=build(h.weapon.definition,h.names),left=build(h.weapon.definition,h.names);
    MEMORY_BASIC_INFORMATION r{},l{};
    if(!right || !left || right==left || right==h.normal || left==h.normal
        || !VirtualQuery(reinterpret_cast<void*>(right),&r,sizeof(r)) || r.State!=MEM_COMMIT
        || !VirtualQuery(reinterpret_cast<void*>(left),&l,sizeof(l)) || l.State!=MEM_COMMIT) {
        h.result.state=NativeHandBuildState::Failed;return;
    }
    if(!HandScope(h,scope) || !HandInputs(h)) {h.result.state=NativeHandBuildState::Canceled;return;}
    h.result={NativeHandBuildState::Ready,right,left}; ai.handCache.push_back(h);
    Log("DUAL_NATIVE_READY index=%u right=%p left=%p definitions_distinct=1",h.weapon.index,
        reinterpret_cast<void*>(right),reinterpret_cast<void*>(left));
}
void FollowOwned(const Snapshot& s,const AiScope& scope,int time) {
    AiPoint player{};
    if(!scope.ready || !ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player)) return;
    for(auto it=ai.owned.begin();it!=ai.owned.end();) {
        const auto e=Entity(it->identity,scope.pool,false);
        if(!it->identity.matches(e)) {
            const auto spawner=Entity(it->spawner,scope.pool,false);
            if(it->spawner.matches(spawner)) reinterpret_cast<EntityFn>(aiProfile.freeEntity)(spawner.address);
            it=ai.owned.erase(it);continue;
        }
        if(e.health>0 && (it->lastGoalTime<0 || time-it->lastGoalTime>=500)) {
            auto target=player;
            // Friendly spacing avoids everybody sharing the player's exact feet.
            if(it->team==PlayerTeam(s)) {
                const float angle=static_cast<float>(e.number%8)*.7853981633974483f;
                target[0]+=96.f*std::cos(angle);target[1]+=96.f*std::sin(angle);
            }
            reinterpret_cast<GoalFn>(aiProfile.setGoal)(e.actor,target.data(),0);
            it->lastGoalTime=time;
        }
        ++it;
    }
    if(ai.owned.empty()) RestoreCapacity();
}
void Service() {
    if(servicing || !nativePending.load(std::memory_order_acquire)) return;
    servicing=true;
    struct ClearServiceFlag { ~ClearServiceFlag() { servicing=false; } } clearServiceFlag;
    std::lock_guard<std::mutex> lock(aiMutex);
    int time=0;
    if(!Value(aiProfile.timeSlot,time) || time==ai.lastServiceTime) return;
    if(!ai.queue.pending() && !ai.hands.pending && ai.nextFollowTime>=0 && time<ai.nextFollowTime && time>=ai.lastServiceTime)return;
    const auto s=ReadSnapshot(); const auto scope=Scope(s); Lifetime(scope);
    if(!scope.ready)return;
    ai.lastServiceTime=time;
    ai.nextFollowTime=time+500;ServiceHands(scope); FollowOwned(s,scope,time);
    if(!ai.queue.pending()) {WorkWanted();return;}
    const auto request=ai.queue.take(scope);
    if(!request) {Finish(ai.queue.reason());return;}
    if(request->operation==AiOperation::RemoveOwned) {
        unsigned removed=0; bool protectedActor=false;
        for(auto it=ai.owned.begin();it!=ai.owned.end();) {
            const auto e=Entity(it->identity,scope.pool,false);
            if(it->identity.matches(e) && e.type==kActorType && e.number>1) {
                int protectedNumber=-1;
                if(!Value(aiProfile.protectedSlot,protectedNumber) || protectedNumber==e.number) {
                    protectedActor=true;++it;continue;
                }
                Log("AI_DELETE_BEGIN entity=%u serial=%u",e.number,e.serial);
                reinterpret_cast<DeleteFn>(aiProfile.remove)(e.number);++removed;++ai.status.deleted;
            }
            const auto spawner=Entity(it->spawner,scope.pool,false);
            if(it->spawner.matches(spawner)) reinterpret_cast<EntityFn>(aiProfile.freeEntity)(spawner.address);
            it=ai.owned.erase(it);
        }
        if(ai.owned.empty()) RestoreCapacity();
        Finish(removed?AiRequestResult::Deleted:protectedActor?AiRequestResult::Protected:AiRequestResult::IdentityChanged);return;
    }
    const auto source=Entity(request->source,scope.pool);
    const bool friendly=request->operation==AiOperation::SpawnFriendly;
    const int playerTeam=PlayerTeam(s),team=friendly?playerTeam:(playerTeam==kAlliesTeam?kAxisTeam:kAlliesTeam);
    if(!request->source.matches(source) || !EligibleAiTemplate(source)) {Finish(AiRequestResult::IdentityChanged);return;}
    const auto enabled=Bool(aiProfile.spawnEnabledSlot,"g_spawnai"),disabled=Bool(aiProfile.spawnDisabledSlot,"ai_disableSpawn");
    if(!enabled.valid || !enabled.current || !disabled.valid || disabled.current || ai.pauseWanted) {Finish(AiRequestResult::SpawnPaused);return;}
    const auto limit=ActorLimit(); int actorCount=0;
    if(!limit.valid || !Value(aiProfile.actorCountSlot,actorCount) || actorCount<0 || actorCount>=kAiNativeActorCapacity) {Finish(AiRequestResult::Limit);return;}
    if(limit.current<kAiNativeActorCapacity && SetCount(limit.address+0x10,limit.current,kAiNativeActorCapacity)) {
        ai.capacity.applied(limit.address,limit.current,kAiNativeActorCapacity);
        Log("AI_CAPACITY requested native maximum %d original=%d",kAiNativeActorCapacity,limit.current);
    }
    std::array<std::uint8_t,kAiEntityCapacity> active{};
    if(!ReadAi(aiProfile.activeArray,active.data(),active.size())
        || std::count(active.begin()+2,active.end()-2,0)<2) {Finish(AiRequestResult::Limit);return;}
    AiPoint center{};
    if(!ReadAi(s.entity+kAiOrigin,center.data(),sizeof(center)) || !FinitePoint(center)) {Finish(AiRequestResult::NotReady);return;}
    const AiPoint player=center;
    bool nearPlayer=friendly; unsigned pointNumber=0;
    if(!friendly) {
        float nearest=std::numeric_limits<float>::max();
        for(const auto& point:ai.enemyPoints) {
            const auto live=Entity(AiIdentity::capture(point),scope.pool);
            if(!EligibleAiTemplate(live) || live.type!=kActorSpawnerType) continue;
            const auto distance=PointDistanceSquared(live.origin,player);
            if(distance<nearest) {nearest=distance;center=live.origin;pointNumber=live.number;}
        }
        if(!pointNumber) nearPlayer=true;
    }
    const auto placement=FindSpawnPlacement(center,nearPlayer,Trace);
    if(!placement) {Finish(AiRequestResult::NoClearPosition);return;}
    // Allocate a real engine entity and copy registered fields/VM vars through
    // native helpers. Original mission spawners, origins and budgets stay intact.
    const auto temporary=reinterpret_cast<AllocateFn>(aiProfile.allocate)();
    if(!temporary || temporary<scope.pool || (temporary-scope.pool)%kAiEntityStride) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    struct Temporary {std::uintptr_t pointer;bool keep=false;~Temporary(){if(!keep)reinterpret_cast<EntityFn>(aiProfile.freeEntity)(pointer);}} cleanup{temporary};
    reinterpret_cast<CopyFn>(aiProfile.copyFields)(temporary,source.address);
    reinterpret_cast<CopyFn>(aiProfile.copyDynamic)(temporary,source.address);
    // Persistent helper spawners must not duplicate a mission targetname or
    // inherit an encounter's scripted target. Native setter maintains refcounts.
    reinterpret_cast<ScriptStringFn>(aiProfile.setString)(temporary+kAiTargetname,0);
    reinterpret_cast<ScriptStringFn>(aiProfile.setString)(temporary+kAiTarget,0);
    std::uint8_t type=0; std::uint32_t flags=0; int count=0,oldTeam=0;
    if(!Value(temporary,type) || !SetByte(temporary,type,kActorSpawnerType)
        || !Value(temporary+kAiSpawnFlags,flags) || !SetCount(temporary+kAiSpawnFlags,static_cast<int>(flags),static_cast<int>(flags&~3u))
        || !Value(temporary+kAiSpawnCount,count) || !SetCount(temporary+kAiSpawnCount,count,1)
        || !Value(temporary+kAiSpawnerTeam,oldTeam) || !SetCount(temporary+kAiSpawnerTeam,oldTeam,team)) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    reinterpret_cast<OriginFn>(aiProfile.setOrigin)(temporary,placement->position.data());
    Log("AI_SPAWN_BEGIN team=%d source=%u serial=%u classname=%s point=%u placement_attempts=%d position=%.1f,%.1f,%.1f native_thread=%lu",
        team,source.number,source.serial,source.classname.c_str(),pointNumber,placement->attempts,
        placement->position[0],placement->position[1],placement->position[2],GetCurrentThreadId());
    // Argument three skips the factory's visibility/player-distance refusal.
    // The separate eviction spawnflag2 is cleared; physical clearance was traced.
    const auto spawned=reinterpret_cast<SpawnFn>(aiProfile.spawn)(temporary,0,1,0);
    if(!spawned) {Finish(AiRequestResult::NativeRefused);return;}
    if(spawned<scope.pool || (spawned-scope.pool)%kAiEntityStride
        || (spawned-scope.pool)/kAiEntityStride>=static_cast<std::uintptr_t>(kAiEntityCapacity-2)) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    auto actor=ReadAiEntity(spawned,static_cast<int>((spawned-scope.pool)/kAiEntityStride),ReadAi);
    if(!actor.valid || actor.type!=kActorType) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    reinterpret_cast<TeamFn>(aiProfile.setTeam)(actor.sentient,team);
    reinterpret_cast<EnemyFn>(aiProfile.setEnemy)(actor.sentient,friendly?0:s.entity,1,0);
    actor=ReadAiEntity(spawned,actor.number,ReadAi);
    if(!actor.valid || actor.team!=team) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    const auto temp=ReadAiEntity(temporary,static_cast<int>((temporary-scope.pool)/kAiEntityStride),ReadAi);
    if(!temp.valid) {reinterpret_cast<DeleteFn>(aiProfile.remove)(actor.number);Finish(AiRequestResult::NativeReturnedInvalid);return;}
    ai.owned.push_back({AiIdentity::capture(actor),AiIdentity::capture(temp),team,-1});cleanup.keep=true;++ai.status.created;
    ai.status.lastEntity=actor.number;ai.status.lastTeam=team;ai.status.lastPosition=actor.origin;
    ai.status.lastSpawnDistance=std::sqrt(PointDistanceSquared(actor.origin,player));ai.status.placementAttempts=placement->attempts;
    std::snprintf(ai.status.lastName.data(),ai.status.lastName.size(),"%s",source.classname.c_str());
    Log("AI_SPAWN_ACCEPT entity=%u serial=%u actor=%p sentient=%p team=%d health=%d origin=%.1f,%.1f,%.1f distance=%.1f original_spawner_untouched=1",
        actor.number,actor.serial,reinterpret_cast<void*>(actor.actor),reinterpret_cast<void*>(actor.sentient),actor.team,actor.health,
        actor.origin[0],actor.origin[1],actor.origin[2],ai.status.lastSpawnDistance);
    FollowOwned(s,scope,time);Finish(AiRequestResult::Created);
}
void OnActorThink(std::uintptr_t entity) {
    const auto original=originalThink.load(std::memory_order_acquire);
    if(!original) return;
    original(entity);
    ++nativeCallbacks;
    DWORD expected=0; nativeThread.compare_exchange_strong(expected,GetCurrentThreadId());
    try { Service(); }
    catch(...) {
        std::lock_guard<std::mutex> lock(aiMutex);
        ai.queue.cancel(AiRequestResult::NativeReturnedInvalid);
        ai.hands.pending=false;ai.hands.result.state=NativeHandBuildState::Failed;WorkWanted();
        Log("AI_SERVICE C++ exception; request canceled, native exceptions still reach the crash handler");
    }
}
bool InstallService() {
    if(ai.status.hookInstalled) return true;
    std::uintptr_t current=0;
    if(!aiProfile.resolved || !Value(aiProfile.thinkSlot,current) || current!=aiProfile.think) return false;
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(aiProfile.thinkSlot),8,PAGE_READWRITE,&old)) return false;
    originalThink.store(reinterpret_cast<ThinkFn>(current),std::memory_order_release);
    void* observed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(aiProfile.thinkSlot),
        reinterpret_cast<void*>(&OnActorThink),reinterpret_cast<void*>(current));
    DWORD ignored=0;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(aiProfile.thinkSlot),8,old,&ignored)!=0;
    ai.status.hookInstalled=observed==reinterpret_cast<void*>(current);
    Log("AI_SERVICE installed=%d protection_restored=%d slot_rva=0x%llx callback_rva=0x%llx",ai.status.hookInstalled,restored,
        static_cast<unsigned long long>(aiProfile.thinkSlot-aiProfile.base),static_cast<unsigned long long>(current-aiProfile.base));
    return ai.status.hookInstalled;
}
float Distance(const AiEntity& e,const std::array<float,3>& origin) {
    float squared=0;
    for(int n=0;n<3;++n) { const float delta=e.origin[n]-origin[n]; squared+=delta*delta; }
    return std::sqrt(squared);
}
void ReadWorld(const Snapshot& s,const AiScope& scope) {
    int count=0;
    if(!scope.ready || !Value(aiProfile.countSlot,count) || count<2 || count>kAiEntityCapacity-2) {ai.status.ready=false;return;}
    std::vector<std::uint8_t> pool(static_cast<std::size_t>(count)*kAiEntityStride),active(static_cast<std::size_t>(count));
    if(!ReadAi(scope.pool,pool.data(),pool.size()) || !ReadAi(aiProfile.activeArray,active.data(),active.size())) {ai.status.ready=false;return;}
    AiPoint player{};
    if(!ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player)) {ai.status.ready=false;return;}
    const int friendlyTeam=PlayerTeam(s),enemyTeam=friendlyTeam==kAlliesTeam?kAxisTeam:kAlliesTeam;
    const std::string oldEnemy=ai.enemies.empty()?"":ai.enemies[ai.enemyIndex].classname;
    const std::string oldFriendly=ai.friends.empty()?"":ai.friends[ai.friendlyIndex].classname;
    ai.enemies.clear();ai.friends.clear();ai.enemyPoints.clear();
    ai.status.enemyAlive=0;ai.status.friendlyAlive=0;ai.status.otherAlive=0;ai.status.nativeCount=0;
    for(int n=2;n<count;++n) {
        if(!active[n]) continue;
        const auto type=pool[static_cast<std::size_t>(n)*kAiEntityStride];
        if(type!=kActorType && type!=kActorSpawnerType) continue;
        const auto address=scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride;
        auto snapshotRead=[&](std::uintptr_t at,void* out,std::size_t size) {
            if(at==address && size==0x188) {std::memcpy(out,pool.data()+static_cast<std::size_t>(n)*kAiEntityStride,size);return true;}
            return ReadAi(at,out,size);
        };
        auto e=ReadAiEntity(address,n,snapshotRead);if(!e.valid)continue;
        e.classname=ClassName(e.classnameId);
        if(e.type==kActorType) {
            ++ai.status.nativeCount;
            if(e.health>0) {
                if(e.team==enemyTeam)++ai.status.enemyAlive;
                else if(e.team==friendlyTeam)++ai.status.friendlyAlive;
                else ++ai.status.otherAlive;
            }
        }
        const bool ours=std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==e.address || x.spawner.address==e.address;});
        if(ours || !EligibleAiTemplate(e))continue;
        if(e.team==enemyTeam) {
            ai.enemies.push_back(e);
            if(e.type==kActorSpawnerType)ai.enemyPoints.push_back(e);
        } else if(e.team==friendlyTeam)ai.friends.push_back(e);
    }
    auto sort=[&](auto& list) {std::sort(list.begin(),list.end(),[&](const auto& a,const auto& b){
        const float da=PointDistanceSquared(a.origin,player),db=PointDistanceSquared(b.origin,player);
        return da==db?a.number<b.number:da<db;
    });};
    sort(ai.enemies);sort(ai.friends);sort(ai.enemyPoints);
    auto unique=[](auto& list) {
        std::vector<AiEntity> types;
        for(const auto& e:list)if(std::none_of(types.begin(),types.end(),[&](const auto& x){return x.classname==e.classname;}))types.push_back(e);
        list=std::move(types);
    };
    unique(ai.enemies);unique(ai.friends);
    ai.status.friendlyFallback=ai.friends.empty() && !ai.enemies.empty();
    if(ai.status.friendlyFallback)ai.friends=ai.enemies;
    if(ai.enemies.empty() && !ai.friends.empty())ai.enemies=ai.friends;
    auto selection=[](const auto& list,const std::string& old){for(std::size_t n=0;n<list.size();++n)if(list[n].classname==old)return static_cast<int>(n);return 0;};
    ai.enemyIndex=selection(ai.enemies,oldEnemy);ai.friendlyIndex=selection(ai.friends,oldFriendly);
    ai.status.enemySpawners=static_cast<int>(ai.enemies.size());ai.status.friendlySpawners=static_cast<int>(ai.friends.size());
    ai.status.enemySpawner=ai.enemies.empty()?0:ai.enemyIndex+1;ai.status.friendlySpawner=ai.friends.empty()?0:ai.friendlyIndex+1;
    ai.status.enemyName={};ai.status.friendlyName={};
    if(!ai.enemies.empty())std::snprintf(ai.status.enemyName.data(),ai.status.enemyName.size(),"%s",ai.enemies[ai.enemyIndex].classname.c_str());
    if(!ai.friends.empty())std::snprintf(ai.status.friendlyName.data(),ai.status.friendlyName.size(),"%s",ai.friends[ai.friendlyIndex].classname.c_str());
    ai.status.enemyDistance=ai.enemyPoints.empty()?0.f:Distance(ai.enemyPoints.front(),player);
    ai.status.friendlyDistance=0.f;
    if(ai.status.lastEntity)ai.status.lastSpawnDistance=std::sqrt(PointDistanceSquared(ai.status.lastPosition,player));
    int nativeActors=0;if(Value(aiProfile.actorCountSlot,nativeActors))ai.status.engineActors=nativeActors;
    const auto limit=ActorLimit();ai.status.engineLimit=limit.valid?limit.current:kAiNativeActorCapacity;
    ai.status.owned=OwnCount();ai.status.ready=true;
}

} // namespace

void InitializeAiEngine(std::uintptr_t base,std::size_t size,std::uintptr_t text,std::size_t textSize,bool supported) {
    std::lock_guard<std::mutex> lock(aiMutex);
    aiProfile.base=base; aiProfile.size=size;
    if(!supported || !InHost(text,textSize)) return;
    const auto think=Unique(text,textSize,"actor think",kAiActorThinkStartPattern);
    const auto dispatch=Unique(text,textSize,"actor dispatch",kAiThinkDispatchPattern);
    const auto array=Unique(text,textSize,"spawner array",kAiSpawnerArrayPattern);
    const auto fields=Unique(text,textSize,"spawner team fields",kAiSpawnerFieldsPattern);
    const auto method=Unique(text,textSize,"normal spawn method",kAiNormalSpawnMethodPattern);
    const auto methodEntry=Unique(text,textSize,"dospawn entry",kAiNormalSpawnEntryPattern);
    const auto spawn=Unique(text,textSize,"native actor spawn",kAiSpawnCorePattern);
    const auto init=Unique(text,textSize,"actor init",kAiActorInitPattern);
    const auto links=Unique(text,textSize,"actor links",kAiActorLinksPattern);
    const auto sentientLinks=Unique(text,textSize,"sentient links",kAiSentientLinksPattern);
    const auto handler=Unique(text,textSize,"actor handler",kAiActorHandlerPattern);
    const auto actorType=Unique(text,textSize,"actor type",kAiActorTypePattern);
    const auto actorSelf=Unique(text,textSize,"actor self link",kAiActorSelfPattern);
    const auto remove=Unique(text,textSize,"delete method",kAiDeleteMethodPattern);
    const auto guard=Unique(text,textSize,"delete protected entity",kAiDeleteGuardPattern);
    const auto serial=Unique(text,textSize,"allocation serial preserve",kAiEntitySerialPattern);
    const auto serialCommit=Unique(text,textSize,"allocation serial increment",kAiEntitySerialCommitPattern);
    const auto strings=Unique(text,textSize,"script string conversion",kAiStringConvertPattern);
    const auto spawnReg=Unique(text,textSize,"g_spawnai registration",kAiSpawnEnabledRegistrationPattern);
    const auto disableReg=Unique(text,textSize,"ai_disableSpawn registration",kAiSpawnDisabledRegistrationPattern);
    const auto copyCalls=Unique(text,textSize,"native spawn copy calls",kAiSpawnCopyCallsPattern);
    const auto copyFields=Unique(text,textSize,"native registered field copier",kAiCopyFieldsStartPattern);
    const auto copyDynamic=Unique(text,textSize,"native script variable copier",kAiCopyDynamicStartPattern);
    const auto trace=Unique(text,textSize,"native Bounds trace",kAiTraceStartPattern);
    const auto traceCall=Unique(text,textSize,"native physics trace caller",kAiTraceScriptCallPattern);
    const auto traceLayout=Unique(text,textSize,"native trace output layout",kAiTraceLayoutPattern);
    const auto position=Unique(text,textSize,"native origin setter",kAiPositionSetterPattern);
    const auto positionCall=Unique(text,textSize,"native origin setter caller",kAiPositionMethodCallPattern);
    const auto team=Unique(text,textSize,"native sentient team transfer",kAiTeamChangePattern);
    const auto teamCall=Unique(text,textSize,"native script team setter",kAiTeamScriptSetterPattern);
    const auto goal=Unique(text,textSize,"native script goal position",kAiGoalPositionPattern);
    const auto goalCall=Unique(text,textSize,"native goal position caller",kAiGoalScriptCallPattern);
    const auto capacity=Unique(text,textSize,"native actor capacity",kAiActorCapacityPattern);
    const auto enemy=Unique(text,textSize,"native clear-enemy caller",kAiClearEnemyTailPattern);
    const auto builder=Unique(text,textSize,"native weapon animation builder",kWeaponAnimBuildStartPattern);
    const auto weapons=Unique(text,textSize,"native weapon pointer tables",kWeaponPointersPattern);
    const auto bounds=Unique(text,textSize,"native actor Bounds/mask",kAiActorBoundsPattern);
    const auto actorTrace=Unique(text,textSize,"native actor trace output",kAiActorTraceResultPattern);
    const auto handsCall=Unique(text,textSize,"native per-hand definition callers",kNativeWeaponHandsCallsPattern);
    const auto enemyStart=Unique(text,textSize,"native set-enemy entry",kAiEnemySetterStartPattern);
    const auto targetname=Unique(text,textSize,"native targetname string setter",kAiSpawnTargetnamePattern);
    if(!think || !dispatch || !array || !fields || !method || !methodEntry || !spawn || !init || !links || !sentientLinks || !handler || !actorType || !actorSelf || !remove || !guard
        || !serial || !serialCommit || !strings || !spawnReg || !disableReg || !copyCalls || !copyFields || !copyDynamic
        || !trace || !traceCall || !traceLayout || !position || !positionCall || !team || !teamCall || !goal || !goalCall
        || !capacity || !enemy || !builder || !weapons || !bounds || !actorTrace || !handsCall || !enemyStart || !targetname) return;
    aiProfile.think=think; aiProfile.thinkSlot=Rip(dispatch,12,3,7)+kActorHandler*80;
    aiProfile.poolSlot=Rip(array,12,3,7); aiProfile.countSlot=Rip(array,0,2,6); aiProfile.activeArray=Rip(array,24,3,7);
    aiProfile.timeSlot=Rip(think,25,2,6); aiProfile.spawn=spawn; aiProfile.remove=remove;
    aiProfile.spawnEnabledSlot=Rip(init,26,3,7); aiProfile.spawnDisabledSlot=Rip(spawn,13,3,7);
    aiProfile.stringPoolSlot=Rip(strings,4,3,7); aiProfile.protectedSlot=Rip(guard,4,2,6);
    aiProfile.fixedPool=Rip(remove,51,3,7);
    aiProfile.allocate=Call(copyCalls,0);aiProfile.copyFields=copyFields;aiProfile.copyDynamic=copyDynamic;
    aiProfile.freeEntity=Jump(guard,47);aiProfile.trace=trace;aiProfile.setOrigin=position;
    aiProfile.setTeam=team;aiProfile.setGoal=goal;aiProfile.setEnemy=Jump(enemy,28);
    aiProfile.actorLimitSlot=Rip(capacity,10,3,7);aiProfile.actorCountSlot=Rip(capacity,17,2,6);
    aiProfile.animationBuilder=builder;aiProfile.completeTable=Rip(weapons,2,3,7);aiProfile.definitionTable=Rip(weapons,16,3,7);
    aiProfile.setString=Call(targetname,9);
    std::uintptr_t original=0;
    AiBounds nativeBounds{};
    const bool boundsMatch=ReadAi(Rip(bounds,0,3,7),&nativeBounds,sizeof(nativeBounds))
        && nativeBounds.midpoint==kAiHumanBounds.midpoint && nativeBounds.halfSize==kAiHumanBounds.halfSize;
    aiProfile.resolved=InHost(aiProfile.thinkSlot,8) && Value(aiProfile.thinkSlot,original) && original==think
        && Call(method,11)==spawn && method>methodEntry && method-methodEntry<256
        && Rip(methodEntry,24,2,6)==aiProfile.timeSlot && Rip(method,31,2,6)==aiProfile.timeSlot
        && Rip(spawnReg,27,3,7)==aiProfile.spawnEnabledSlot
        && Rip(disableReg,36,3,7)==aiProfile.spawnDisabledSlot && Rip(serial,22,3,7)==aiProfile.poolSlot
        && InHost(aiProfile.poolSlot,8) && InHost(aiProfile.countSlot,4) && InHost(aiProfile.activeArray,kAiEntityCapacity)
        && InHost(aiProfile.timeSlot,4) && InHost(aiProfile.stringPoolSlot,8) && InHost(aiProfile.protectedSlot,4)
        && InHost(aiProfile.fixedPool,kAiEntityStride*kAiEntityCapacity) && MethodEntry("delete",remove) && MethodEntry("dospawn",methodEntry)
        && TeamNames() && EntityFields()
        && Call(copyCalls,14)==copyFields && Call(copyCalls,25)==copyDynamic
        && Call(traceCall,38)==trace && Call(positionCall,8)==position && Jump(teamCall,33)==team
        && Call(goalCall,23)==goal && InHost(aiProfile.allocate,1) && InHost(aiProfile.freeEntity,1)
        && InHost(aiProfile.setEnemy,1) && InHost(aiProfile.actorCountSlot,4) && InHost(aiProfile.actorLimitSlot,8)
        && InHost(aiProfile.completeTable,kWeaponCapacity*8) && InHost(aiProfile.definitionTable,kWeaponCapacity*8)
        && boundsMatch && Call(actorTrace,50)==trace && Call(handsCall,7)==builder && Call(handsCall,24)==builder
        && aiProfile.setEnemy==enemyStart && InHost(aiProfile.setString,1);
    ai.status.resolved=aiProfile.resolved;
    Log("AI_PROFILE resolved=%d callback_slot_rva=0x%llx spawn_rva=0x%llx delete_rva=0x%llx pool_slot_rva=0x%llx clock_rva=0x%llx",
        aiProfile.resolved,static_cast<unsigned long long>(aiProfile.thinkSlot-base),static_cast<unsigned long long>(spawn-base),
        static_cast<unsigned long long>(remove-base),static_cast<unsigned long long>(aiProfile.poolSlot-base),static_cast<unsigned long long>(aiProfile.timeSlot-base));
}
void UpdateAiControls(const Snapshot& s,ULONGLONG now) {
    std::lock_guard<std::mutex> lock(aiMutex);
    if(!aiProfile.resolved) return;
    const auto scope=Scope(s); Lifetime(scope); Pause(s);
    if(ai.queue.discardStale(scope)) Finish(ai.queue.reason());
    if(!scope.ready) {
        RestoreCapacity();
        ai.status.ready=false; ai.status.owned=0; ai.status.enemySpawner=0; ai.status.friendlySpawner=0;
        ai.status.enemySpawners=0; ai.status.friendlySpawners=0;
        ai.status.enemyAlive=0; ai.status.friendlyAlive=0; ai.status.otherAlive=0; ai.status.nativeCount=0;
        ai.status.engineActors=0;ai.status.engineLimit=kAiNativeActorCapacity;ai.status.friendlyFallback=false;
        ai.status.enemyName={}; ai.status.friendlyName={}; return;
    }
    if(now>=ai.nextRead) { ai.nextRead=now+500; ReadWorld(s,scope); }
    ai.status.pending=ai.queue.pending();WorkWanted();
}
void RequestAiOperation(AiOperation op,const Snapshot& s) {
    std::lock_guard<std::mutex> lock(aiMutex);
    const auto scope=Scope(s); Lifetime(scope);
    if(!scope.ready || !ai.status.ready || ai.status.nativeCount==0) { Finish(AiRequestResult::NotReady); return; }
    if(ai.queue.pending()) { ai.status.lastResult=AiRequestResult::Busy; return; }
    const auto& list=op==AiOperation::SpawnFriendly?ai.friends:ai.enemies;
    const int selected=op==AiOperation::SpawnFriendly?ai.friendlyIndex:ai.enemyIndex;
    AiIdentity source;
    if(op!=AiOperation::RemoveOwned) {
        if(list.empty()) { Finish(AiRequestResult::NoSpawner); return; }
        source=AiIdentity::capture(list[selected]);
    } else if(!OwnCount()) { Finish(AiRequestResult::IdentityChanged); return; }
    if(!InstallService()) { Finish(AiRequestResult::HookUnavailable); return; }
    if(!ai.queue.enqueue(op,source,scope)) { Finish(ai.queue.reason()); return; }
    ai.status.lastResult=AiRequestResult::Queued; ai.status.pending=true;
    nativePending.store(true,std::memory_order_release);
    Log("AI_REQUEST operation=%d source=%u serial=%u map=%s player_time=%d render_thread=%lu",
        static_cast<int>(op),source.number,source.serial,s.map,scope.commandTime,GetCurrentThreadId());
}
void AdjustAiSpawner(bool friendly,int direction) {
    std::lock_guard<std::mutex> lock(aiMutex);
    const auto& list=friendly?ai.friends:ai.enemies;
    auto& selected=friendly?ai.friendlyIndex:ai.enemyIndex;
    if(list.empty() || !direction) return;
    selected=(selected+static_cast<int>(list.size())+(direction>0?1:-1))%static_cast<int>(list.size());
    ai.nextRead=0;
    Log("AI_SPAWNER_SELECT friendly=%d entity=%u classname=%s",friendly,list[selected].number,list[selected].classname.c_str());
}
void ToggleAiSpawnPause() {
    std::lock_guard<std::mutex> lock(aiMutex);
    if(!ai.status.ready) return;
    ai.pauseWanted=!ai.pauseWanted; ai.nextRead=0;
    Log("AI_SPAWN_PAUSE requested=%d",ai.pauseWanted);
}
AiStatus GetAiStatus() {
    std::lock_guard<std::mutex> lock(aiMutex);
    auto status=ai.status; status.callbacks=nativeCallbacks.load(); status.nativeThread=nativeThread.load(); return status;
}
void LogAiState() {
    const auto s=GetAiStatus();
    Log("AI_STATE resolved=%d ready=%d hook=%d callbacks=%llu native_thread=%lu enemies=%d friendly=%d other=%d actor_count=%d "
        "enemy_spawner=%d/%d friendly_spawner=%d/%d debug_owned=%d pending=%d spawn_paused=%d requested_pause=%d result=%s created=%llu deleted=%llu",
        s.resolved,s.ready,s.hookInstalled,s.callbacks,s.nativeThread,s.enemyAlive,s.friendlyAlive,s.otherAlive,s.nativeCount,
        s.enemySpawner,s.enemySpawners,s.friendlySpawner,s.friendlySpawners,s.owned,s.pending,s.spawnPaused,s.pauseWanted,
        AiResultText(s.lastResult),s.created,s.deleted);
    if(s.enemySpawners) Log("AI_SELECTED_ENEMY name=%s distance_units=%.1f",s.enemyName.data(),s.enemyDistance);
    if(s.friendlySpawners) Log("AI_SELECTED_FRIENDLY name=%s distance_units=%.1f",s.friendlyName.data(),s.friendlyDistance);
    Log("AI_PLACEMENT engine_actors=%d engine_limit=%d friendly_fallback=%d last_entity=%u last_team=%d last_position=%.1f,%.1f,%.1f last_distance=%.1f placement_attempts=%d",
        s.engineActors,s.engineLimit,s.friendlyFallback,s.lastEntity,s.lastTeam,s.lastPosition[0],s.lastPosition[1],s.lastPosition[2],s.lastSpawnDistance,s.placementAttempts);
}
std::uint64_t RequestNativeHandDefinitions(const WeaponFields& weapon,std::uintptr_t names,std::uintptr_t normalTree,
    std::uintptr_t cache,const Snapshot& s) {
    std::lock_guard<std::mutex> lock(aiMutex);
    const auto scope=Scope(s);
    if(!scope.ready || !weapon.valid || !names || !normalTree || !cache || !aiProfile.animationBuilder
        || !ai.status.ready || !ai.status.nativeCount) return 0;
    if(ai.hands.pending) return SameWeapon(ai.hands.weapon,weapon)?ai.hands.ticket:0;
    if(!InstallService())return 0;
    HandBuild h;h.ticket=++ai.nextHandTicket;h.weapon=weapon;h.names=names;h.normal=normalTree;h.cache=cache;
    h.lifetime.client=scope.client;h.lifetime.pool=scope.pool;h.lifetime.commandTime=scope.commandTime;
    std::strcpy(h.lifetime.map.data(),scope.map);h.pending=true;h.result.state=NativeHandBuildState::Queued;
    ai.hands=h;WorkWanted();return h.ticket;
}
NativeHandBuildResult GetNativeHandBuild(std::uint64_t ticket) {
    std::lock_guard<std::mutex> lock(aiMutex);
    return ticket && ticket==ai.hands.ticket?ai.hands.result:NativeHandBuildResult{};
}
} // namespace mod
