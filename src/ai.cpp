#include "runtime.hpp"
#include "script_vm.hpp"
#include <cstdio>
#include <vector>
#include <limits>
#include <cctype>
#include <type_traits>
#include <intrin.h>
#include "native_hooks.hpp"
#include "ai_behavior.hpp"
#include "checked_memory.hpp"
#include "script_text.hpp"
#include "ai_persistence.hpp"
#include "script_parameters.hpp"
#include "friendly_fire.hpp"
#include "combat_feedback.hpp"
#include "zombie_bindings.hpp"
#include "script_call.hpp"
#include "bo1_zombie_rules.hpp"
#include "bo1_assets.hpp"
#include "bo1_models.hpp"
#include <memory>
#include <optional>
#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace mod {
namespace {
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
using InternStringFn=unsigned(*)(const char*);
using ClockFn=void(*)();
using InitFn=int(*)(std::uintptr_t);
using SaveReadFn=void(*)(std::uintptr_t);
using SaveWriteFn=void(*)(bool,std::uintptr_t);
using DamageFn=bool(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,const float*,const float*,int,int,int,int,int,int,int);
using DObjFn=void(*)(std::uintptr_t,int);
struct AiProfile {
    std::uintptr_t base=0;
    std::size_t size=0;
    bool resolved=false;
    std::uintptr_t think=0,thinkSlot=0,poolSlot=0,countSlot=0,activeArray=0,timeSlot=0;
    std::uintptr_t spawnEnabledSlot=0,spawnDisabledSlot=0,stringPoolSlot=0,spawn=0,remove=0,protectedSlot=0,fixedPool=0;
    std::uintptr_t allocate=0,freeEntity=0,copyFields=0,copyDynamic=0,trace=0,setOrigin=0,setTeam=0,setGoal=0,setEnemy=0;
    std::uintptr_t actorCountSlot=0,actorLimitSlot=0,animationBuilder=0,completeTable=0,definitionTable=0;
    std::uintptr_t setString=0,damage=0,updateModel=0;
    std::uintptr_t setEntityHandle=0;bool goalEntityReady=false;
} aiProfile;
using EntityHandleFn=void(*)(std::uintptr_t,std::uintptr_t);
struct OwnedAi {
    AiIdentity identity,spawner;int team=0,lastGoalTime=-1;
    std::uintptr_t target=0;AiPoint goal{};bool hasGoal=false;
    AiAppearance appearance;int appearanceAt=0;bool modelApplied=false;
    std::string sourceName;
    bool tagsHeld=false;
    bool zombie=false;   // serviced by the zombies adapter, not squad following
};
struct RecoveryAi {
    AiIdentity previous;
    std::string sourceName;
    int team=0,health=100;
    AiAppearance appearance;
    bool tagsHeld=false;
    AiPoint position{};
};
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
    std::deque<RecoveryAi> recovery;
    std::vector<AiEntity> enemies,friends,enemyPoints,combatants,dogPoints,allPoints;
    AiPoint playerPosition{};
    bool playerPositionValid=false;
    std::array<AiIdentity,kFriendlyCharacterCount> appearances{};
    int enemyIndex=0,friendlyIndex=0;
    int nextWorldTime=-1,nextRecoveryTime=-1,nextResourceCheck=-1,allyTeam=kAlliesTeam;
    bool reuseHelpers=true;
    std::size_t followCursor=0;
    bool detailed=false,adoptPending=true;
    std::vector<std::uint8_t> worldPool,worldActive;
    struct Names {bool valid=false;std::uint32_t serial=0;std::uint16_t classname=0,targetname=0;std::string classText,targetText;};
    std::array<Names,kAiEntityCapacity> names{};
    double worldTotalUs=0,serviceTotalUs=0;
    bool pauseWanted=false,scopeActive=false;
    bool cleanupDeferred=false;
    std::atomic<std::uint64_t> checkpointEpoch{0};
    ScalarOwnership<std::uint8_t> pause;
    ScalarOwnership<int> capacity;
    HandBuild hands;
    std::vector<HandBuild> handCache;
    std::uint64_t nextHandTicket=0;
    std::uintptr_t client=0,pool=0;
    int commandTime=0,lastServiceTime=-1;
    char map[96]{};
} ai;
std::recursive_mutex aiMutex;
ClockFn originalClock=nullptr;
InitFn originalInit=nullptr;
SaveReadFn originalSaveRead=nullptr;
SaveWriteFn originalSaveWrite=nullptr;
DamageFn originalDamage=nullptr;
std::atomic<bool> allowFriendlyFire{false},friendlyFireHookReady{false};
std::atomic<bool> hitmarkersEnabled{false},invincibleFriendlies{false};
std::atomic<ULONGLONG> lastPlayerHit{0};
std::atomic<unsigned long long> confirmedPlayerHits{0},protectedFriendlyHits{0};
std::atomic<bool> nativePending{false};
std::atomic<unsigned long long> nativeCallbacks{0};
std::atomic<DWORD> nativeThread{0};
thread_local bool servicing=false;
thread_local bool spawningOwned=false;
thread_local bool cleaningOwned=false;
thread_local unsigned checkpointReadDepth=0;
thread_local int traceIgnoreEntity=-1;

bool ZombieDamage(std::uintptr_t,std::uintptr_t,std::uintptr_t,const float*,const float*,int,int,int,int,int,int,int,bool&);
bool DamageHook(std::uintptr_t target,std::uintptr_t inflictor,std::uintptr_t attacker,const float* direction,const float* point,
    int damage,int flags,int mod,int weapon,int hitloc,int time,int surface) {
    {
        // Test38 zombies: player damage to our zombies scores BO1 points.
        std::lock_guard<std::recursive_mutex> lock(aiMutex);
        bool handled=false;
        const bool result=ZombieDamage(target,inflictor,attacker,direction,point,damage,flags,mod,weapon,hitloc,time,surface,handled);
        if(handled) {
            if(hitmarkersEnabled.load(std::memory_order_relaxed) && result) {lastPlayerHit.store(GetTickCount64(),std::memory_order_release);++confirmedPlayerHits;}
            return result;
        }
    }
    const bool feedback=hitmarkersEnabled.load(std::memory_order_relaxed);
    const bool protect=invincibleFriendlies.load(std::memory_order_relaxed);
    const bool friendlyFire=allowFriendlyFire.load(std::memory_order_relaxed);
    AiEntity before;
    bool player=false;
    if((feedback || protect || friendlyFire) && target) {
        const auto pool=aiProfile.fixedPool;
        const auto inPool=[&](std::uintptr_t at) {return at>=pool && (at-pool)%kAiEntityStride==0 && (at-pool)/kAiEntityStride<kAiEntityCapacity;};
        if(inPool(target)) {
            before=ReadAiEntity(target,static_cast<int>((target-pool)/kAiEntityStride),[](auto at,auto out,auto size){return CheckedReadMemory(at,out,size);});
        }
        if(before.valid && protect) {
            std::lock_guard<std::recursive_mutex> lock(aiMutex);
            for(const auto& owned:ai.owned)if(ProtectSpawnedFriendly(true,owned.identity,before,ai.allyTeam)) {
                ++protectedFriendlyHits;
                return false;
            }
        }
        if(before.valid && attacker==pool && target!=attacker) {
            std::uintptr_t client=0,sentient=0,self=0;int team=0;std::uint8_t active=0;
            player=CheckedReadMemory(attacker+kAiInUse,&active,1) && active==1
                && CheckedReadMemory(attacker+kAiClient,&client,8) && client
                && CheckedReadMemory(attacker+kAiSentient,&sentient,8) && sentient
                && CheckedReadMemory(sentient,&self,8) && self==attacker
                && CheckedReadMemory(sentient+8,&team,4);
            if(BypassFriendlyFirePenalty(friendlyFire,before,player,team)) {
                // The native damage dispatcher substitutes the world entity
                // for null attribution. Campaign damage/death callbacks then
                // cannot count the hit as a player attack on an ally. Damage,
                // hit location, weapon and native actor reactions still run.
                inflictor=0;attacker=0;
                RecordNativeOperation("FRIENDLY_DAMAGE_NEUTRAL",before.number);
            }
        }
    }
    const auto result=originalDamage(target,inflictor,attacker,direction,point,damage,flags,mod,weapon,hitloc,time,surface);
    if(feedback && player && result) {
        std::uint32_t serial=0;int health=0;
        if(CheckedReadMemory(target+kAiUseCount,&serial,4) && CheckedReadMemory(target+kAiHealth,&health,4)
            && ConfirmedPlayerActorHit(true,damage,before,serial,health)) {
            lastPlayerHit.store(GetTickCount64(),std::memory_order_release);
            ++confirmedPlayerHits;
        }
    }
    return result;
}

bool ReadAi(std::uintptr_t at,void* out,std::size_t length) {
    return CheckedReadMemory(at,out,length);
}
template<class T> bool Value(std::uintptr_t at,T& out) { return ReadAi(at,&out,sizeof(out)); }
bool InHost(std::uintptr_t at,std::size_t size) {
    return at>=aiProfile.base && size<=aiProfile.size && at-aiProfile.base<=aiProfile.size-size;
}
bool CString(std::uintptr_t at,char* out,std::size_t size) {
    if(!at || size<2)return false;
    auto available=HostReadableBytes(at);
    if(!available) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info)) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        available=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize-at;
    }
    const auto count=std::min(size,static_cast<std::size_t>(available));
    if(!ReadAi(at,out,count)) return false;
    for(std::size_t n=0;n<count;++n) {
        if(!out[n]) return true;
        if(static_cast<unsigned char>(out[n])<32 || static_cast<unsigned char>(out[n])>126) return false;
    }
    return false;
}
bool ScriptText(std::uint16_t id,std::string& out) {
    if(!id)return false;
    std::uint16_t emptyTag=0;
    if(Value(aiProfile.base+kAiEmptyAttachmentTagRva,emptyTag) && id==emptyTag) {out.clear();return true;}
    std::uintptr_t pool=0; char text[128]{};
    if(!id || !Value(aiProfile.stringPoolSlot,pool) || !pool || !CString(pool+static_cast<std::uintptr_t>(id)*16+4,text,sizeof(text)))return false;
    return DecodeScriptText(text,sizeof(text),out);
}
std::string ClassName(std::uint16_t id) {std::string out;ScriptText(id,out);return out;}
bool MarkOwned(std::uintptr_t entity,const std::string& marker) {
    if(!AssignOwnedAiMarker(entity+kAiTargetname,marker,
        reinterpret_cast<InternStringFn>(aiProfile.base+kAiInternStringRva),
        reinterpret_cast<ScriptStringFn>(aiProfile.setString)))return false;
    std::uint16_t current=0;std::string text;
    if(!Value(entity+kAiTargetname,current) || !ScriptText(current,text) || text!=marker)return false;
    std::uintptr_t pool=0;std::uint32_t header=0;
    if(Value(aiProfile.stringPoolSlot,pool) && pool && Value(pool+static_cast<std::uintptr_t>(current)*16,header))
        Log("AI_MARKER entity=%u id=%u references=%u engine_user1=%u temporary_reference=NONE",
            static_cast<unsigned>((entity-aiProfile.fixedPool)/kAiEntityStride),current,header&0xffffu,(header>>16)&1u);
    return true;
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
bool PersistenceFields() {
    std::array<std::uint8_t,10> intern{};
    std::uint64_t offset=0;std::uint32_t kind=0;
    return ReadAi(aiProfile.base+kAiInternStringRva,intern.data(),intern.size())
        && intern[0]==0xba && intern[1]==1 && intern[2]==0 && intern[3]==0 && intern[4]==0
        && Jump(aiProfile.base+kAiInternStringRva,5)==aiProfile.base+0x21ac70
        && Value(aiProfile.base+kAiEntitySaveFieldsRva+11*16,offset) && offset==kAiTargetname
        && Value(aiProfile.base+kAiEntitySaveFieldsRva+11*16+8,kind) && kind==1;
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
bool SetVector(std::uintptr_t at,const AiPoint& point) {
    if(!FinitePoint(point))return false;
    for(int n=0;n<3;++n) {
        int before=0,after=0;std::memcpy(&after,&point[n],4);
        if(!Value(at+n*4,before) || !SetCount(at+n*4,before,after))return false;
    }
    return true;
}
bool SetWord(std::uintptr_t at,std::uint16_t before,std::uint16_t after) {
    return before==after || (!(at&1) && Writable(at) && static_cast<std::uint16_t>(InterlockedCompareExchange16(
        reinterpret_cast<volatile SHORT*>(at),static_cast<SHORT>(after),static_cast<SHORT>(before)))==before);
}
struct CodeEdit {std::uintptr_t at=0;alignas(16) std::array<std::uint8_t,16> before{},after{};};
std::vector<CodeEdit> serviceEdits;
std::vector<void*> serviceMemory;
bool clockInstalled=false;
void Service();
bool CompleteServiceInstall();
void ResetZombieGame(const char* reason,bool deleteActors);
unsigned CleanupOwned(const char* reason,bool one=false,bool onlyZombies=false);
void CaptureRecovery(const AiIdentity* only=nullptr);
void ClearRecovery(bool releaseTags);
void HoldAppearance(AiAppearance& appearance);
void ReleaseAppearance(AiAppearance& appearance);
void ClockHook() {
    originalClock();++nativeCallbacks;
    DWORD expected=0;nativeThread.compare_exchange_strong(expected,GetCurrentThreadId());
    try {Service();} catch(...) {
        std::lock_guard<std::recursive_mutex> lock(aiMutex);
        ai.queue.cancel(AiRequestResult::NativeReturnedInvalid);ai.hands.pending=false;ai.hands.result.state=NativeHandBuildState::Failed;
        nativePending=!ai.owned.empty();Log("AI_SERVICE C++ failure on server clock; queued work canceled");
    }
}
void BeforeLoad(const char* reason) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    ResetZombieGame(reason,false);
    if(servicing || cleaningOwned) {
        // Native read replaces the world. Keep active iteration tickets stable
        // until its caller returns, but release DLL-held refs before VM reset.
        // The following clock discards metadata; it must not delete restored
        // entities which may have exactly the old address/useCount again.
        for(auto& owned:ai.owned)if(owned.tagsHeld) {ReleaseAppearance(owned.appearance);owned.tagsHeld=false;}
        ClearRecovery(true);ai.queue.cancel(AiRequestResult::LifetimeChanged);ai.hands.pending=false;
        ai.cleanupDeferred=true;nativePending=true;
        Log("AI_CLEANUP deferred=1 metadata_only=1 reason=%s",reason);return;
    }
    ai.queue.cancel(AiRequestResult::LifetimeChanged);
    ai.hands.pending=false;ai.handCache.clear();
    ClearRecovery(true);
    const auto count=CleanupOwned(reason);
    // Even a protected/refused actor is replaced by native load. Detach the
    // tracking data and release extra references before that VM replacement.
    for(auto& owned:ai.owned)if(owned.tagsHeld)ReleaseAppearance(owned.appearance);
    ai.owned.clear();ai.capacity.clear();ai.status.owned=0;
    ai.nextRecoveryTime=-1;
    if(count) {ai.status.lastResult=AiRequestResult::Deleted;ai.nextWorldTime=0;}
}
void SaveReadHook(std::uintptr_t save) {
    RecordNativeOperation("CHECKPOINT_READ_BEGIN");
    CheckStockCheckpoint(save);
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);++ai.checkpointEpoch;}
    BeforeLoad("CHECKPOINT_READ");
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);ai.names={};ai.nextWorldTime=0;ai.adoptPending=true;nativePending=true;}
    struct ReadScope {ReadScope(){++checkpointReadDepth;}~ReadScope(){--checkpointReadDepth;}} readScope;
    originalSaveRead(save);
    RecordNativeOperation("CHECKPOINT_READ_END");
}
void SaveWriteHook(bool flag,std::uintptr_t save) {
    RecordNativeOperation("CHECKPOINT_WRITE_BEGIN");
    // Player feature code takes its lock before the AI lock. Keep that order;
    // a save nested inside an active native AI call defers its AI cleanup.
    const bool suspend=!servicing && !cleaningOwned;
    if(suspend)SuspendRecoilForCheckpoint(true);
    struct Resume {bool active;~Resume(){if(active)SuspendRecoilForCheckpoint(false);}} resume{suspend};
    // Actors created through a real human spawner have native actor, sentient,
    // animation and VM state. Let the unchanged native save writer persist them.
    // Registered targetname strings recover ownership after load or relaunch.
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);
        Log("AI_CHECKPOINT_WRITE retained=%llu recovery_pending=%llu mode=NATIVE_SAVE",static_cast<unsigned long long>(ai.owned.size()),static_cast<unsigned long long>(ai.recovery.size()));}
    originalSaveWrite(flag,save);
    RecordNativeOperation("CHECKPOINT_WRITE_END");
}
int ActorInitHook(std::uintptr_t entity) {
    const auto epoch=ai.checkpointEpoch.load();
    if(!spawningOwned && !checkpointReadDepth) {
        std::lock_guard<std::recursive_mutex> lock(aiMutex);
        int count=0,limit=NativeActorCapacity();std::uintptr_t dvar=0;
        if(Value(aiProfile.actorLimitSlot,dvar) && dvar)Value(dvar+0x10,limit);
        if((!ai.owned.empty() || !ai.recovery.empty()) && limit>0 && limit<NativeActorCapacity()
            && SetCount(dvar+0x10,limit,NativeActorCapacity())) {
            ai.capacity.applied(dvar,limit,NativeActorCapacity());limit=NativeActorCapacity();
        }
        while(limit>0 && limit<=NativeActorCapacity() && Value(aiProfile.actorCountSlot,count) && count>=limit && !ai.owned.empty()) {
            RecordNativeOperation("CAMPAIGN_RECLAIM_BEGIN",0,count);
            const auto removed=CleanupOwned("CAMPAIGN_NEEDS_SLOT",true);
            if(epoch!=ai.checkpointEpoch)return 0;
            ai.status.reclaimed+=removed;
            if(!removed)break;
            RecordNativeOperation("CAMPAIGN_RECLAIM_END",0,count-static_cast<int>(removed));
        }
    }
    return originalInit(entity);
}
bool ExchangeCode(CodeEdit& edit,bool install) {
    if(edit.at&15)return false;
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(edit.at),16,PAGE_EXECUTE_READWRITE,&old))return false;
    alignas(16) LONG64 expected[2]{};LONG64 desired[2]{};
    const auto& from=install?edit.before:edit.after;const auto& to=install?edit.after:edit.before;
    std::memcpy(expected,from.data(),16);std::memcpy(desired,to.data(),16);
#if defined(_MSC_VER)
    const bool changed=_InterlockedCompareExchange128(reinterpret_cast<volatile LONG64*>(edit.at),desired[1],desired[0],expected)!=0;
#else
    struct alignas(16) Pair {LONG64 low,high;};
    unsigned char exchanged=0;
    __asm__ __volatile__("lock cmpxchg16b %1; setz %0"
        : "=q"(exchanged), "+m"(*reinterpret_cast<volatile Pair*>(edit.at)), "+a"(expected[0]), "+d"(expected[1])
        : "b"(desired[0]), "c"(desired[1]) : "memory", "cc");
    const bool changed=exchanged!=0;
#endif
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(edit.at),16);
    DWORD ignored=0;const bool protectedAgain=VirtualProtect(reinterpret_cast<void*>(edit.at),16,old,&ignored)!=0;
    if(!protectedAgain)Log("AI_HOOK page protection restore failed");
    return changed;
}
void* Relay(std::uintptr_t at,std::uintptr_t target) {
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto step=static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto center=at&~(step-1);
    for(std::uintptr_t offset=step;offset<0x70000000;offset+=step) {
        for(int sign:{1,-1}) {
            if(sign<0 && offset>=center)continue;
            const auto candidate=sign>0?center+offset:center-offset;
            void* p=VirtualAlloc(reinterpret_cast<void*>(candidate),step,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p)continue;
            const auto jump=AbsoluteJump(target);std::memcpy(p,jump.data(),jump.size());
            DWORD old=0;if(!VirtualProtect(p,step,PAGE_EXECUTE_READ,&old)) {VirtualFree(p,0,MEM_RELEASE);continue;}
            FlushInstructionCache(GetCurrentProcess(),p,jump.size());serviceMemory.push_back(p);return p;
        }
    }
    return nullptr;
}
bool PlanCall(std::uintptr_t rva,std::uintptr_t targetRva,std::uintptr_t replacement) {
    const auto call=aiProfile.base+rva;
    if(!InHost(call,5) || Call(call,0)!=aiProfile.base+targetRva)return false;
    auto* relay=Relay(call,replacement);if(!relay)return false;
    std::array<std::uint8_t,5> code{};if(!RelativeCall(call,reinterpret_cast<std::uintptr_t>(relay),code))return false;
    CodeEdit edit;edit.at=call&~std::uintptr_t(15);
    if(call-edit.at+code.size()>16 || !ReadAi(edit.at,edit.before.data(),16))return false;
    edit.after=edit.before;std::memcpy(edit.after.data()+call-edit.at,code.data(),code.size());serviceEdits.push_back(edit);return true;
}
bool PlanEntry(std::uintptr_t rva,const std::array<std::uint8_t,15>& expected,std::uintptr_t replacement,void*& original) {
    const auto at=aiProfile.base+rva;
    std::array<std::uint8_t,15> observed{};
    if(!ReadAi(at,observed.data(),observed.size()) || observed!=expected || (at&15))return false;
    auto* trampoline=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!trampoline)return false;
    std::memcpy(trampoline,observed.data(),observed.size());
    const auto tail=AbsoluteJump(at+observed.size());std::memcpy(trampoline+observed.size(),tail.data(),tail.size());
    DWORD old=0;if(!VirtualProtect(trampoline,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,29);serviceMemory.push_back(trampoline);
    original=trampoline;
    CodeEdit edit;edit.at=at;if(!ReadAi(at,edit.before.data(),16))return false;edit.after=edit.before;
    const auto jump=AbsoluteJump(replacement);
    std::memcpy(edit.after.data(),jump.data(),jump.size());edit.after[14]=0x90;serviceEdits.push_back(edit);return true;
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
        && s.supported && s.serverRunning && (s.state==LevelState::Ready || s.state==LevelState::PlayerDead) && s.client.valid;
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
bool ZombieWorkWanted();
void WorkWanted() { ai.status.recovering=static_cast<int>(ai.recovery.size());nativePending=ai.queue.pending() || ai.hands.pending || !ai.owned.empty() || !ai.recovery.empty() || ai.cleanupDeferred || ai.adoptPending || ai.detailed || ai.pauseWanted || ai.pause.owned || ZombieWorkWanted(); }
void RestoreCapacity();
void Lifetime(const AiScope& s) {
    const bool changed=!s.ready || (ai.scopeActive && (ai.client!=s.client || ai.pool!=s.pool || std::strcmp(ai.map,s.map)
        || s.commandTime<ai.commandTime));
    if(changed) {
        RestoreCapacity();
        ClearRecovery(false);
        if(ai.queue.discardStale(s)) ai.status.lastResult=AiRequestResult::LifetimeChanged;
        nativePending=ai.queue.pending(); ai.status.pending=ai.queue.pending(); ai.owned={}; ai.scopeActive=false;ai.cleanupDeferred=false;
        ai.enemies.clear(); ai.friends.clear(); ai.enemyPoints.clear();ai.lastServiceTime=-1;
        ai.status.ready=false;ai.status.owned=0;ai.status.enemySpawners=0;ai.status.friendlySpawners=0;ai.status.enemyName={};ai.status.friendlyName={};
        ai.nextWorldTime=-1;ai.nextRecoveryTime=-1;ai.nextResourceCheck=-1;ai.reuseHelpers=true;ai.followCursor=0;
        ai.names={};
        ai.adoptPending=true;
        ai.status.lastEntity=0;ai.status.lastPosition={};ai.status.lastName={};ai.status.placementAttempts=0;ai.status.lastSpawnDistance=0.f;
        ai.hands.pending=false; ai.hands.result.state=NativeHandBuildState::Canceled; ai.handCache.clear();
    }
    if(s.ready) {
        ai.scopeActive=true; ai.client=s.client; ai.pool=s.pool; ai.commandTime=s.commandTime;
        std::strncpy(ai.map,s.map,sizeof(ai.map)-1); ai.map[sizeof(ai.map)-1]='\0';
    }
}
void Finish(AiRequestResult result) {
    if(ai.owned.empty() && ai.recovery.empty())RestoreCapacity();
    ai.status.lastResult=result; ai.status.pending=ai.queue.pending();
    WorkWanted(); ai.nextWorldTime=0;
    Log("AI_RESULT result=%s native_thread=%lu owned=%d",AiResultText(result),GetCurrentThreadId(),OwnCount());
}
struct IntDvarRead { bool valid=false; std::uintptr_t address=0; int current=0; };
IntDvarRead ActorLimit() {
    IntDvarRead d; std::uintptr_t name=0; std::uint8_t type=255; char text[32]{};
    d.valid=Value(aiProfile.actorLimitSlot,d.address) && d.address && Value(d.address,name)
        && Value(d.address+0xc,type) && type==5 && CString(name,text,sizeof(text)) && !std::strcmp(text,"ai_count")
        && Value(d.address+0x10,d.current) && d.current>=0 && d.current<=NativeActorCapacity();
    return d;
}
void RestoreCapacity() {
    if(!ai.capacity.owned) return;
    const auto d=ActorLimit();
    if(!d.valid) return;
    if(!ai.capacity.canRestore(d.address,d.current)) ai.capacity.clear();
    else if(int count=0; Value(aiProfile.actorCountSlot,count) && count<=ai.capacity.original && SetCount(d.address+0x10,d.current,ai.capacity.original)) {
        Log("AI_CAPACITY restored=%d",ai.capacity.original); ai.capacity.clear();
    }
}
// Free a helper spawner only when no other ownership ticket still uses it.
void ReleaseHelper(const OwnedAi& owner) {
    const auto users=std::count_if(ai.owned.begin(),ai.owned.end(),[&](const OwnedAi& x){
        return x.spawner.address==owner.spawner.address && x.spawner.serial==owner.spawner.serial;});
    if(users>1)return;
    const auto helper=ReadAiAllocation(owner.spawner,ReadAi);
    if(SameAiAllocation(owner.spawner,helper) && helper.type==kActorSpawnerType)
        reinterpret_cast<EntityFn>(aiProfile.freeEntity)(owner.spawner.address);
}
unsigned CleanupOwned(const char* reason,bool one,bool onlyZombies) {
    if(cleaningOwned)return 0;
    NativeScriptParameters parameters{};
    const auto parameterAddress=aiProfile.base+kScriptParametersRva;
    if(!ReadAi(parameterAddress,&parameters,sizeof(parameters)))return 0;
    NativeScriptParameterFrame frame(*reinterpret_cast<volatile NativeScriptParameters*>(parameterAddress));
    if(!frame.valid()) {Log("AI_DELETE refused=SCRIPT_PARAMETER_FRAME_INVALID reason=%s",reason);return 0;}
    const auto parameterEpoch=ai.checkpointEpoch.load();
    struct RestoreParameters {
        NativeScriptParameterFrame& frame;std::uint64_t epoch;
        ~RestoreParameters() {
            if(epoch!=ai.checkpointEpoch)frame.abandon();
            else if(!frame.close()) {
                RecordNativeOperation("SCRIPT_PARAMETER_FRAME_UNBALANCED");
                Log("AI_DELETE parameter frame changed unexpectedly; caller counters were not restored");
            }
        }
    } restoreParameters{frame,parameterEpoch};
    if(parameters.incoming || parameters.outgoing) {
        RecordNativeOperation("SCRIPT_PARAMETER_FRAME_ISOLATED");
        Log("AI_DELETE isolated_caller_params incoming=%u outgoing=%u reason=%s",parameters.incoming,parameters.outgoing,reason);
    }
    cleaningOwned=true;
    struct ClearCleanupFlag {~ClearCleanupFlag(){cleaningOwned=false;}} cleanupFlag;
    unsigned removed=0;
    const auto epoch=ai.checkpointEpoch.load();
    if(one) {
        // Reserve campaign slots by retiring corpses/hostiles before allies.
        auto priority=[&](const OwnedAi& x) {
            const auto raw=ReadAiAllocation(x.identity,ReadAi);
            return raw.type==kActorCorpseType?0:x.team!=ai.allyTeam?1:2;
        };
        std::stable_sort(ai.owned.begin(),ai.owned.end(),[&](const auto& a,const auto& b){return priority(a)<priority(b);});
    }
    for(auto it=ai.owned.begin();it!=ai.owned.end();) {
        if(onlyZombies && !it->zombie) {++it;continue;}
        const auto raw=ReadAiAllocation(it->identity,ReadAi);
        if(!raw.valid) {++it;continue;}
        const bool ours=SameAiAllocation(it->identity,raw);
        if(ours && (raw.type==kActorType || raw.type==kActorCorpseType) && raw.number>1) {
            int protectedNumber=-1;
            if(!Value(aiProfile.protectedSlot,protectedNumber) || protectedNumber==raw.number) {++it;continue;}
            Log("AI_DELETE_BEGIN entity=%u serial=%u type=%u reason=%s",raw.number,raw.serial,raw.type,reason);
            RecordNativeOperation("DELETE_BEGIN",raw.number);
            if(one && it->team==ai.allyTeam)CaptureRecovery(&it->identity);
            // Native delete sends notifications and performs brain/sentient/VM cleanup.
            reinterpret_cast<DeleteFn>(aiProfile.remove)(raw.number);
            RecordNativeOperation("DELETE_END",raw.number);
            if(epoch!=ai.checkpointEpoch)return removed;
            const auto after=ReadAiAllocation(it->identity,ReadAi);
            if(SameAiAllocation(it->identity,after)) {Log("AI_DELETE refused entity=%u",raw.number);++it;continue;}
            ++removed;++ai.status.deleted;
        } else if(ours) {++it;continue;}
        ReleaseHelper(*it);
        if(epoch!=ai.checkpointEpoch)return removed;
        if(it->tagsHeld)ReleaseAppearance(it->appearance);
        it=ai.owned.erase(it);
        if(one && removed)break;
    }
    if(ai.owned.empty() && ai.recovery.empty())RestoreCapacity();
    ai.status.owned=OwnCount();WorkWanted();
    Log("AI_CLEANUP reason=%s removed=%u remaining=%d",reason,removed,OwnCount());return removed;
}
bool ReadAppearance(const AiEntity& e,AiAppearance& out) {
    out={};
    if(!e.valid || !Value(e.address+kAiModelIndex,out.model) || !out.model
        || !ReadAi(e.address+kAiAttachmentModels,out.models.data(),sizeof(out.models))
        || !ReadAi(e.address+kAiAttachmentTags,out.tags.data(),sizeof(out.tags))
        || !Value(e.address+kAiAttachmentFlags,out.flags)) {
        Log("AI_MODEL_CAPTURE entity=%u result=MODEL_READ_FAILED",e.number);return false;
    }
    TrimAiAttachments(out);
    for(std::size_t n=0;n<kAiAttachmentCount;++n)if(out.tags[n]) {
        if(!ScriptText(out.tags[n],out.tagNames[n])) {
            Log("AI_MODEL_CAPTURE entity=%u slot=%u model=%u tag=%u result=TAG_NOT_LOADED",e.number,static_cast<unsigned>(n),out.models[n],out.tags[n]);return false;
        }
    }
    Log("AI_MODEL_CAPTURE entity=%u result=READY model=%u first_attachment=%u first_tag=%u first_tag_name='%s'",e.number,out.model,out.models[0],out.tags[0],out.tagNames[0].c_str());
    return true;
}
void HoldAppearance(AiAppearance& appearance) {
    for(auto& tag:appearance.tags)if(tag) {
        const auto value=tag;tag=0;
        reinterpret_cast<ScriptStringFn>(aiProfile.setString)(reinterpret_cast<std::uintptr_t>(&tag),value);
    }
}
void ReleaseAppearance(AiAppearance& appearance) {
    for(auto& tag:appearance.tags)if(tag)
        reinterpret_cast<ScriptStringFn>(aiProfile.setString)(reinterpret_cast<std::uintptr_t>(&tag),0);
}
void ReleaseRecovery(RecoveryAi& r) {
    if(r.tagsHeld)ReleaseAppearance(r.appearance);
    r.tagsHeld=false;
}
void ClearRecovery(bool releaseTags) {
    if(releaseTags)for(auto& r:ai.recovery)ReleaseRecovery(r);
    ai.recovery.clear();ai.status.recovering=0;
}
void CaptureRecovery(const AiIdentity* only) {
    for(const auto& owned:ai.owned) {
        if(only && (owned.identity.address!=only->address || owned.identity.serial!=only->serial))continue;
        if(ai.recovery.size()>=static_cast<std::size_t>(NativeActorCapacity()))break;
        if(std::any_of(ai.recovery.begin(),ai.recovery.end(),[&](const auto& r){return r.previous.address==owned.identity.address && r.previous.serial==owned.identity.serial;}))continue;
        const auto e=Entity(owned.identity,ai.pool,false);
        if(!owned.identity.matches(e) || e.health<=0)continue;
        RecoveryAi r;r.previous=owned.identity;r.sourceName=owned.sourceName;r.team=owned.team;r.health=e.health;r.position=e.origin;
        if(owned.appearance.model) {
            r.appearance=owned.appearance;
            if(owned.modelApplied && !ReadAppearance(e,r.appearance))continue;
            bool valid=true;
            for(std::size_t n=0;n<kAiAttachmentCount;++n)if(r.appearance.tags[n]) {std::string current;if(!ScriptText(r.appearance.tags[n],current) || current!=r.appearance.tagNames[n])valid=false;}
            if(!valid)continue;
            HoldAppearance(r.appearance);
            r.tagsHeld=true;
        }
        ai.recovery.push_back(std::move(r));
    }
    std::stable_partition(ai.recovery.begin(),ai.recovery.end(),[](const auto& r){return r.team==ai.allyTeam;});
    ai.status.recovering=static_cast<int>(ai.recovery.size());
}
AiTraceResult Trace(const AiPoint& start,const AiPoint& end,const AiBounds& bounds) {
    alignas(16) std::array<std::uint8_t,48> output{};
    if(!FinitePoint(start) || !FinitePoint(end)) return {};
    reinterpret_cast<TraceFn>(aiProfile.trace)(output.data(),start.data(),end.data(),&bounds,0x7ff,kAiPlacementMask);
    auto result=DecodeAiTrace(output);
    if(result.valid && start==end && bounds.halfSize==kAiHumanBounds.halfSize) {
        if(ai.playerPositionValid && traceIgnoreEntity!=0 && BodiesOverlap(start,ai.playerPosition)) {result.startSolid=true;result.fraction=0.f;}
        for(const auto& e:ai.combatants) {
            if(e.number!=traceIgnoreEntity && e.health>0 && BodiesOverlap(start,e.origin)) {result.startSolid=true;result.fraction=0.f;break;}
        }
    }
    return result;
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
    const auto epoch=ai.checkpointEpoch.load();
    const auto right=build(h.weapon.definition,h.names);
    if(epoch!=ai.checkpointEpoch) {h.result.state=NativeHandBuildState::Canceled;return;}
    const auto left=build(h.weapon.definition,h.names);
    if(epoch!=ai.checkpointEpoch) {h.result.state=NativeHandBuildState::Canceled;return;}
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
void ReadWorld(const Snapshot&,const AiScope&);
template<class T> bool SetQword(std::uintptr_t at,T before,T after) {
    static_assert(sizeof(T)==8);
    if(before==after)return true;
    if((at&7) || !Writable(at))return false;
    LONG64 b=0,a=0;std::memcpy(&b,&before,8);std::memcpy(&a,&after,8);
    return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(at),a,b)==b;
}
bool SetFloat(std::uintptr_t at,float before,float after) {
    int b=0,a=0;std::memcpy(&b,&before,4);std::memcpy(&a,&after,4);return SetCount(at,b,a);
}
// Native setgoalentity without its script parameter reads: clear claimed-node
// and arrival requests, set the goal entity handle through the native handle
// record setter, then clear the goal node and goal volume. The engine copies
// the entity origin into the effective goal each think (0x12b89c).
bool AssignGoalEntity(const AiEntity& e,std::uintptr_t target) {
    if(!aiProfile.goalEntityReady || !e.actor || !target)return false;
    for(std::size_t n=0;n<2;++n) {std::uint8_t keep=0;if(Value(e.actor+kActorKeepClaimedNode+n,keep) && keep<=1)SetByte(e.actor+kActorKeepClaimedNode+n,keep,0);}
    for(std::size_t n=0;n<2;++n) {int arrival=0;if(Value(e.actor+kActorRequestArrival+n*4,arrival))SetCount(e.actor+kActorRequestArrival+n*4,arrival,0);}
    reinterpret_cast<EntityHandleFn>(aiProfile.setEntityHandle)(e.actor+kActorGoalEntity,target);
    std::uintptr_t node=0,volume=0;
    if(Value(e.actor+kActorGoalNode,node))SetQword(e.actor+kActorGoalNode,node,std::uintptr_t{0});
    if(Value(e.actor+kActorGoalVolume,volume))SetQword(e.actor+kActorGoalVolume,volume,std::uintptr_t{0});
    return true;
}
std::uint16_t EntityHandleValue(std::uintptr_t entity,std::uintptr_t pool) {
    return entity>=pool && !((entity-pool)%kAiEntityStride)?static_cast<std::uint16_t>((entity-pool)/kAiEntityStride+1):0;
}
bool ClearFixedNode(const AiEntity& e) {
    std::uint8_t fixed=0;
    if(!Value(e.actor+kActorFixedNode,fixed) || fixed!=1 || !SetByte(e.actor+kActorFixedNode,fixed,0))return false;
    int check=0;if(Value(e.actor+kActorFixedNodeCheck,check))SetCount(e.actor+kActorFixedNodeCheck,check,static_cast<int>(0x80000000u));
    return true;
}
void FollowOwned(const Snapshot& s,const AiScope& scope,int time) {
    AiPoint player{};
    if(!scope.ready || !ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player))return;
    const int ally=PlayerTeam(s);
    const auto epoch=ai.checkpointEpoch.load();
    LARGE_INTEGER batchStart{},frequency{},now{};QueryPerformanceCounter(&batchStart);QueryPerformanceFrequency(&frequency);
    std::size_t followers=0;
    for(const auto& x:ai.owned)if(x.team==ally && !x.zombie)++followers;
    // Native notifications may re-enter checkpoint cleanup. Iterate a snapshot,
    // then commit only to a still-owned allocation instead of keeping iterators
    // across engine calls that can erase ownership records.
    std::vector<OwnedAi> ownedSnapshot;
    const auto count=std::min<std::size_t>(kFollowMaximumPerTick,ai.owned.size());
    ownedSnapshot.reserve(count);
    for(std::size_t n=0;n<count;++n)ownedSnapshot.push_back(ai.owned[(ai.followCursor+n)%ai.owned.size()]);
    std::size_t done=0;
    const auto cursorBase=ai.followCursor;
    for(const auto& original:ownedSnapshot) {
        QueryPerformanceCounter(&now);
        if(!ContinueFollowBatch(done,ownedSnapshot.size(),(now.QuadPart-batchStart.QuadPart)*1000000.0/frequency.QuadPart))break;
        ++done;
        auto update=original;
        auto* it=&update;
        auto tracked=[&]() {return epoch==ai.checkpointEpoch && std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==it->identity.address && x.identity.serial==it->identity.serial;});};
        if(!tracked())continue;
        const auto raw=ReadAiAllocation(it->identity,ReadAi);
        if(raw.valid && !SameAiAllocation(it->identity,raw)) {
            ReleaseHelper(*it);
            for(auto& x:ai.owned)if(x.identity.address==it->identity.address && x.identity.serial==it->identity.serial && x.tagsHeld)ReleaseAppearance(x.appearance);
            ai.owned.erase(std::remove_if(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==it->identity.address && x.identity.serial==it->identity.serial;}),ai.owned.end());continue;
        }
        // Zombies are driven by the zombies adapter; only their ownership
        // records are pruned here once the native allocation is gone.
        if(it->zombie)continue;
        const auto e=Entity(it->identity,scope.pool,false);
        // A transient invalid actor view or a corpse must not lose its allocation ticket.
        if(!e.valid || e.type!=kActorType || !SameAiAllocation(it->identity,raw) || e.health<=0)continue;
        if(!it->identity.matches(e)) {
            // Native initialization may replace a brain while retaining the
            // same Gentity allocation. Its validated self links identify it.
            it->identity=AiIdentity::capture(e);it->modelApplied=false;it->target=0;it->hasGoal=false;
            Log("AI_BIND refreshed entity=%u serial=%u",e.number,e.serial);
        }
        if(e.team!=it->team)reinterpret_cast<TeamFn>(aiProfile.setTeam)(e.sentient,it->team);
        if(!tracked() || !it->identity.matches(Entity(it->identity,scope.pool,false)))continue;
        int combat=0;
        if(Value(e.actor+0xd4c,combat) && combat>=0 && combat<=3 && combat!=0) {
            SetCount(e.actor+0xd4c,combat,0);std::uint8_t cover=0;
            if(Value(e.actor+0xd49,cover) && cover<=1)SetByte(e.actor+0xd49,cover,0);
        }
        for(const auto field:{kSentientIgnoreMe,kSentientIgnoreAll}) {
            std::uint8_t current=0;if(Value(e.sentient+field,current) && current<=1)SetByte(e.sentient+field,current,0);
        }
        auto self=e;self.team=it->team;
        auto chosen=ChooseAiTarget(self,ally,s.entity,player,ai.combatants);
        // The combatant cache can be 500 ms old. A dead or recycled allocation
        // must never become an enemy handle, especially during large battles.
        if(chosen.entity && chosen.entity!=s.entity) {
            const auto candidate=std::find_if(ai.combatants.begin(),ai.combatants.end(),[&](const auto& x){return x.address==chosen.entity;});
            const auto live=candidate==ai.combatants.end()?AiEntity{}:Entity(AiIdentity::capture(*candidate),scope.pool,false);
            if(candidate==ai.combatants.end() || !LiveAiTarget(*candidate,live,self.team))
                chosen={0,player,std::numeric_limits<float>::max()};
            else {chosen.position=live.origin;chosen.squaredDistance=PointDistanceSquared(self.origin,live.origin);}
        }
        const auto target=chosen.entity;auto goal=chosen.position;const auto distance=chosen.squaredDistance;
        std::uint16_t currentEnemy=0;
        const auto desiredEnemy=target?static_cast<std::uint16_t>((target-scope.pool)/kAiEntityStride+1):0;
        const bool enemyRead=Value(e.sentient+0x44,currentEnemy);
        if(target!=it->target || (target && enemyRead && currentEnemy!=desiredEnemy)) {
            RecordNativeOperation("SET_ENEMY_BEGIN",e.number);
            // Notify once for a genuinely new target. Reassertions of the same
            // cached target update combat state without waking script listeners.
            reinterpret_cast<EnemyFn>(aiProfile.setEnemy)(e.sentient,target,target!=it->target?1:0,0);
            RecordNativeOperation("SET_ENEMY_END",e.number);
            if(target!=it->target)Log("AI_COMBAT entity=%u team=%d target=%u",e.number,it->team,target?static_cast<unsigned>((target-scope.pool)/kAiEntityStride):0);
            else ++ai.status.combatReassertions;
            it->target=target;
        }
        if(!tracked() || !it->identity.matches(Entity(it->identity,scope.pool,false)))continue;
        // This is the native lastenemysightpos script property, not fabricated
        // perception-array offsets. Keep pursuit aware of its assigned opponent.
        std::uint8_t sight=0;
        if(Value(e.actor+kAiEnemySightValid,sight) && sight<=1) {
            if(target && SetVector(e.actor+kAiEnemySightPosition,chosen.position))SetByte(e.actor+kAiEnemySightValid,sight,1);
            else if(!target)SetByte(e.actor+kAiEnemySightValid,sight,0);
        }
        // Test38: allies follow the player as their native goal entity. The
        // engine re-reads the player's position, so following never waits for
        // this service. The goal radius bounds native cover/combat movement:
        // crowd-sized, stretched toward the chosen enemy so allies engage it.
        // Position goals (setGoal) would clear the goal entity again.
        if(it->team==ally && aiProfile.goalEntityReady) {
            std::uint16_t goalEntity=0;std::uintptr_t node=0,volume=0;
            const auto wanted=EntityHandleValue(s.entity,scope.pool);
            const bool read=Value(e.actor+kActorGoalEntity,goalEntity) && Value(e.actor+kActorGoalNode,node) && Value(e.actor+kActorGoalVolume,volume);
            if(read && wanted && (goalEntity!=wanted || node || volume)) {
                RecordNativeOperation("GOAL_ENTITY_BEGIN",e.number);
                if(AssignGoalEntity(e,s.entity))++ai.status.followGoalEntity;
                RecordNativeOperation("GOAL_ENTITY_END",e.number);
                it->hasGoal=false;
            }
            if(ClearFixedNode(e))++ai.status.followUnfixed;
            std::uint8_t pacifist=0;if(Value(e.actor+kActorPacifist,pacifist) && pacifist==1)SetByte(e.actor+kActorPacifist,pacifist,0);
            const float radius=SquadFollowRadius(followers,target!=0,target?PointDistanceSquared(player,chosen.position):0.f);
            float current=0.f;
            if(Value(e.actor+kActorGoalRadius,current) && RadiusNeedsUpdate(current,radius) && SetFloat(e.actor+kActorGoalRadius,current,radius))
                ++ai.status.followRadius;
        }
        const bool follow=it->team==ally && !aiProfile.goalEntityReady && (!target || PointDistanceSquared(e.origin,player)>640.f*640.f);
        const bool advance=it->team!=ally && distance>384.f*384.f;
        if(follow) {
            goal=player;
            const float angle=static_cast<float>(e.number%12)*.5235987756f;
            goal[0]+=112.f*std::cos(angle);goal[1]+=112.f*std::sin(angle);
            if(NeedsAiGoal(it->hasGoal,it->goal,goal)) {
                traceIgnoreEntity=e.number;const auto ground=FindSpawnPlacement(goal,false,Trace);traceIgnoreEntity=-1;
                if(ground)goal=ground->position;
            }
        }
        AiPoint nativeGoal{};
        const bool nativeGoalRead=ReadAi(e.actor+kActorGoalPosition,nativeGoal.data(),sizeof(nativeGoal)) && FinitePoint(nativeGoal);
        const bool redirected=nativeGoalRead && PointDistanceSquared(nativeGoal,goal)>96.f*96.f;
        if((follow || advance) && (NeedsAiGoal(it->hasGoal,it->goal,goal) || redirected)) {
            RecordNativeOperation("SET_GOAL_BEGIN",e.number);
            reinterpret_cast<GoalFn>(aiProfile.setGoal)(e.actor,goal.data(),0);
            RecordNativeOperation("SET_GOAL_END",e.number);
            it->goal=goal;it->hasGoal=true;it->lastGoalTime=time;
        }
        if(!tracked() || !it->identity.matches(Entity(it->identity,scope.pool,false)))continue;
        if(it->appearance.model && !it->modelApplied && time>=it->appearanceAt) {
            bool tagsValid=true;
            for(std::size_t n=0;n<kAiAttachmentCount;++n)
                if(it->appearance.tags[n]) {std::string current;if(!ScriptText(it->appearance.tags[n],current) || current!=it->appearance.tagNames[n])tagsValid=false;}
            if(!tagsValid) {Log("AI_MODEL tags changed before apply entity=%u",e.number);it->modelApplied=true;for(auto& live:ai.owned)if(live.identity.address==it->identity.address && live.identity.serial==it->identity.serial)live=update;continue;}
            std::uint16_t current=0;
            if(Value(e.address+kAiModelIndex,current) && current && SetWord(e.address+kAiModelIndex,current,it->appearance.model)) {
                for(std::size_t n=0;n<kAiAttachmentCount;++n) {
                    std::uint16_t old=0;
                    if(Value(e.address+kAiAttachmentModels+n*2,old))SetWord(e.address+kAiAttachmentModels+n*2,old,it->appearance.models[n]);
                    reinterpret_cast<ScriptStringFn>(aiProfile.setString)(e.address+kAiAttachmentTags+n*2,it->appearance.tags[n]);
                }
                std::uint32_t flags=0;
                if(Value(e.address+kAiAttachmentFlags,flags))SetCount(e.address+kAiAttachmentFlags,static_cast<int>(flags),static_cast<int>((it->appearance.flags&0x7fffffff)|(flags&0x80000000)));
                reinterpret_cast<DObjFn>(aiProfile.updateModel)(e.address,1);it->modelApplied=true;
                Log("AI_MODEL entity=%u model_index=%u attachment_slots=%u",e.number,it->appearance.model,static_cast<unsigned>(kAiAttachmentCount));
            }
        }
        if(epoch==ai.checkpointEpoch)for(auto& live:ai.owned)if(live.identity.address==it->identity.address && live.identity.serial==it->identity.serial) {live=std::move(update);break;}
    }
    if(epoch!=ai.checkpointEpoch)return;
    ai.status.followLastBatch=static_cast<int>(done);ai.status.followPeakBatch=std::max(ai.status.followPeakBatch,static_cast<int>(done));
    if(!ai.owned.empty())ai.followCursor=(cursorBase+done)%ai.owned.size();
    if(ai.owned.empty() && ai.recovery.empty())RestoreCapacity();
}
struct AiMeasure {
    LARGE_INTEGER start{},frequency{};bool world=false;
    explicit AiMeasure(bool scan=false):world(scan) {QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);}
    ~AiMeasure() {
        LARGE_INTEGER end{};QueryPerformanceCounter(&end);
        const double us=(end.QuadPart-start.QuadPart)*1000000.0/frequency.QuadPart;
        if(world) {++ai.status.worldReads;ai.worldTotalUs+=us;ai.status.worldAverageUs=ai.worldTotalUs/ai.status.worldReads;ai.status.worldPeakUs=std::max(ai.status.worldPeakUs,us);}
        else {++ai.status.serviceUpdates;ai.serviceTotalUs+=us;ai.status.serviceAverageUs=ai.serviceTotalUs/ai.status.serviceUpdates;ai.status.servicePeakUs=std::max(ai.status.servicePeakUs,us);}
    }
};
// Test38: zombies-mode spawns reuse the owned-actor factory with a fixed
// spawn point, the campaign spawn gate bypassed for this one native call,
// and no initial enemy (zombies pursue through goals, not combat).
struct SpawnOptions {
    const AiPoint* center=nullptr;
    bool zombie=false,dog=false,bypassPause=false,setEnemy=true;
    AiIdentity* created=nullptr;
};
void SpawnOwned(const AiRequest& request,const Snapshot& s,const AiScope& scope,int time,const RecoveryAi* recovery=nullptr,const SpawnOptions* options=nullptr);
void ServiceZombies(const Snapshot& s,const AiScope& scope,int time);
void ResetZombieGame(const char* reason,bool deleteActors);
void Service() {
    if(servicing || !nativePending.load(std::memory_order_acquire)) return;
    servicing=true;
    struct ClearServiceFlag { ~ClearServiceFlag() { servicing=false; } } clearServiceFlag;
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    NativeScriptParameters parameters{};int executeDepth=0,functionCount=0;
    // The clock may be called recursively by campaign scripts. Only run mod
    // work when the VM is idle and no builtin owns stack parameters.
    if(!Value(aiProfile.base+kScriptExecuteDepthRva,executeDepth) || executeDepth!=-1
        || !Value(aiProfile.base+kScriptFunctionCountRva,functionCount) || functionCount
        || !Value(aiProfile.base+kScriptParametersRva,parameters) || parameters.incoming || parameters.outgoing)return;
    auto& liveParameters=*reinterpret_cast<volatile NativeScriptParameters*>(aiProfile.base+kScriptParametersRva);
    auto& liveState=*reinterpret_cast<NativeScriptVmState*>(aiProfile.base+kScriptVmStateRva);
    auto& liveFrame=*reinterpret_cast<volatile std::uintptr_t*>(aiProfile.base+kScriptFramePointerRva);
    NativeScriptWorkFrame vmFrame(liveParameters,liveState,liveFrame,functionCount,executeDepth,
        aiProfile.base+kScriptStackBaseRva,aiProfile.base+kScriptFrameBaseRva,
        reinterpret_cast<std::uint8_t*>(aiProfile.base+kScriptFrameBaseRva),reinterpret_cast<std::uint8_t*>(aiProfile.base+kScriptStackBaseRva),
        reinterpret_cast<volatile std::uintptr_t*>(aiProfile.base+kScriptLocalVariablesPointerRva),aiProfile.base+kScriptLocalVariablesBaseRva);
    if(!vmFrame.valid())return;
    const auto workEpoch=ai.checkpointEpoch.load();
    struct RestoreWorkVm {
        NativeScriptWorkFrame& frame;std::uint64_t epoch;
        ~RestoreWorkVm() {
            if(epoch!=ai.checkpointEpoch) {frame.abandon();return;}
            auto& p=*reinterpret_cast<volatile NativeScriptParameters*>(aiProfile.base+kScriptParametersRva);
            int count=0,depth=0;
            if(!Value(aiProfile.base+kScriptFunctionCountRva,count) || !Value(aiProfile.base+kScriptExecuteDepthRva,depth)) {frame.abandon();return;}
            // Only values produced by this isolated operation are released.
            // Zero-object native Notify consumes both outgoing and incoming
            // parameters, including ignored native return values/references.
            if(!count && depth==-1 && p.incoming<=2048 && p.outgoing<=2048
                && p.top<=aiProfile.base+kScriptStackBaseRva+16*2047 && !(p.top&15)
                && p.top>=aiProfile.base+kScriptStackBaseRva+16*(static_cast<std::uintptr_t>(p.incoming)+p.outgoing))
                reinterpret_cast<void(*)(unsigned,unsigned,unsigned)>(aiProfile.base+kScriptNotifyRva)(0,0,p.incoming);
            if(!frame.close(count,depth)) {
                RecordNativeOperation("VM_WORK_UNBALANCED");
                Log("AI_SERVICE native VM work did not balance; incoming=%u outgoing=%u top=%p functions=%d depth=%d",p.incoming,p.outgoing,reinterpret_cast<void*>(p.top),count,depth);
            }
        }
    } restoreWorkVm{vmFrame,workEpoch};
    if(!CompleteServiceInstall()) {
        ai.queue.cancel(AiRequestResult::HookUnavailable);ai.hands.pending=false;ai.hands.result.state=NativeHandBuildState::Failed;
        Finish(AiRequestResult::HookUnavailable);return;
    }
    int time=0;
    if(!Value(aiProfile.timeSlot,time) || time==ai.lastServiceTime) return;
    AiMeasure measure;
    const auto s=ReadSnapshot(); const auto scope=Scope(s);
    Lifetime(scope);
    if(ai.cleanupDeferred && scope.ready) {
        ai.cleanupDeferred=false;ai.owned.clear();ai.capacity.clear();ai.adoptPending=true;
        ai.nextWorldTime=0;
        ai.queue.cancel(AiRequestResult::LifetimeChanged);ai.hands.pending=false;ai.handCache.clear();WorkWanted();
    }
    if(s.state==LevelState::PlayerDead && scope.ready) {
        ResetZombieGame("PLAYER_DIED",false);
        ClearRecovery(true);
        CleanupOwned("PLAYER_DEATH");ai.queue.cancel(AiRequestResult::LifetimeChanged);
        ai.hands.pending=false;ai.hands.result.state=NativeHandBuildState::Canceled;ai.handCache.clear();
        ai.lastServiceTime=time;WorkWanted();return;
    }
    if(!scope.ready) {Pause(s);WorkWanted();return;}
    ai.lastServiceTime=time;
    // Load shedding before any fatal native pool error: combat can grow
    // script threads/objects after spawning stops. Remove owned actors
    // (corpses and hostiles first) until every pool is back under its limit.
    if(!ai.owned.empty() && (ai.nextResourceCheck<0 || time>=ai.nextResourceCheck || time<ai.nextResourceCheck-1000)) {
        ai.nextResourceCheck=time+250;
        const auto shedEpoch=ai.checkpointEpoch.load();
        for(unsigned n=0;n<8 && !ai.owned.empty() && shedEpoch==ai.checkpointEpoch;++n) {
            const auto usage=ReadScriptVmUsage();const char* reason=ScriptVmEmergency(usage);
            if(!reason)break;
            if(!n)Log("AI_RESOURCE_SHED reason=%s parents=%u/%u peak_pool=%u/%u string_nodes=%u entities=%u/%u owned=%d",reason,usage.parents,usage.parentCapacity,
                usage.childPeakPool,usage.poolCapacity,usage.mtNativeUsedNodes,usage.entitiesInUse,usage.entityCapacity,OwnCount());
            if(!CleanupOwned("RESOURCE_EMERGENCY",true))break;
            ++ai.status.resourceShed;
        }
        if(shedEpoch!=ai.checkpointEpoch) {ai.lastServiceTime=-1;WorkWanted();return;}
    }
    if(ai.nextWorldTime<0 || time>=ai.nextWorldTime || ai.queue.pending()) {ai.nextWorldTime=time+500;ReadWorld(s,scope);}
    const auto epoch=ai.checkpointEpoch.load();
    Pause(s);ServiceHands(scope);if(epoch==ai.checkpointEpoch)FollowOwned(s,scope,time);
    if(epoch==ai.checkpointEpoch)ServiceZombies(s,scope,time);
    if(epoch!=ai.checkpointEpoch) {ai.lastServiceTime=-1;WorkWanted();return;}
    if(!ai.queue.pending()) {
        if(!ai.recovery.empty() && (ai.nextRecoveryTime<0 || time>=ai.nextRecoveryTime)) {
            auto& r=ai.recovery.front();
            if(SameAiAllocation(r.previous,ReadAiAllocation(r.previous,ReadAi))) {ReleaseRecovery(r);ai.recovery.pop_front();}
            else {
                const bool friendly=r.team==PlayerTeam(s);const auto& list=friendly?ai.friends:ai.enemies;
                auto source=std::find_if(list.begin(),list.end(),[&](const auto& e){return e.classname==r.sourceName;});
                if(source==list.end() && !list.empty())source=list.begin();
                if(source!=list.end()) {
                    AiRequest request;request.operation=friendly?AiOperation::SpawnFriendly:AiOperation::SpawnEnemy;request.source=AiIdentity::capture(*source);
                    SpawnOwned(request,s,scope,time,&r);
                    if(ai.status.lastResult==AiRequestResult::Created) {ReleaseRecovery(r);ai.recovery.pop_front();}
                }
                ai.nextRecoveryTime=time+(ai.status.lastResult==AiRequestResult::Created?50:1000);
            }
        }
        WorkWanted();return;
    }
    const auto request=ai.queue.take(scope);
    if(!request) {Finish(ai.queue.reason());return;}
    if(request->operation==AiOperation::RemoveOwned) {
        ClearRecovery(true);
        const auto removed=CleanupOwned("MENU_REMOVE");
        Finish(removed?AiRequestResult::Deleted:ai.owned.empty()?AiRequestResult::IdentityChanged:AiRequestResult::Protected);return;
    }
    if(request->operation==AiOperation::KillEnemies) {
        const int ally=PlayerTeam(s);
        for(auto it=ai.recovery.begin();it!=ai.recovery.end();) {
            if(it->team!=ally) {ReleaseRecovery(*it);it=ai.recovery.erase(it);}else ++it;
        }
        int count=0;if(!Value(aiProfile.countSlot,count) || count<2 || count>kAiEntityCapacity) {Finish(AiRequestResult::NotReady);return;}
        const int team=PlayerTeam(s);unsigned killed=0;
        std::vector<AiIdentity> targets;
        for(int n=2;n<count;++n) {
            const auto e=ReadAiEntity(scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride,n,ReadAi);
            if(e.valid && e.type==kActorType && e.health>0 && e.team!=team && (e.team==kAxisTeam || e.team==kAlliesTeam))targets.push_back(AiIdentity::capture(e));
        }
        for(const auto& identity:targets) {
            const auto e=Entity(identity,scope.pool,false);int protectedNumber=-1;
            if(!identity.matches(e) || !Value(aiProfile.protectedSlot,protectedNumber) || protectedNumber==e.number)continue;
            std::uint8_t damage=0;if(!Value(e.address+0x139,damage) || damage>1 || !SetByte(e.address+0x139,damage,1))continue;
            // Same native damage call and neutral arguments as the retail kill builtin.
            const bool hit=reinterpret_cast<DamageFn>(aiProfile.damage)(e.address,0,0,nullptr,e.origin.data(),0x3fffffff,0,0,-1,0,0,0);
            if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
            int health=1;if(hit && Value(e.address+kAiHealth,health) && health<=0)++killed;
            const auto after=Entity(identity,scope.pool,false);
            if(identity.matches(after)) {std::uint8_t current=0;if(Value(e.address+0x139,current) && current==1)SetByte(e.address+0x139,current,damage);}
        }
        ai.status.killed+=killed;Log("AI_KILL_ALL selected=%llu killed=%u",static_cast<unsigned long long>(targets.size()),killed);
        Finish(killed?AiRequestResult::Killed:AiRequestResult::NativeRefused);return;
    }
    SpawnOwned(*request,s,scope,time);
}
bool DogSpawner(const AiEntity& e) {
    auto copy=e;copy.flags&=~2u;
    std::string name=e.classname;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return e.type==kActorSpawnerType && EligibleSpawner(copy,e.team) && name.find("dog")!=std::string::npos;
}
void SpawnOwned(const AiRequest& input,const Snapshot& s,const AiScope& scope,int time,const RecoveryAi* recovery,const SpawnOptions* options) {
    const auto epoch=ai.checkpointEpoch.load();
    const auto* request=&input;
    const auto source=Entity(request->source,scope.pool);
    const bool friendly=request->operation==AiOperation::SpawnFriendly;
    const int playerTeam=PlayerTeam(s),team=friendly?playerTeam:(playerTeam==kAlliesTeam?kAxisTeam:kAlliesTeam);
    const bool dogSource=options && options->dog;
    if(!request->source.matches(source) || !(dogSource?DogSpawner(source):HumanSpawner(source))) {Finish(AiRequestResult::IdentityChanged);return;}
    const auto enabled=Bool(aiProfile.spawnEnabledSlot,"g_spawnai"),disabled=Bool(aiProfile.spawnDisabledSlot,"ai_disableSpawn");
    const bool bypass=options && options->bypassPause;
    if(!enabled.valid || !enabled.current || !disabled.valid || (!bypass && (disabled.current || ai.pauseWanted))) {Finish(AiRequestResult::SpawnPaused);return;}
    const auto limit=ActorLimit(); int actorCount=0;
    if(!limit.valid || !Value(aiProfile.actorCountSlot,actorCount) || actorCount<0 || actorCount>=NativeActorCapacity()) {Finish(AiRequestResult::Limit);return;}
    if(!recovery && actorCount+static_cast<int>(ai.recovery.size())>=NativeActorCapacity()) {Finish(AiRequestResult::Limit);return;}
    if(limit.current<NativeActorCapacity() && SetCount(limit.address+0x10,limit.current,NativeActorCapacity())) {
        ai.capacity.applied(limit.address,limit.current,NativeActorCapacity());
        Log("AI_CAPACITY requested native maximum %d original=%d",NativeActorCapacity(),limit.current);
    }
    std::array<std::uint8_t,kAiEntityCapacity> active{};
    if(!ReadAi(aiProfile.activeArray,active.data(),active.size())
        || std::count(active.begin()+2,active.end()-2,0)<2) {Finish(AiRequestResult::Limit);return;}
    // Keep every fatal native pool (script variables, string tree, entities)
    // out of reach: refuse one more owned actor before headroom runs out.
    if(const char* refusal=ScriptVmSpawnRefusal(ReadScriptVmUsage())) {
        static ULONGLONG lastLog=0;const auto now=GetTickCount64();
        if(now-lastLog>5000){lastLog=now;Log("AI_RESOURCE_GUARD refused=%s actors=%d",refusal,actorCount);LogScriptVm(true);}
        Finish(AiRequestResult::ResourceGuard);return;
    }
    AiPoint center{};
    if(!ReadAi(s.entity+kAiOrigin,center.data(),sizeof(center)) || !FinitePoint(center)) {Finish(AiRequestResult::NotReady);return;}
    const AiPoint player=center;
    bool nearPlayer=friendly; unsigned pointNumber=0;
    if(options && options->center && FinitePoint(*options->center)) {center=*options->center;nearPlayer=false;pointNumber=1;}
    else if(!friendly) {
        float nearest=std::numeric_limits<float>::max();
        for(const auto& point:ai.enemyPoints) {
            const auto live=Entity(AiIdentity::capture(point),scope.pool);
            if(!HumanSpawner(live) || live.type!=kActorSpawnerType) continue;
            const auto distance=PointDistanceSquared(live.origin,player);
            if(distance<nearest) {nearest=distance;center=live.origin;pointNumber=live.number;}
        }
        if(!pointNumber) nearPlayer=true;
    }
    AiAppearance appearance;
    if(recovery)appearance=recovery->appearance;
    else if(request->appearance.address) {
        const auto hero=Entity(request->appearance,scope.pool);
        if(!SameAiAllocation(request->appearance,ReadAiAllocation(request->appearance,ReadAi)) || !ReadAppearance(hero,appearance)) {Finish(AiRequestResult::ModelUnavailable);return;}
    }
    const auto placement=recovery?FindRecoveryPlacement(recovery->position,player,Trace):FindSpawnPlacement(center,nearPlayer,Trace);
    if(!placement) {Finish(AiRequestResult::NoClearPosition);return;}
    // One persistent helper spawner per (source class, team) serves every owned
    // actor of that kind, as campaign spawners with count>1 do. Each helper
    // is a real entity with its own script object; one per actor cost an
    // entity and its variables per actor (Test34: Contingency 1,395 entities
    // at 0 actors, ~2.4 more per actor against the fixed 2,046 limit).
    std::uintptr_t temporary=0;bool reused=false;
    for(const auto& x:ai.owned) {
        if(!ai.reuseHelpers || x.team!=team || x.sourceName!=source.classname)continue;
        const auto helper=ReadAiAllocation(x.spawner,ReadAi);
        if(SameAiAllocation(x.spawner,helper) && helper.type==kActorSpawnerType){temporary=x.spawner.address;reused=true;break;}
    }
    if(!reused) {
        // Allocate a real engine entity and copy registered fields/VM vars through
        // native helpers. Original mission spawners, origins and budgets stay intact.
        temporary=reinterpret_cast<AllocateFn>(aiProfile.allocate)();
        if(!temporary || temporary<scope.pool || (temporary-scope.pool)%kAiEntityStride) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    }
    // A shared helper still serves other owned actors: never free it here.
    struct Temporary {std::uintptr_t pointer;std::uint64_t epoch;bool keep=false;~Temporary(){if(!keep && epoch==ai.checkpointEpoch)reinterpret_cast<EntityFn>(aiProfile.freeEntity)(pointer);}} cleanup{temporary,epoch,reused};
    if(!reused) {
        reinterpret_cast<CopyFn>(aiProfile.copyFields)(temporary,source.address);
        reinterpret_cast<CopyFn>(aiProfile.copyDynamic)(temporary,source.address);
        if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
        // Persistent helper spawners must not duplicate a mission targetname or
        // inherit an encounter's scripted target. Native setter maintains refcounts.
        reinterpret_cast<ScriptStringFn>(aiProfile.setString)(temporary+kAiTargetname,0);
        reinterpret_cast<ScriptStringFn>(aiProfile.setString)(temporary+kAiTarget,0);
    }
    std::uint8_t type=0; std::uint32_t flags=0; int count=0,oldTeam=0;
    if(!Value(temporary,type) || !SetByte(temporary,type,kActorSpawnerType)
        || !Value(temporary+kAiSpawnFlags,flags) || !SetCount(temporary+kAiSpawnFlags,static_cast<int>(flags),static_cast<int>(flags&~3u))
        || !Value(temporary+kAiSpawnCount,count) || !SetCount(temporary+kAiSpawnCount,count,1)
        || !Value(temporary+kAiSpawnerTeam,oldTeam) || !SetCount(temporary+kAiSpawnerTeam,oldTeam,team)) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    reinterpret_cast<OriginFn>(aiProfile.setOrigin)(temporary,placement->position.data());
    if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
    Log("AI_SPAWN_BEGIN team=%d source=%u serial=%u classname=%s helper_reused=%d point=%u placement_attempts=%d position=%.1f,%.1f,%.1f native_thread=%lu",
        team,source.number,source.serial,source.classname.c_str(),reused,pointNumber,placement->attempts,
        placement->position[0],placement->position[1],placement->position[2],GetCurrentThreadId());
    RecordNativeOperation("SPAWN_BEGIN",source.number,actorCount);
    // Argument three skips the factory's visibility/player-distance refusal.
    // The separate eviction spawnflag2 is cleared; physical clearance was traced.
    struct SpawnScope {SpawnScope(){spawningOwned=true;}~SpawnScope(){spawningOwned=false;}};
    // A zombies-mode spawn opens the native ai_disableSpawn gate for this call
    // only; campaign spawners stay closed. The gate is closed again at once.
    const bool reopen=bypass && disabled.current==1 && SetByte(disabled.address+0x10,1,0);
    std::uintptr_t spawned=0;{SpawnScope spawnScope;spawned=reinterpret_cast<SpawnFn>(aiProfile.spawn)(temporary,0,1,0);}
    if(reopen) {std::uint8_t now=0;if(Value(disabled.address+0x10,now))SetByte(disabled.address+0x10,now,1);}
    RecordNativeOperation("SPAWN_END",source.number,actorCount+static_cast<int>(spawned!=0));
    if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
    if(!spawned) {
        // Never strand spawning on a helper the native factory stopped accepting.
        if(reused && ai.reuseHelpers){ai.reuseHelpers=false;Log("AI_HELPER_REUSE disabled=1 reason=NATIVE_REFUSED helper=%u",static_cast<unsigned>((temporary-scope.pool)/kAiEntityStride));}
        Finish(AiRequestResult::NativeRefused);return;
    }
    if(spawned<scope.pool || (spawned-scope.pool)%kAiEntityStride
        || (spawned-scope.pool)/kAiEntityStride>=static_cast<std::uintptr_t>(kAiEntityCapacity-2)) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    // Roll back an allocated NPC before its helper if initialization/validation
    // fails. Otherwise a living NPC can retain a dangling self.spawner reference.
    struct SpawnedRollback {
        AiIdentity identity;std::uint64_t epoch=ai.checkpointEpoch.load();bool keep=false;
        ~SpawnedRollback() {
            if(keep || epoch!=ai.checkpointEpoch)return;
            const auto raw=ReadAiAllocation(identity,ReadAi);
            if(SameAiAllocation(identity,raw) && raw.number>1 && (raw.type==kActorType || raw.type==kActorCorpseType)) {
                int protectedNumber=-1;
                if(Value(aiProfile.protectedSlot,protectedNumber) && protectedNumber!=raw.number)
                    reinterpret_cast<DeleteFn>(aiProfile.remove)(raw.number);
            }
        }
    } rollback;
    rollback.identity.address=spawned;rollback.identity.number=static_cast<std::uint16_t>((spawned-scope.pool)/kAiEntityStride);
    Value(spawned+kAiUseCount,rollback.identity.serial);
    auto actor=ReadAiEntity(spawned,static_cast<int>((spawned-scope.pool)/kAiEntityStride),ReadAi);
    if(!actor.valid || actor.type!=kActorType) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    reinterpret_cast<TeamFn>(aiProfile.setTeam)(actor.sentient,team);
    if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
    if(!options || options->setEnemy)reinterpret_cast<EnemyFn>(aiProfile.setEnemy)(actor.sentient,friendly?0:s.entity,1,0);
    if(epoch!=ai.checkpointEpoch) {Finish(AiRequestResult::LifetimeChanged);return;}
    actor=ReadAiEntity(spawned,actor.number,ReadAi);
    if(!actor.valid || actor.team!=team) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    if(recovery && actor.health!=recovery->health)SetCount(actor.address+kAiHealth,actor.health,recovery->health);
    const auto temp=ReadAiEntity(temporary,static_cast<int>((temporary-scope.pool)/kAiEntityStride),ReadAi);
    if(!temp.valid) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    if((!reused && !MarkOwned(temporary,kOwnedSpawnerTag)) || !MarkOwned(spawned,OwnedActorTag(temp.number))) {
        Finish(AiRequestResult::NativeReturnedInvalid);return;
    }
    ai.combatants.push_back(actor);
    ai.owned.push_back({AiIdentity::capture(actor),AiIdentity::capture(temp),team,-1,0,{},false,appearance,time+1000,false,source.classname});cleanup.keep=true;rollback.keep=true;++ai.status.created;
    if(appearance.model) {HoldAppearance(ai.owned.back().appearance);ai.owned.back().tagsHeld=true;}
    ai.owned.back().zombie=options && options->zombie;
    if(options && options->created)*options->created=AiIdentity::capture(actor);
    ai.status.lastEntity=actor.number;ai.status.lastTeam=team;ai.status.lastPosition=actor.origin;
    ai.status.lastSpawnDistance=std::sqrt(PointDistanceSquared(actor.origin,player));ai.status.placementAttempts=placement->attempts;
    std::snprintf(ai.status.lastName.data(),ai.status.lastName.size(),"%s",source.classname.c_str());
    Log("AI_SPAWN_ACCEPT entity=%u serial=%u actor=%p sentient=%p team=%d health=%d origin=%.1f,%.1f,%.1f distance=%.1f original_spawner_untouched=1",
        actor.number,actor.serial,reinterpret_cast<void*>(actor.actor),reinterpret_cast<void*>(actor.sentient),actor.team,actor.health,
        actor.origin[0],actor.origin[1],actor.origin[2],ai.status.lastSpawnDistance);
    Finish(AiRequestResult::Created);
}
// =====================================================================
// Test38 playable zombies: the native adapter. Rules come from the portable
// BO1 ports in bo1_zombie_rules.hpp; this section only translates them to
// exact MW2 actors, goals, damage and builtins on the server clock.
// =====================================================================
ScriptCallNatives scriptNatives;
bool zombieBindingsReady=false,cbufReady=false;
using CbufFn=void(*)(int,const char*);
struct ZombieActor {
    AiIdentity identity; bo1::ActorTicket ticket; bool dog=false;
    bo1::MoveSpeed speed=bo1::MoveSpeed::Walk;
    int spawned=0,nextRepath=0,failsafeAt=0,attackStart=0,lastMiss=-1000000,nukeAt=-1;
    AiPoint failsafeOrigin{},lastOrigin{};
    bool attacking=false,fired=false,sprintApplied=false;
    int health=0,reconfigureAt=0;bool damaged=false,reconfigured=false;
    // Test39: BO1 rise out of the ground and animated attacks.
    int risingUntil=0,showAt=-1;bool rose=false,riseStopped=false;
    // zombie_melee: one hit check per "fire" notetrack of the attack anim.
    static constexpr std::size_t kMaxFires=8;
    std::array<int,kMaxFires> fireMs{};std::size_t fireCount=0,nextFire=0;
    int attackEndMs=static_cast<int>(bo1::kAttackEndMilliseconds);
    bool animatedAttack=false;
    // MW2 maps/_utility setup (gun_remove, disable_pain, ...) after spawn init.
    bool scriptSetup=false;
    // Test40: wearing a BO1 body and head.
    bool dressed=false;
};
struct ZombieDrop { bo1::Powerup type=bo1::Powerup::None; AiPoint origin{}; int spawned=0; };
struct ZombieGameState {
    bool running=false,startWanted=false,stopWanted=false;
    ZombieSettings settings,requested;
    std::string pendingMap;
    bo1::RoundController controller; bo1::PowerupDirector powerups; bo1::DogRounds dogs; bo1::ScoreLedger ledger;
    std::vector<ZombieActor> zombies; std::vector<ZombieDrop> drops;
    int doublePointsUntil=0,instaKillUntil=0,nextWatch=0,nextCleanup=0,nukePointsAt=-1,startTime=0;
    int firstRound=1,previousSpawner=-1,dogFailures=0,announcedRound=0,lastPlayerHealth=0;
    unsigned kills=0,headshots=0,hits=0,campaignRemoved=0,meleeHits=0,meleeMisses=0;
    std::uint32_t random=0x5eed1u;
    std::uintptr_t client=0; char map[96]{};
    bool loadout=false,restorePause=false,pauseBefore=false;
    int loadoutAt=0,loadoutTries=0;
    std::string pistol,grenade;
    std::string event; int eventTime=-1000000;
    int lastDelta=0,deltaTime=-1000000,gameOverRound=0,lastTime=0;
    bool dogSpawners=false;
    AiPoint lastDogOrigin{};bool lastDogKnown=false;
    int readySince=-1;
    // Test39: the Spec Ops map this game was launched on, for GAME OVER relaunch.
    std::string launchedMap; ULONGLONG relaunchAt=0; ZombieSettings launchSettings;
    unsigned rises=0,animatedAttacks=0,riseFailures=0,attacksBrokenOff=0,setupZombies=0;
} zg;
int ZombieRandom(int n) {
    zg.random^=zg.random<<13;zg.random^=zg.random>>17;zg.random^=zg.random<<5;
    return n>0?static_cast<int>(zg.random%static_cast<std::uint32_t>(n)):0;
}
const bo1::RandomInt& ZombieRng() {static const bo1::RandomInt rng=[](int n){return ZombieRandom(n);};return rng;}
bool ZombieWorkWanted() {return zg.running || zg.startWanted || zg.stopWanted;}
void ZombieEvent(const char* text,int time) {zg.event=text;zg.eventTime=time;}
ScriptCallResult Builtin(const BuiltinBinding& b,unsigned entref,std::initializer_list<ScriptArg> args,ScriptReturn* out=nullptr) {
    if(!zombieBindingsReady)return ScriptCallResult::Invalid;
    auto& p=*reinterpret_cast<volatile NativeScriptParameters*>(aiProfile.base+kScriptParametersRva);
    const auto r=CallScriptBuiltin(p,scriptNatives,reinterpret_cast<ScriptBuiltin>(aiProfile.base+b.rva),entref,args,out);
    if(r!=ScriptCallResult::Ok)Log("ZOMBIE_BUILTIN name=%s result=%s",b.name,ScriptCallText(r));
    return r;
}
void Announce(const char* text) {Builtin(kBuiltinIPrintLnBold,0,{ScriptArg::String(text)});}
// ---- Test39: Spec Ops level preparation and the BO1 animation import ----
// When a zombies launch loads its Spec Ops map, the level's scripts are
// prepared while they are still being linked (Scr_EndLoadScripts):
//  * the three Spec Ops end-of-mission functions are entered with OP_End, so
//    objectives, timers, kill counts and leaving the area cannot end the level;
//  * MW2 soldier animations that zombies play are replaced, in place, by BO1
//    zombie animations read from the owner's own common_zombie.ff. The MW2
//    XAnimParts headers are restored when the next level loads, before any
//    actor of that level exists.
enum class AnimRole : int {Move,Idle,Death,
    RiseWalk,RiseWalk2,RiseRun,RiseSprint,                                   // _zombie_rise_anims
    Attack1,Attack2,Attack4,Attack6,AttackForward1,AttackForward2,             // _zombie_melee
    WalkAttack1,WalkAttack2,WalkAttack3,WalkAttack4,                            // _zombie_walk_melee
    RunAttack1,RunAttack2,RunAttack3,Count};                                    // _zombie_run_melee
struct AnimSlot { const char* mw2; const char* bo1; AnimRole role; };
// MW2 animscripts (animset.gsc, walk.gsc, run.gsc, stop.gsc, death.gsc):
// walkers play walk_cqb_f, runners run_lowready_f, sprinters (enable_sprint)
// sprint_loop_distant/sprint1_loop; idles are the casual/exposed alert idles;
// deaths are death.gsc's standing, running and strong-hit lists.
// BO1-only anims (rise, melee) need a leaf in the generic_human tree that a
// zombie never plays itself, so animscripted can name them: flashbang
// reactions (no flashbangs in the loadout), grenade return/cower anims
// (grenadeawareness 0) and surprise/bullet reactions (disabled at setup).
inline constexpr AnimSlot kAnimSlots[]={
    {"walk_cqb_f","ai_zombie_walk_v1",AnimRole::Move},{"walk_forward","ai_zombie_walk_v2",AnimRole::Move},
    {"run_lowready_f","ai_zombie_run_v2",AnimRole::Move},{"run_cqb_f_search_v1","ai_zombie_walk_fast_v1",AnimRole::Move},
    {"heat_run_loop","ai_zombie_run_v3",AnimRole::Move},
    {"sprint_loop_distant","ai_zombie_sprint_v1",AnimRole::Move},{"sprint1_loop","ai_zombie_sprint_v2",AnimRole::Move},
    {"casual_stand_idle","ai_zombie_idle_v1",AnimRole::Idle},{"casual_stand_v2_idle","ai_zombie_idle_v1",AnimRole::Idle},
    {"exposed_idle_alert_v1","ai_zombie_idle_v1",AnimRole::Idle},{"exposed_idle_alert_v2","ai_zombie_idle_v1",AnimRole::Idle},
    {"exposed_idle_alert_v3","ai_zombie_idle_v1",AnimRole::Idle},
    {"exposed_death_twist","ai_zombie_death_v1",AnimRole::Death},{"stand_death_shoulder_spin","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_tumbleforward","ai_zombie_death_v1",AnimRole::Death},{"stand_death_stumbleforward","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_falltoknees","ai_zombie_death_v1",AnimRole::Death},{"exposed_death_falltoknees_02","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_02","ai_zombie_death_v1",AnimRole::Death},{"exposed_death_headshot","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_flop","ai_zombie_death_v1",AnimRole::Death},{"exposed_death_neckgrab","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_groin","ai_zombie_death_v1",AnimRole::Death},{"exposed_death_blowback","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_face","ai_zombie_death_v1",AnimRole::Death},{"stand_death_headshot_slowfall","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_tumbleback","ai_zombie_death_v1",AnimRole::Death},{"stand_death_shoulderback","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_leg","ai_zombie_death_v1",AnimRole::Death},{"stand_death_crotch","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_guts","ai_zombie_death_v1",AnimRole::Death},{"stand_death_fallside","ai_zombie_death_v2",AnimRole::Death},
    {"stand_death_head_straight_back","ai_zombie_death_v1",AnimRole::Death},{"stand_death_chest_stunned","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_nerve","ai_zombie_death_v1",AnimRole::Death},{"exposed_death_firing_02","ai_zombie_death_v2",AnimRole::Death},
    {"exposed_death_firing","ai_zombie_death_v1",AnimRole::Death},{"exposed_death","ai_zombie_death_v2",AnimRole::Death},
    {"run_death_facedown","ai_zombie_death_v1",AnimRole::Death},{"run_death_roll","ai_zombie_death_v2",AnimRole::Death},
    {"run_death_fallonback","ai_zombie_death_v1",AnimRole::Death},{"run_death_flop","ai_zombie_death_v2",AnimRole::Death},
    {"death_shotgun_legs","ai_zombie_death_v1",AnimRole::Death},{"death_stand_sniper_leg","ai_zombie_death_v2",AnimRole::Death},
    {"death_shotgun_back_v1","ai_zombie_death_v1",AnimRole::Death},{"death_stand_sniper_chest1","ai_zombie_death_v2",AnimRole::Death},
    {"death_stand_sniper_chest2","ai_zombie_death_v1",AnimRole::Death},{"death_stand_sniper_spin1","ai_zombie_death_v2",AnimRole::Death},
    {"death_stand_sniper_spin2","ai_zombie_death_v1",AnimRole::Death},{"death_shotgun_spinl","ai_zombie_death_v2",AnimRole::Death},
    {"death_shotgun_spinr","ai_zombie_death_v1",AnimRole::Death},
    {"exposed_flashbang_v1","ai_zombie_traverse_ground_v1_walk",AnimRole::RiseWalk},
    {"exposed_idle_twitch","ai_zombie_traverse_ground_v2_walk_alta",AnimRole::RiseWalk2},
    {"exposed_flashbang_v2","ai_zombie_traverse_ground_v1_run",AnimRole::RiseRun},
    {"exposed_flashbang_v3","ai_zombie_traverse_ground_climbout_fast",AnimRole::RiseSprint},
    {"exposed_flashbang_v4","ai_zombie_attack_v1",AnimRole::Attack1},
    {"exposed_flashbang_v5","ai_zombie_attack_v2",AnimRole::Attack2},
    {"grenade_return_running_throw_forward","ai_zombie_attack_v4",AnimRole::Attack4},
    {"grenade_return_standing_throw_forward_1","ai_zombie_attack_v6",AnimRole::Attack6},
    {"grenade_return_standing_throw_overhand_forward","ai_zombie_attack_forward_v1",AnimRole::AttackForward1},
    {"exposed_squat_down_grenade_f","ai_zombie_attack_forward_v2",AnimRole::AttackForward2},
    {"exposed_squat_idle_grenade_f","ai_zombie_walk_attack_v1",AnimRole::WalkAttack1},
    {"exposed_dive_grenade_b","ai_zombie_walk_attack_v2",AnimRole::WalkAttack2},
    {"exposed_idle_reacta","ai_zombie_walk_attack_v3",AnimRole::WalkAttack3},
    {"exposed_idle_reactb","ai_zombie_walk_attack_v4",AnimRole::WalkAttack4},
    {"exposed_dive_grenade_f","ai_zombie_run_attack_v1",AnimRole::RunAttack1},
    {"run_react_duck","ai_zombie_run_attack_v2",AnimRole::RunAttack2},
    {"run_react_flinch","ai_zombie_run_attack_v3",AnimRole::RunAttack3}};
// level._zombie_melee / _zombie_walk_melee / _zombie_run_melee (_zombiemode.gsc).
inline constexpr AnimRole kMeleeAnims[]={AnimRole::Attack2,AnimRole::Attack4,AnimRole::Attack6,AnimRole::Attack1,AnimRole::AttackForward1,AnimRole::AttackForward2};
inline constexpr AnimRole kWalkMeleeAnims[]={AnimRole::WalkAttack1,AnimRole::WalkAttack2,AnimRole::WalkAttack3,AnimRole::WalkAttack4};
inline constexpr AnimRole kRunMeleeAnims[]={AnimRole::RunAttack1,AnimRole::RunAttack2,AnimRole::RunAttack3};
inline constexpr int kFirstSpecialRole=static_cast<int>(AnimRole::RiseWalk);
inline constexpr int kAnimRoleCount=static_cast<int>(AnimRole::Count);
using FindAssetEntryFn=std::uintptr_t(*)(int,const char*);
using SlGetStringFn=unsigned(*)(const char*,unsigned,unsigned);
using GetFunctionHandleFn=int(*)(const char*,const char*);
using EntAnimTreeFn=std::uintptr_t(*)(std::uintptr_t);
using AnimTreeIndexFn=int(*)(std::uintptr_t);
using AnimTreeByIndexFn=std::uintptr_t(*)(int);
using EndLoadFn=void(*)();
struct AppliedAnim { std::uintptr_t parts=0; std::array<std::uint8_t,bo1xanim::kMw2XAnimBytes> original{},written{}; std::string mw2,bo1; };
struct TreeAnims { bool valid=false; int tree=-1; std::array<int,kAnimRoleCount> index{}; };
struct LevelPrep {
    bool bindings=false,hookInstalled=false;
    bool prepared=false; std::string map;
    std::vector<std::unique_ptr<bo1xanim::Image>> images;
    std::vector<AppliedAnim> applied;
    std::array<std::uintptr_t,kAnimRoleCount> roleParts{};
    std::array<const bo1xanim::Anim*,kAnimRoleCount> roleAnim{};
    std::vector<std::pair<std::uintptr_t,std::uint8_t>> guards;
    std::unordered_map<std::uintptr_t,TreeAnims> trees;
    unsigned slotsWanted=0;
    // Compiled maps/_utility setup functions of the level being played
    // (Scr_GetFunctionHandle is only valid while the level's scripts link).
    bool threads=false;
    std::array<int,static_cast<std::size_t>(SetupFunction::Count)> setup{};
    unsigned setupCalls=0,setupFailures=0;
} prep;
// ---- Test40: BO1 zombie character models ----
struct ModelPrep {
    bool bindings=false,hookInstalled=false;
    bool appended=false;            // the generated zone is part of this level's load
    std::string zoneFor;            // the level zone it was loaded with
    bool precached=false;
    std::vector<std::string> bodies,heads;   // precached, in BO1 alias order
    int helmetSwap=0;               // animscripts\shared::HandleNoteTrack
    unsigned dressed=0,dressFailures=0,helmetSwaps=0;
    unsigned long long firstDressed=0;   // GetTickCount64 of this level's first dressed zombie (crash guard)
} models;
bool WriteProtected(std::uintptr_t at,const void* bytes,std::size_t size) {
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),size,PAGE_READWRITE,&old)) return false;
    std::memcpy(reinterpret_cast<void*>(at),bytes,size);
    DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(at),size,old,&ignored);
    return true;
}
const bo1xanim::Anim* RoleAnim(AnimRole role) {return prep.roleAnim[static_cast<std::size_t>(role)];}
// Runs one maps/_utility setup function on a zombie (see kSetupFunctions).
bool RunSetupFunction(unsigned entnum,SetupFunction f,std::initializer_list<ScriptArg> args={}) {
    const auto n=static_cast<std::size_t>(f);
    if(!prep.threads || !prep.setup[n] || args.size()!=static_cast<std::size_t>(kSetupFunctions[n].params)) return false;
    auto& p=*reinterpret_cast<volatile NativeScriptParameters*>(aiProfile.base+kScriptParametersRva);
    const auto r=CallScriptThread(p,scriptNatives,reinterpret_cast<ScriptExecEntThread>(aiProfile.base+kScrExecEntThreadNumRva),
        reinterpret_cast<ScriptFreeThread>(aiProfile.base+kScrFreeThreadRva),entnum,prep.setup[n],args);
    ++prep.setupCalls;
    if(r!=ScriptCallResult::Ok && ++prep.setupFailures<=24) Log("ZOMBIE_SETUP entity=%u function=%s result=%s",entnum,kSetupFunctions[n].function,ScriptCallText(r));
    return r==ScriptCallResult::Ok;
}
// BO1 sound notetracks ("sndnt#alias") name BO1 aliases MW2 does not have.
bo1xanim::Anim ForMw2(const bo1xanim::Anim& source) {
    bo1xanim::Anim a=source;
    a.notify.erase(std::remove_if(a.notify.begin(),a.notify.end(),[](const auto& n){return n.first.rfind("sndnt#",0)==0;}),a.notify.end());
    return a;
}
void RestoreLevelPrep(const char* reason) {
    // Restore only headers that are still ours, in the asset the database
    // still returns (the common zone stays loaded between levels).
    const auto find=reinterpret_cast<FindAssetEntryFn>(aiProfile.base+kDbFindXAssetEntryRva);
    unsigned skipped=0;
    for(auto it=prep.applied.rbegin();it!=prep.applied.rend();++it) {
        const auto entry=find(kAssetTypeXAnimParts,it->mw2.c_str());
        std::uintptr_t parts=0;std::array<std::uint8_t,bo1xanim::kMw2XAnimBytes> current{};
        if(!entry || !Value(entry+kAssetEntryHeader,parts) || parts!=it->parts || !ReadAi(parts,current.data(),current.size()) || current!=it->written) {++skipped;continue;}
        if(!WriteProtected(it->parts,it->original.data(),it->original.size())) Log("BO1_ANIM restore_failed mw2=%s",it->mw2.c_str());
    }
    if(skipped)Log("BO1_ANIM restore_skipped=%u (asset replaced or reloaded)",skipped);
    if(!prep.applied.empty() || prep.prepared)
        Log("LEVEL_PREP restored=1 reason=%s anims=%llu map=%s",reason,static_cast<unsigned long long>(prep.applied.size()),prep.map.c_str());
    prep.applied.clear();prep.images.clear();prep.trees.clear();
    prep.roleParts.fill(0);prep.roleAnim.fill(nullptr);prep.guards.clear();prep.prepared=false;prep.map.clear();
    prep.setup.fill(0);
}
unsigned ApplySpecOpsGuards() {
    const auto base=aiProfile.base;
    std::uintptr_t program=0;std::uint32_t size=0;
    if(!Value(base+kScrProgramBufferRva,program) || !Value(base+kScrProgramSizeRva,size) || !program || !size) {Log("SO_GUARD program_buffer=UNAVAILABLE");return 0;}
    const auto handleOf=reinterpret_cast<GetFunctionHandleFn>(base+kScrGetFunctionHandleRva);
    unsigned applied=0;
    for(const auto& f:kSpecOpsEndFunctions) {
        const int handle=handleOf(f.file,f.function);
        if(handle<=0 || static_cast<std::uint32_t>(handle)>=size) {Log("SO_GUARD function=%s::%s present=0",f.file,f.function);continue;}
        const auto code=program+static_cast<std::uint32_t>(handle);
        std::uint8_t op=0;
        if(!Value(code,op) || op==kVmOpEnd || op>0x8b) {Log("SO_GUARD function=%s::%s refused opcode=0x%02x",f.file,f.function,op);continue;}
        if(!WriteProtected(code,&kVmOpEnd,1)) {Log("SO_GUARD function=%s::%s write_failed=1",f.file,f.function);continue;}
        prep.guards.push_back({code,op});++applied;
        Log("SO_GUARD function=%s::%s handle=0x%x first_opcode=0x%02x -> OP_End",f.file,f.function,handle,op);
    }
    return applied;
}
unsigned ApplyAnimSwaps() {
    prep.slotsWanted=static_cast<unsigned>(std::size(kAnimSlots));
    if(!bo1assets::WaitReady(15000)) {
        const auto st=bo1assets::GetStatus();
        Log("BO1_ANIM swap=0 assets=%s detail=%s",bo1assets::StateText(st.state),st.detail.c_str());return 0;
    }
    const auto base=aiProfile.base;
    const auto find=reinterpret_cast<FindAssetEntryFn>(base+kDbFindXAssetEntryRva);
    const auto slGet=reinterpret_cast<SlGetStringFn>(base+kSlGetStringRva);
    std::unordered_map<std::string,std::uint16_t> interned;
    auto intern=[&](const std::string& text) -> std::uint16_t {
        const auto it=interned.find(text);if(it!=interned.end())return it->second;
        const unsigned id=slGet(text.c_str(),0,kSlGetStringType);
        const auto value=static_cast<std::uint16_t>(id>0 && id<=0xffffu?id:0u);
        interned.emplace(text,value);return value;
    };
    std::unordered_map<std::string,bo1xanim::Image*> built;
    unsigned applied=0;
    for(const auto& slot:kAnimSlots) {
        const auto* source=bo1assets::Find(slot.bo1);
        const auto entry=find(kAssetTypeXAnimParts,slot.mw2);
        std::uintptr_t parts=0;
        if(!source || !entry || !Value(entry+kAssetEntryHeader,parts) || !parts) {
            Log("BO1_ANIM slot=%s source=%s missing=%s",slot.mw2,slot.bo1,!source?"BO1":"MW2");continue;
        }
        std::array<std::uint8_t,bo1xanim::kMw2XAnimBytes> original{};
        if(!ReadAi(parts,original.data(),original.size())) continue;
        // Never re-apply onto a header we already replaced (shared MW2 aliases).
        if(std::any_of(prep.applied.begin(),prep.applied.end(),[&](const auto& a){return a.parts==parts;})) continue;
        const auto frames=bo1xanim::Get<std::uint16_t>(original.data()+bo1xanim::mw2::kNumFrames);
        const auto rate=bo1xanim::Get<float>(original.data()+bo1xanim::mw2::kFramerate);
        if(!frames || !(rate>0.f && rate<=1000.f)) {Log("BO1_ANIM slot=%s refused=MW2_HEADER",slot.mw2);continue;}
        auto& image=built[slot.bo1];
        if(!image) {
            auto owned=std::make_unique<bo1xanim::Image>();
            if(!bo1xanim::Build(ForMw2(*source),intern,0,*owned)) {Log("BO1_ANIM source=%s refused=BUILD",slot.bo1);continue;}
            image=owned.get();prep.images.push_back(std::move(owned));
        }
        auto header=image->header;
        std::memcpy(header.data(),original.data(),8);                 // keep MW2's asset name
        if(!WriteProtected(parts,header.data(),header.size())) {Log("BO1_ANIM slot=%s write_failed=1",slot.mw2);continue;}
        prep.applied.push_back({parts,original,header,slot.mw2,slot.bo1});++applied;
        const auto r=static_cast<std::size_t>(slot.role);
        if(!prep.roleParts[r]) {prep.roleParts[r]=parts;prep.roleAnim[r]=source;}
    }
    int special=0;
    for(int k=kFirstSpecialRole;k<kAnimRoleCount;++k) if(prep.roleParts[static_cast<std::size_t>(k)]) ++special;
    Log("BO1_ANIM swap=%u/%u images=%llu strings=%llu special=%d/%d",applied,prep.slotsWanted,
        static_cast<unsigned long long>(prep.images.size()),static_cast<unsigned long long>(interned.size()),special,kAnimRoleCount-kFirstSpecialRole);
    return applied;
}
unsigned CaptureSetupFunctions() {
    const auto handleOf=reinterpret_cast<GetFunctionHandleFn>(aiProfile.base+kScrGetFunctionHandleRva);
    std::uintptr_t program=0;std::uint32_t size=0;
    if(!Value(aiProfile.base+kScrProgramBufferRva,program) || !Value(aiProfile.base+kScrProgramSizeRva,size) || !program || !size) return 0;
    unsigned found=0;
    for(std::size_t n=0;n<kSetupFunctions.size();++n) {
        const int handle=handleOf(kSetupFunctions[n].file,kSetupFunctions[n].function);
        std::uint8_t op=0xff;
        // A compiled function starts with an opcode the VM dispatch accepts.
        if(handle>0 && static_cast<std::uint32_t>(handle)<size && Value(program+static_cast<std::uint32_t>(handle),op) && op<=0x8b) {
            prep.setup[n]=handle;++found;
        } else Log("ZOMBIE_SETUP function=%s::%s present=0",kSetupFunctions[n].file,kSetupFunctions[n].function);
    }
    return found;
}
unsigned PrecacheModels();
int CaptureHelmetSwap();
void PrepareLevelScripts() {
    char map[96]{};bool launch=false;
    {
        std::lock_guard<std::recursive_mutex> lock(aiMutex);
        RestoreLevelPrep("NEXT_LOAD");
        const bool named=ReadCurrentMapName(map,sizeof(map));
        launch=named && zg.startWanted && !zg.pendingMap.empty() && zg.pendingMap==map;
        const unsigned setup=prep.threads?CaptureSetupFunctions():0u;
        Log("LEVEL_SCRIPTS_LOADED map=%s zombies_launch=%d setup_functions=%u/%llu",named?map:"?",launch,setup,
            static_cast<unsigned long long>(kSetupFunctions.size()));
        if(!launch || !prep.bindings) return;
    }
    // The animation load started at boot; wait for it without holding the
    // AI lock (the render thread keeps drawing the loading screen).
    bo1assets::WaitReady(15000);
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    prep.map=map;
    const auto guards=SpecOpsMapIndex(map)>=0?ApplySpecOpsGuards():0u;
    const auto anims=ApplyAnimSwaps();
    models.helmetSwap=prep.threads?CaptureHelmetSwap():0;
    const auto precached=PrecacheModels();
    if(models.appended) bo1models::GuardLoaded();   // the zone loaded without an error
    prep.prepared=true;
    Log("LEVEL_PREP map=%s so_guards=%u/%llu anims=%u bo1_models=%u zone_loaded=%d",map,guards,static_cast<unsigned long long>(kSpecOpsEndFunctions.size()),anims,
        precached,models.appended);
}
void LevelScriptsLoadedHook() {
    try {PrepareLevelScripts();}
    catch(...) {Log("LEVEL_PREP C++ failure; level continues unprepared");}
    reinterpret_cast<EndLoadFn>(aiProfile.base+kScrEndLoadScriptsRva)();
}
bool InstallLevelScriptHook() {
    if(prep.hookInstalled) return true;
    if(!prep.bindings) return false;
    const auto at=aiProfile.base+kLevelScriptsLoadedJumpRva;
    auto* relay=Relay(at,reinterpret_cast<std::uintptr_t>(&LevelScriptsLoadedHook));
    std::array<std::uint8_t,5> code{};
    if(!relay || !RelativeCall(at,reinterpret_cast<std::uintptr_t>(relay),code)) {Log("LEVEL_PREP hook=RELAY_UNAVAILABLE");return false;}
    code[0]=0xe9;
    CodeEdit edit;edit.at=at&~std::uintptr_t(15);
    if(at-edit.at+code.size()>16 || !ReadAi(edit.at,edit.before.data(),16)) return false;
    edit.after=edit.before;std::memcpy(edit.after.data()+(at-edit.at),code.data(),code.size());
    // Only reached while a level loads; a 16-byte exchange is atomic for it.
    prep.hookInstalled=ExchangeCode(edit,true);
    Log("LEVEL_PREP hook_installed=%d",prep.hookInstalled);
    return prep.hookInstalled;
}
// ---- Test40: BO1 zombie character models ----
// Com_LoadLevelFastFiles loads "so_<mode>_<base>" and then <base> last.
std::string LevelZoneOf(const std::string& map) {
    if(map.rfind("so_",0)==0) {const auto u=map.find('_',3);if(u!=std::string::npos && u+1<map.size()) return map.substr(u+1);}
    return map;
}
struct XZoneInfo {const char* name;int allocFlags;int freeFlags;};
using LoadXAssetsFn=void(*)(XZoneInfo*,unsigned,int);
using ModelIndexFn=int(*)(const char*);
// The level zone of a zombies launch loads together with the generated BO1
// model fastfile (same allocation flags, so the next level frees both).
void LevelZoneLoadHook(XZoneInfo* info,unsigned count,int sync) {
    const auto load=reinterpret_cast<LoadXAssetsFn>(aiProfile.base+kDbLoadXAssetsRva);
    bool add=false;std::string zone;
    try {
        bool wanted=false;
        {
            std::lock_guard<std::recursive_mutex> lock(aiMutex);
            models.appended=false;models.precached=false;models.bodies.clear();models.heads.clear();models.zoneFor.clear();models.firstDressed=0;
            if(count==1 && info && info[0].name) zone=info[0].name;
            wanted=!zone.empty() && info[0].allocFlags==kLevelZoneAllocFlags && zg.startWanted && !zg.pendingMap.empty()
                && LevelZoneOf(zg.pendingMap)==zone;
        }
        if(wanted) {
            add=bo1models::WaitReady(30000);
            std::string skip;
            if(add && !bo1models::GuardAllowsLoad(skip)) add=false;
            if(add) bo1models::GuardBegin(zone);
            const auto st=bo1models::GetStatus();
            Log("BO1_MODELS level_zone=%s append=%d state=%s cached=%d skipped=%s detail=%s",zone.c_str(),add,bo1models::StateText(st.state),st.cached,
                skip.empty()?"-":skip.c_str(),st.detail.c_str());
        }
    } catch(...) {add=false;}
    if(!add) {load(info,count,sync);return;}
    XZoneInfo zones[2]={info[0],{bo1models::kZoneName,info[0].allocFlags,0}};
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);models.appended=true;models.zoneFor=zone;}
    load(zones,2,sync);
}
bool InstallLevelZoneHook() {
    if(models.hookInstalled) return true;
    if(!models.bindings) return false;
    const auto at=aiProfile.base+kLevelZoneLoadCallRva;
    auto* relay=Relay(at,reinterpret_cast<std::uintptr_t>(&LevelZoneLoadHook));
    std::array<std::uint8_t,5> code{};
    if(!relay || !RelativeCall(at,reinterpret_cast<std::uintptr_t>(relay),code)) {Log("BO1_MODELS hook=RELAY_UNAVAILABLE");return false;}
    CodeEdit edit;edit.at=at&~std::uintptr_t(15);
    if(at-edit.at+code.size()>16 || !ReadAi(edit.at,edit.before.data(),16)) return false;
    edit.after=edit.before;std::memcpy(edit.after.data()+(at-edit.at),code.data(),code.size());
    models.hookInstalled=ExchangeCode(edit,true);
    Log("BO1_MODELS hook_installed=%d",models.hookInstalled);
    return models.hookInstalled;
}
// Precache (G_ModelIndex) the zone's models while level.initializing, after
// checking each one is the real converted XModel and model slots remain.
unsigned PrecacheModels() {
    if(!models.appended || !models.bindings) return 0;
    const auto base=aiProfile.base;
    int initializing=0;
    if(!Value(base+kLevelInitializingRva,initializing) || !initializing) {Log("BO1_MODELS precache=0 reason=NOT_INITIALIZING");return 0;}
    std::uint16_t empty=0;unsigned free=0;
    if(!Value(base+kEmptyStringIdRva,empty)) return 0;
    for(unsigned i=1;i<kMaxModels;++i) {std::uint16_t v=0;if(Value(base+kConfigstringsRva+2*(kModelConfigstringBase+i),v) && v==empty) ++free;}
    if(free<16) {Log("BO1_MODELS precache=0 reason=MODEL_SLOTS free=%u",free);return 0;}
    const auto find=reinterpret_cast<FindAssetEntryFn>(base+kDbFindXAssetEntryRva);
    const auto index=reinterpret_cast<ModelIndexFn>(base+kGModelIndexRva);
    auto precache=[&](const char* name)->bool {
        const auto entry=find(kAssetTypeXModel,name);
        std::uintptr_t header=0,namePtr=0;std::uint8_t bones=0;char text[64]{};
        const auto length=std::strlen(name);
        if(!entry || length>=sizeof(text) || !Value(entry+kAssetEntryHeader,header) || !header || !Value(header,namePtr) || !namePtr
            || !ReadAi(namePtr,text,length+1) || std::strcmp(text,name) || !Value(header+8,bones) || !bones) {
            Log("BO1_MODELS model=%s present=0",name);return false;
        }
        return index(name)>0;
    };
    for(const auto* name:bo1models::kBodyAlias) if(precache(name)) models.bodies.emplace_back(name);
    for(const auto* name:bo1models::kHeadAlias) if(precache(name)) models.heads.emplace_back(name);
    models.precached=!models.bodies.empty() && !models.heads.empty();
    Log("BO1_MODELS precache=%d bodies=%llu heads=%llu model_slots_free=%u helmet_swap=%d",models.precached,
        static_cast<unsigned long long>(models.bodies.size()),static_cast<unsigned long long>(models.heads.size()),free,models.helmetSwap!=0);
    return static_cast<unsigned>(models.bodies.size()+models.heads.size());
}
int CaptureHelmetSwap() {
    const auto handleOf=reinterpret_cast<GetFunctionHandleFn>(aiProfile.base+kScrGetFunctionHandleRva);
    std::uintptr_t program=0;std::uint32_t size=0;std::uint8_t op=0xff;
    if(!Value(aiProfile.base+kScrProgramBufferRva,program) || !Value(aiProfile.base+kScrProgramSizeRva,size) || !program || !size) return 0;
    const int handle=handleOf(kHelmetSwapFunction.file,kHelmetSwapFunction.function);
    if(handle>0 && static_cast<std::uint32_t>(handle)<size && Value(program+static_cast<std::uint32_t>(handle),op) && op<=0x8b) return handle;
    Log("BO1_MODELS function=%s::%s present=0",kHelmetSwapFunction.file,kHelmetSwapFunction.function);
    return 0;
}
// character\c_ger_honorguard_zt: a random body (1 in 4 body1) and head,
// the head attached with no tag so its j_spine4/j_neck/j_head follow the body.
// The MW2 helmet is moved off self.hatModel first (helmetPop would look for
// it), then every MW2 attachment is removed.
void DressZombie(ZombieActor& z,const AiEntity& e) {
    if(z.dog || z.dressed || !models.precached) return;
    z.dressed=true;
    const auto ref=ScriptEntityRef(e.number);
    if(models.helmetSwap && prep.threads) {
        auto& p=*reinterpret_cast<volatile NativeScriptParameters*>(aiProfile.base+kScriptParametersRva);
        const auto r=CallScriptThread(p,scriptNatives,reinterpret_cast<ScriptExecEntThread>(aiProfile.base+kScrExecEntThreadNumRva),
            reinterpret_cast<ScriptFreeThread>(aiProfile.base+kScrFreeThreadRva),e.number,models.helmetSwap,{ScriptArg::String(kHelmetSwapNote)});
        if(r==ScriptCallResult::Ok) ++models.helmetSwaps;
        else Log("BO1_MODELS helmet_swap entity=%u result=%s",e.number,ScriptCallText(r));
    }
    const auto& body=models.bodies[static_cast<std::size_t>(ZombieRandom(static_cast<int>(models.bodies.size())))];
    const auto& head=models.heads[static_cast<std::size_t>(ZombieRandom(static_cast<int>(models.heads.size())))];
    if(Builtin(kBuiltinDetachAll,ref,{})!=ScriptCallResult::Ok
        || Builtin(kBuiltinSetModel,ref,{ScriptArg::String(body.c_str())})!=ScriptCallResult::Ok
        || Builtin(kBuiltinAttach,ref,{ScriptArg::String(head.c_str()),ScriptArg::String(""),ScriptArg::Int(1)})!=ScriptCallResult::Ok) {
        if(++models.dressFailures<=8) Log("BO1_MODELS dress entity=%u failed=1 body=%s head=%s",e.number,body.c_str(),head.c_str());
        return;
    }
    ++models.dressed;
    if(!models.firstDressed) models.firstDressed=GetTickCount64();
}
// %anim value (script animtree index << 16 | index in the tree) for one of the
// replaced flashbang slots, in the tree this actor animates with.
std::optional<std::uint32_t> AnimValue(const AiEntity& e,AnimRole role) {
    const auto r=static_cast<std::size_t>(role);
    if(!prep.prepared || !prep.roleParts[r]) return std::nullopt;
    const auto base=aiProfile.base;
    const auto tree=reinterpret_cast<EntAnimTreeFn>(base+kEntAnimTreeRva)(e.address);
    std::uintptr_t anims=0;
    if(!tree || !Value(tree,anims) || !anims) return std::nullopt;
    auto& t=prep.trees[anims];
    if(!t.valid && t.tree==-1) {
        t.tree=-2;t.index.fill(-1);
        std::uint32_t size=0;
        if(!Value(anims,size) || !size || size>16384) {Log("BO1_ANIM tree=0x%llx size=%u refused=1",static_cast<unsigned long long>(anims),size);return std::nullopt;}
        std::vector<std::uint8_t> entries(static_cast<std::size_t>(size)*kXAnimEntryStride);
        if(!ReadAi(anims+kXAnimEntries,entries.data(),entries.size())) return std::nullopt;
        for(std::uint32_t i=0;i<size;++i) {
            const auto* entry=entries.data()+static_cast<std::size_t>(i)*kXAnimEntryStride;
            if(bo1xanim::Get<std::uint16_t>(entry)!=0) continue;     // only leaf anims
            const auto parts=bo1xanim::Get<std::uint64_t>(entry+kXAnimEntryParts);
            for(int k=0;k<kAnimRoleCount;++k) if(prep.roleParts[static_cast<std::size_t>(k)]==parts && t.index[static_cast<std::size_t>(k)]<0) t.index[static_cast<std::size_t>(k)]=static_cast<int>(i);
        }
        const int index=reinterpret_cast<AnimTreeIndexFn>(base+kAnimTreeIndexRva)(anims);
        if(index>=0 && index<0x10000 && reinterpret_cast<AnimTreeByIndexFn>(base+kAnimTreeByIndexRva)(index)==anims) {t.tree=index;t.valid=true;}
        int leaves=0;
        for(int k=kFirstSpecialRole;k<kAnimRoleCount;++k) if(t.index[static_cast<std::size_t>(k)]>=0) ++leaves;
        Log("BO1_ANIM tree=0x%llx script_tree=%d size=%u special_leaves=%d/%d",static_cast<unsigned long long>(anims),t.tree,size,
            leaves,kAnimRoleCount-kFirstSpecialRole);
    }
    if(!t.valid || t.index[r]<0) return std::nullopt;
    return (static_cast<std::uint32_t>(t.tree)<<16)|static_cast<std::uint32_t>(t.index[r]);
}
int AnimMilliseconds(AnimRole role,int fallback) {
    const auto* a=RoleAnim(role);
    return a && a->lengthSeconds()>0.f?static_cast<int>(a->lengthSeconds()*1000.f):fallback;
}
// do_zombie_rise: walkers pick rise version 1 or 2 at random (runners and
// sprinters always 1); the anim starts under the spawn spot (45 or 14 units),
// facing the player, hidden for the first 0.5 s like BO1's hide_pop.
void StartRise(ZombieActor& z,const AiEntity& e,const AiPoint& player,int time) {
    if(z.dog) return;
    int version=1;
    if(z.speed==bo1::MoveSpeed::Walk && ZombieRandom(2)) version=2;
    auto role=z.speed==bo1::MoveSpeed::Walk?(version==2?AnimRole::RiseWalk2:AnimRole::RiseWalk):
        z.speed==bo1::MoveSpeed::Run?AnimRole::RiseRun:AnimRole::RiseSprint;
    auto value=AnimValue(e,role);
    if(!value && version==2) {version=1;role=AnimRole::RiseWalk;value=AnimValue(e,role);}
    if(!value) return;
    const auto ref=ScriptEntityRef(e.number);
    AiPoint origin=e.origin;origin[2]-=bo1::RiseDepth(version);
    const float yaw=std::atan2(player[1]-e.origin[1],player[0]-e.origin[0])*57.29578f;
    Builtin(kBuiltinHide,ref,{});
    const auto r=Builtin(kBuiltinAnimScripted,ref,{ScriptArg::String("zombie_rise"),ScriptArg::Vector(origin),ScriptArg::Vector({0.f,yaw,0.f}),ScriptArg::Anim(*value)});
    if(r!=ScriptCallResult::Ok) {Builtin(kBuiltinShow,ref,{});++zg.riseFailures;return;}
    z.risingUntil=time+AnimMilliseconds(role,3000)+150;z.showAt=time+bo1::kRiseShowMilliseconds;z.rose=true;++zg.rises;
}
// zombie_spawn_init equivalents that MW2 keeps in script variables: no
// weapon, no pain, arrivals, exits, long deaths, surprise, bullet or danger
// reactions, no battle chatter; sprinters use the sprint animset.
void RunZombieSetup(ZombieActor& z,const AiEntity& e) {
    z.scriptSetup=true;
    if(z.dog || !prep.threads) return;
    unsigned ok=0;
    for(const auto f:{SetupFunction::GunRemove,SetupFunction::DisableArrivals,SetupFunction::DisableExits,SetupFunction::DisablePain,
        SetupFunction::DisableLongDeath,SetupFunction::DisableSurprise,SetupFunction::DisableWhizby,SetupFunction::DisableDangerReact})
        if(RunSetupFunction(e.number,f)) ++ok;
    if(RunSetupFunction(e.number,SetupFunction::BattleChatterOff,{ScriptArg::Int(0)})) ++ok;
    if(z.speed==bo1::MoveSpeed::Sprint && RunSetupFunction(e.number,SetupFunction::EnableSprint)) ++ok;
    if(ok) ++zg.setupZombies;
}
int ZombieMultiplier(int time) {return time<zg.doublePointsUntil?2:1;}
void AwardPoints(int points,int time) {
    if(points<=0)return;
    zg.ledger.add(points);zg.lastDelta=points;zg.deltaTime=time;
}
// World-only point trace (CONTENTS_SOLID): actors and the player never block it.
AiTraceResult WorldTrace(const AiPoint& start,const AiPoint& end) {
    alignas(16) std::array<std::uint8_t,48> output{};
    if(!FinitePoint(start) || !FinitePoint(end))return {};
    reinterpret_cast<TraceFn>(aiProfile.trace)(output.data(),start.data(),end.data(),&kAiPointBounds,0x7ff,1);
    return DecodeAiTrace(output);
}
bo1::BodyPart PartFromHit(int hit) {
    switch(hit) {
        case kHitHelmet: return bo1::BodyPart::Helmet;
        case kHitHead: return bo1::BodyPart::Head;
        case kHitNeck: return bo1::BodyPart::Neck;
        case kHitTorsoUpper: return bo1::BodyPart::TorsoUpper;
        case kHitTorsoLower: return bo1::BodyPart::TorsoLower;
        case kHitNone: return bo1::BodyPart::None;
        default: return bo1::BodyPart::Limb;
    }
}
bo1::DamageKind KindFromMod(int mod) {
    if(mod==kModMelee)return bo1::DamageKind::Melee;
    if(mod==kModGrenade || mod==kModGrenadeSplash || mod==kModProjectile || mod==kModProjectileSplash || mod==kModExplosive)return bo1::DamageKind::Explosive;
    if(mod==kModUnknown)return bo1::DamageKind::Unknown;
    return bo1::DamageKind::Bullet;
}
ZombieActor* FindZombie(std::uintptr_t entity) {
    for(auto& z:zg.zombies)if(z.identity.address==entity)return &z;
    return nullptr;
}
void QueueDrop(bo1::Powerup type,const AiPoint& at,int time) {
    // zombie_death_points: ground trace below the zombie, powerup 40 units up.
    AiPoint start=at,end=at;start[2]+=5.f;end[2]-=300.f;
    const auto ground=WorldTrace(start,end);
    AiPoint origin=at;
    if(ground.valid && !ground.startSolid)origin[2]=start[2]+(end[2]-start[2])*ground.fraction;
    origin[2]+=bo1::kPowerupHeight;
    if(zg.drops.size()>=8)zg.drops.erase(zg.drops.begin());
    zg.drops.push_back({type,origin,time});
    Log("ZOMBIE_POWERUP_DROP type=%s origin=%.1f,%.1f,%.1f drops_this_round=%d score_to_drop=%d",
        bo1::PowerupText(type),origin[0],origin[1],origin[2],zg.powerups.dropsThisRound(),zg.powerups.scoreToDrop());
}
// Called from the damage hook (server thread) for player damage to a zombie.
// Mirrors actor_damage_override/zombie_damage: hit points on every hit,
// insta-kill, grenade/explosive bonus damage, then kill points and drops.
bool ZombieDamage(std::uintptr_t target,std::uintptr_t inflictor,std::uintptr_t attacker,const float* direction,const float* point,
    int damage,int flags,int mod,int weapon,int hitloc,int time,int surface,bool& handled) {
    handled=false;
    if(!zg.running || !target || attacker!=aiProfile.fixedPool)return false;
    auto* z=FindZombie(target);
    if(!z)return false;
    int health=0,level=0;
    if(!Value(target+kAiHealth,health) || health<=0 || !Value(aiProfile.timeSlot,level))return false;
    handled=true;z->damaged=true;
    const bool insta=level<zg.instaKillUntil;
    auto kind=KindFromMod(mod);
    const int round=zg.controller.status().round;
    if(mod==kModGrenade || mod==kModGrenadeSplash)damage+=round+100+ZombieRandom(100);
    else if(mod==kModProjectile || mod==kModProjectileSplash || mod==kModExplosive)damage+=round*ZombieRandom(100);
    // check_for_instakill: a hit that would not kill becomes DoDamage(health*10)
    // with a stripped (unknown) means of death; outright kills keep their own.
    if(insta && damage<health) {damage=health*10;kind=bo1::DamageKind::Unknown;}
    const bool result=originalDamage(target,inflictor,attacker,direction,point,damage,flags,mod,weapon,hitloc,time,surface);
    if(!result)return result;
    ++zg.hits;
    const int multiplier=ZombieMultiplier(level);
    AwardPoints(bo1::DamagePoints(multiplier),level);
    int after=1;
    if(Value(target+kAiHealth,after) && after<=0) {
        const auto part=PartFromHit(hitloc);
        AwardPoints(bo1::KillPoints(kind,part,insta,multiplier),level);
        ++zg.kills;if(part==bo1::BodyPart::Head || part==bo1::BodyPart::Helmet)++zg.headshots;
        AiPoint origin{};
        if(ReadAi(target+kAiOrigin,origin.data(),sizeof(origin)) && FinitePoint(origin))
            if(const auto drop=zg.powerups.zombieDied(ZombieRng()))QueueDrop(*drop,origin,level);
    }
    return result;
}
// USP .45 first (design contract), then any loaded pistol; BO1's lethal
// grenade is the frag grenade when the level loaded one.
void ChooseLoadout() {
    const auto weapons=LoadedWeapons();
    zg.pistol.clear();zg.grenade.clear();
    auto find=[&](const char* name){return std::find_if(weapons.begin(),weapons.end(),[&](const auto& w){return w.name==name;});};
    for(const char* name:{"usp","usp_silencer","usp_fmj","usp_tactical"})
        if(auto it=find(name);it!=weapons.end() && it->inventoryType==0) {zg.pistol=it->name;break;}
    if(zg.pistol.empty())for(const auto& w:weapons)
        if(w.inventoryType==0 && w.weaponClass==5 && w.clipSize>0 && w.name.find("akimbo")==std::string::npos) {zg.pistol=w.name;break;}
    for(const char* name:{"fraggrenade","frag_grenade_mp","frag_grenade"})
        if(auto it=find(name);it!=weapons.end()) {zg.grenade=it->name;break;}
}
bool ApplyLoadout() {
    ChooseLoadout();
    if(zg.pistol.empty()) {Log("ZOMBIE_LOADOUT pistol=NONE kept_campaign_weapons=1");return true;}
    if(Builtin(kBuiltinTakeAllWeapons,0,{})!=ScriptCallResult::Ok)return false;
    Builtin(kBuiltinGiveWeapon,0,{ScriptArg::String(zg.pistol.c_str())});
    // BO1 starting pistol: full magazine and 80 reserve rounds (M1911 stock).
    Builtin(kBuiltinSetWeaponAmmoStock,0,{ScriptArg::String(zg.pistol.c_str()),ScriptArg::Int(80)});
    Builtin(kBuiltinSwitchToWeapon,0,{ScriptArg::String(zg.pistol.c_str())});
    if(!zg.grenade.empty()) {
        Builtin(kBuiltinGiveWeapon,0,{ScriptArg::String(zg.grenade.c_str())});
        Builtin(kBuiltinSetWeaponAmmoClip,0,{ScriptArg::String(zg.grenade.c_str()),ScriptArg::Int(0)});
    }
    Log("ZOMBIE_LOADOUT pistol=%s grenade=%s stock=80 method=NATIVE_BUILTINS",zg.pistol.c_str(),zg.grenade.empty()?"NONE":zg.grenade.c_str());
    return true;
}
// award_grenades_for_survivors: 0 -> 2, 1 -> 3, otherwise 4 (BO1 max 4).
void AwardGrenades() {
    if(zg.grenade.empty())return;
    ScriptReturn current;
    if(Builtin(kBuiltinGetWeaponAmmoClip,0,{ScriptArg::String(zg.grenade.c_str())},&current)!=ScriptCallResult::Ok || !current.present)return;
    const int have=current.value.type==kScriptInteger?current.asInt():0;
    const int give=have<=0?2:have==1?3:4;
    if(give>have)Builtin(kBuiltinSetWeaponAmmoClip,0,{ScriptArg::String(zg.grenade.c_str()),ScriptArg::Int(give)});
}
void MaxAmmo() {
    std::array<std::uint32_t,15> owned{};
    if(!ReadAi(ai.client+kOwnedWeapons,owned.data(),sizeof(owned)))return;
    const auto weapons=LoadedWeapons();
    for(const auto id:owned) {
        if(!id)continue;
        const auto it=std::find_if(weapons.begin(),weapons.end(),[&](const auto& w){return w.index==id;});
        if(it!=weapons.end())Builtin(kBuiltinGiveMaxAmmo,0,{ScriptArg::String(it->name.c_str())});
    }
}
void DeleteActor(const AiEntity& e) {
    int protectedNumber=-1;
    if(!Value(aiProfile.protectedSlot,protectedNumber) || protectedNumber==e.number)return;
    RecordNativeOperation("ZOMBIE_CAMPAIGN_DELETE",e.number);
    reinterpret_cast<DeleteFn>(aiProfile.remove)(e.number);
}
// Remove campaign actors (never ours): enemies always, allies when chosen.
void ClearCampaignActors(const Snapshot& s,const AiScope& scope) {
    int count=0;if(!Value(aiProfile.countSlot,count) || count<3 || count>kAiEntityCapacity)return;
    const int ally=PlayerTeam(s);
    const auto epoch=ai.checkpointEpoch.load();
    unsigned removed=0;
    for(int n=2;n<count && removed<16;++n) {
        const auto e=ReadAiEntity(scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride,n,ReadAi);
        if(!e.valid || e.type!=kActorType || e.health<=0)continue;
        if(std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==e.address;}))continue;
        if(e.team==ally && !zg.settings.removeAllies)continue;
        if(e.team!=kAxisTeam && e.team!=kAlliesTeam)continue;
        DeleteActor(e);
        if(epoch!=ai.checkpointEpoch)return;
        const auto after=ReadAiAllocation(AiIdentity::capture(e),ReadAi);
        if(!SameAiAllocation(AiIdentity::capture(e),after) || after.type!=kActorType) {++removed;++zg.campaignRemoved;}
    }
    if(removed)Log("ZOMBIE_CAMPAIGN_CLEAR removed=%u total=%u allies_removed=%d",removed,zg.campaignRemoved,zg.settings.removeAllies);
}
template<class T> void SetField(std::uintptr_t at,T value) {
    T current{};if(!Value(at,current) || current==value)return;
    if constexpr(sizeof(T)==1)SetByte(at,static_cast<std::uint8_t>(current),static_cast<std::uint8_t>(value));
    else if constexpr(std::is_same_v<T,float>)SetFloat(at,current,value);
    else SetCount(at,static_cast<int>(current),static_cast<int>(value));
}
// zombie_spawn_init + zombie_setup_attack_properties, translated to native
// actor fields. Zombies keep ignoreall: MW2's soldier combat scripts would
// shoot, so pursuit and the melee rule are driven from here instead.
void ConfigureZombie(const AiEntity& e,int health,bool dog,bo1::MoveSpeed speed) {
    int current=0;
    if(Value(e.address+kAiHealth,current))SetCount(e.address+kAiHealth,current,health);
    if(Value(e.address+kEntityMaxHealth,current))SetCount(e.address+kEntityMaxHealth,current,health);
    SetField<std::uint8_t>(e.sentient+kSentientIgnoreAll,dog?0:1);
    SetField<std::uint8_t>(e.sentient+kSentientIgnoreMe,0);
    SetField<int>(e.actor+kActorGrenadeAmmo,0);
    SetField<float>(e.actor+kActorGrenadeAwareness,0.f);
    SetField<int>(e.actor+kActorDropWeapon,0);
    SetField<std::uint8_t>(e.actor+kActorAllowPain,0);       // a.disablepain
    SetField<std::uint8_t>(e.actor+kActorAllowDeath,1);      // dies inside animscripted (rise, attacks)
    SetField<int>(e.actor+kActorIgnoreSuppression,1);
    SetField<std::uint8_t>(e.actor+kActorNoDodgeMove,1);
    SetField<float>(e.actor+kActorPathEnemyLookahead,0.f);
    SetField<float>(e.actor+kActorPathEnemyFightDist,bo1::kZombiePathEnemyFightDist);
    SetField<float>(e.actor+kActorMeleeAttackDist,bo1::kZombieMeleeAttackDist);
    SetField<float>(e.actor+kActorMaxSightDistSq,bo1::kZombieMaxSightDistSq);
    SetField<float>(e.actor+kActorGoalRadius,bo1::kZombieGoalRadius);
    SetField<float>(e.actor+kActorWalkDist,speed==bo1::MoveSpeed::Walk?100000.f:0.f);
    ClearFixedNode(e);
}
// level.enemy_spawns: campaign enemy spawn locations; maps without enemy
// spawners (e.g. the museum) fall back to every human spawner.
const std::vector<AiEntity>& ZombieSpawnPoints() {return ai.enemyPoints.empty()?ai.allPoints:ai.enemyPoints;}
std::vector<AiEntity> SpawnPointBand(const AiPoint& player,float nearest,float farthest) {
    std::vector<AiEntity> band;
    for(const auto& p:ZombieSpawnPoints()) {
        const float d=PointDistanceSquared(p.origin,player);
        if(d>=nearest*nearest && d<=farthest*farthest)band.push_back(p);
    }
    return band;
}
bool SpawnZombie(const bo1::SpawnRequest& request,const Snapshot& s,const AiScope& scope,int time) {
    AiPoint player{};
    if(!ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player))return false;
    const auto& templates=request.dogs?ai.dogPoints:ai.enemies.empty()?ai.friends:ai.enemies;
    if(templates.empty() || ZombieSpawnPoints().empty()) {
        static int lastLog=-100000;
        if(time-lastLog>5000) {lastLog=time;Log("ZOMBIE_SPAWN refused=%s dogs=%d",templates.empty()?"NO_TEMPLATE":"NO_SPAWN_POINT",request.dogs);}
        return false;
    }
    // level.enemy_spawns: campaign enemy spawn locations near the player.
    auto band=SpawnPointBand(player,request.dogs?400.f:400.f,request.dogs?800.f:2500.f);
    if(band.size()<2)band=SpawnPointBand(player,256.f,4000.f);
    if(band.empty()) {band=ZombieSpawnPoints();if(band.size()>8)band.resize(8);}
    const int count=static_cast<int>(band.size());
    const int draw=ZombieRandom(count);
    int previous=-1;
    for(int n=0;n<count;++n)if(band[n].number==zg.previousSpawner)previous=n;
    const auto pick=bo1::ChooseSpawner(count,previous,draw,ZombieRandom(count));
    if(!pick)return false;
    const auto point=band[*pick];
    const auto& source=templates[static_cast<std::size_t>(ZombieRandom(static_cast<int>(templates.size())))];
    AiRequest spawn;spawn.operation=AiOperation::SpawnEnemy;spawn.source=AiIdentity::capture(source);
    AiIdentity created;
    SpawnOptions options;options.center=&point.origin;options.zombie=true;options.dog=request.dogs;
    options.bypassPause=true;options.setEnemy=request.dogs;options.created=&created;
    SpawnOwned(spawn,s,scope,time,nullptr,&options);
    if(ai.status.lastResult!=AiRequestResult::Created || !created.address)return false;
    zg.previousSpawner=point.number;
    const auto e=Entity(created,scope.pool,false);
    if(!e.valid)return false;
    const auto speed=request.dogs?bo1::MoveSpeed::Sprint:bo1::RunSpeed(bo1::ZombieMoveSpeed(request.round),ZombieRng());
    ConfigureZombie(e,request.health,request.dogs,speed);
    ZombieActor z;z.identity=created;z.ticket=request.ticket;z.dog=request.dogs;z.speed=speed;z.spawned=time;
    z.health=request.health;z.reconfigureAt=time+300;
    z.nextRepath=time;z.failsafeAt=time+static_cast<int>(bo1::kFailsafeIntervalMilliseconds);z.failsafeOrigin=e.origin;
    zg.zombies.push_back(z);
    DressZombie(zg.zombies.back(),e);
    StartRise(zg.zombies.back(),e,player,time);
    Log("ZOMBIE_SPAWN entity=%u round=%d health=%d speed=%s dog=%d point=%u distance=%.0f template=%s rise=%d alive=%d queued=%d",
        e.number,request.round,request.health,bo1::MoveSpeedText(speed),request.dogs,point.number,
        std::sqrt(PointDistanceSquared(point.origin,player)),source.classname.c_str(),zg.zombies.back().rose,zg.controller.status().alive+1,zg.controller.status().queued-1);
    return true;
}
float YawDegrees(const AiPoint& from,const AiPoint& to) {return std::atan2(to[1]-from[1],to[0]-from[0])*57.29578f;}
float AngleDelta(float a,float b) {float d=std::fmod(a-b,360.f);if(d>180.f)d-=360.f;if(d<-180.f)d+=360.f;return d;}
// zombie_melee.gsc MeleeCombat: admission by the BO1 rule, a random attack
// from _zombie_melee plus the walk or run set, one hit check per "fire"
// notetrack, broken off when the target leaves melee range (the engine ends
// the melee animscript then), and a new swing while it stays in reach.
void EndAttack(ZombieActor& z,const AiEntity& e) {
    z.attacking=false;
    // A looping run attack never ends by itself; stopanimscripted is a no-op
    // when the scripted anim has already finished.
    if(z.animatedAttack) Builtin(kBuiltinStopAnimScripted,ScriptEntityRef(e.number),{});
    z.animatedAttack=false;
}
void MeleeHit(ZombieActor& z,const AiEntity& e,const Snapshot& s,const AiPoint& player,int playerHealth,float distance,int time) {
    if(playerHealth>0 && distance<=bo1::kAttackHitRangeSquared) {
        AiPoint dir{player[0]-e.origin[0],player[1]-e.origin[1],0.f};
        const float length=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]);
        if(length>0.f) {dir[0]/=length;dir[1]/=length;}
        AiPoint hit=player;hit[2]+=48.f;
        RecordNativeOperation("ZOMBIE_MELEE_BEGIN",e.number);
        reinterpret_cast<DamageFn>(aiProfile.damage)(s.entity,e.address,e.address,dir.data(),hit.data(),
            bo1::kZombieMeleeDamage,0,kModMelee,-1,kHitTorsoUpper,0,0);
        RecordNativeOperation("ZOMBIE_MELEE_END",e.number);
        ++zg.meleeHits;
    } else {z.lastMiss=time;++zg.meleeMisses;}
}
void ServiceMelee(ZombieActor& z,const AiEntity& e,const Snapshot& s,const AiPoint& player,int playerHealth,int time) {
    if(z.dog)return;   // MW2 dogs attack with their own native animscripts
    const float distance=PointDistanceSquared(e.origin,player);
    if(z.attacking) {
        const int elapsed=time-z.attackStart;
        const auto epoch=ai.checkpointEpoch.load();
        while(z.nextFire<z.fireCount && elapsed>=z.fireMs[z.nextFire]) {
            ++z.nextFire;
            MeleeHit(z,e,s,player,playerHealth,distance,time);
            if(epoch!=ai.checkpointEpoch)return;
        }
        if(elapsed>=z.attackEndMs) EndAttack(z,e);
        else if(playerHealth<=0 || bo1::BreakOffAttack(elapsed,distance)) {++zg.attacksBrokenOff;EndAttack(z,e);}
        return;
    }
    if(playerHealth<=0 || distance>bo1::kLongChargeRangeSquared)return;
    AiPoint angles{};
    if(!ReadAi(e.address+kEntityAngles,angles.data(),sizeof(angles)) || !FinitePoint(angles))return;
    bo1::MeleeInputs inputs;inputs.sentient=true;inputs.alive=true;
    inputs.yawDegrees=AngleDelta(YawDegrees(e.origin,player),angles[1]);inputs.distanceSquared=distance;
    inputs.millisecondsSinceMiss=time-z.lastMiss;
    // Path: knee then eye world trace (zombie_melee CanMeleeInternal fallback).
    AiPoint from=e.origin,to=player;from[2]+=18.f;to[2]+=18.f;
    auto clear=WorldTrace(from,to);
    if(!(clear.valid && !clear.startSolid && clear.fraction>=1.f)) {from[2]+=42.f;to[2]+=42.f;clear=WorldTrace(from,to);}
    inputs.pathClear=clear.valid && !clear.startSolid && clear.fraction>=1.f;
    if(bo1::CanMeleePlayer(inputs,bo1::MeleeCheck::Normal)!=bo1::MeleeDecision::Allowed) return;
    z.attacking=true;z.attackStart=time;z.nextFire=0;z.animatedAttack=false;
    z.fireMs[0]=static_cast<int>(bo1::kAttackFireMilliseconds);z.fireCount=1;z.attackEndMs=static_cast<int>(bo1::kAttackEndMilliseconds);
    const float yaw=YawDegrees(e.origin,player);
    // pick_zombie_melee_anim: _zombie_melee plus _zombie_walk_melee for
    // walkers or _zombie_run_melee for runners and sprinters.
    constexpr std::size_t kChoices=std::size(kMeleeAnims)+std::size(kWalkMeleeAnims);
    std::array<AnimRole,kChoices> choices{};std::array<std::uint32_t,kChoices> values{};std::size_t count=0;
    auto offer=[&](AnimRole role){if(const auto v=AnimValue(e,role)) {choices[count]=role;values[count]=*v;++count;}};
    for(const auto r:kMeleeAnims) offer(r);
    if(z.speed==bo1::MoveSpeed::Walk) {for(const auto r:kWalkMeleeAnims) offer(r);}
    else for(const auto r:kRunMeleeAnims) offer(r);
    if(count) {
        const auto pick=static_cast<std::size_t>(ZombieRandom(static_cast<int>(count)));
        const auto role=choices[pick];
        if(Builtin(kBuiltinAnimScripted,ScriptEntityRef(e.number),{ScriptArg::String("zombie_attack"),ScriptArg::Vector(e.origin),
            ScriptArg::Vector({0.f,yaw,0.f}),ScriptArg::Anim(values[pick])})==ScriptCallResult::Ok) {
            const auto* a=RoleAnim(role);
            z.attackEndMs=AnimMilliseconds(role,z.attackEndMs);
            if(a) {
                const auto fires=bo1::AttackFireTimes(a->notify,z.attackEndMs,z.fireMs.data(),z.fireMs.size());
                if(fires) z.fireCount=fires;
            }
            z.animatedAttack=true;++zg.animatedAttacks;
            return;
        }
    }
    // Without the imported anims: OrientMode("face angle", yaw) as the BO1
    // attack loop does, with the adapter fire/end times.
    Builtin(kBuiltinOrientMode,ScriptEntityRef(e.number),{ScriptArg::String("face angle"),ScriptArg::Float(yaw)});
}
void GrabPowerup(const ZombieDrop& drop,int time) {
    Log("ZOMBIE_POWERUP_GRAB type=%s age_ms=%d",bo1::PowerupText(drop.type),time-drop.spawned);
    ZombieEvent(bo1::PowerupText(drop.type),time);
    Announce(bo1::PowerupText(drop.type));
    switch(drop.type) {
        case bo1::Powerup::DoublePoints: zg.doublePointsUntil=time+static_cast<int>(bo1::kPowerupDurationMilliseconds); break;
        case bo1::Powerup::InstaKill: zg.instaKillUntil=time+static_cast<int>(bo1::kPowerupDurationMilliseconds); break;
        case bo1::Powerup::MaxAmmo: MaxAmmo(); break;
        case bo1::Powerup::Nuke: {
            // nuke_powerup: closest first, 0.1-0.7 s apart, after 0.5 s.
            std::vector<std::pair<float,ZombieActor*>> order;
            for(auto& z:zg.zombies) {
                const auto e=Entity(z.identity,ai.pool,false);
                if(e.valid)order.push_back({PointDistanceSquared(e.origin,drop.origin),&z});
            }
            std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            int at=time+500;
            for(auto& [d,z]:order) {(void)d;at+=100+ZombieRandom(601);z->nukeAt=at;}
            zg.nukePointsAt=at;
            break;
        }
        default: break;
    }
}
void ServicePowerups(const AiPoint& player,int time) {
    if(time>=zg.nextWatch) {zg.nextWatch=time+static_cast<int>(bo1::kPowerupWatchMilliseconds);zg.powerups.watch(zg.ledger.total);}
    if(zg.nukePointsAt>=0 && time>=zg.nukePointsAt) {zg.nukePointsAt=-1;AwardPoints(bo1::NukePoints(ZombieMultiplier(time)),time);}
    for(auto it=zg.drops.begin();it!=zg.drops.end();) {
        const int age=time-it->spawned;
        if(age>=bo1::PowerupLifetimeMilliseconds()) {Log("ZOMBIE_POWERUP_TIMEOUT type=%s",bo1::PowerupText(it->type));it=zg.drops.erase(it);continue;}
        if(PointDistanceSquared(player,it->origin)<bo1::kPowerupGrabRangeSquared) {const auto drop=*it;it=zg.drops.erase(it);GrabPowerup(drop,time);continue;}
        ++it;
    }
}
void ServiceZombieActors(const Snapshot& s,const AiScope& scope,const AiPoint& player,int playerHealth,int time) {
    const auto epoch=ai.checkpointEpoch.load();
    const auto& c=zg.controller.status();
    for(std::size_t n=0;n<zg.zombies.size();) {
        auto& z=zg.zombies[n];
        const auto raw=ReadAiAllocation(z.identity,ReadAi);
        const auto e=Entity(z.identity,scope.pool,false);
        if(!SameAiAllocation(z.identity,raw) || raw.type!=kActorType || !e.valid || e.type!=kActorType || e.health<=0) {
            if(z.dog) {zg.lastDogOrigin=z.lastOrigin;zg.lastDogKnown=true;}
            zg.controller.retire(z.ticket,bo1::Retirement::Killed);
            zg.zombies.erase(zg.zombies.begin()+static_cast<std::ptrdiff_t>(n));continue;
        }
        // Native initialization may replace the actor brain in the same entity.
        if(!z.identity.matches(e)) {z.identity=AiIdentity::capture(e);Log("ZOMBIE_BIND refreshed entity=%u",e.number);}
        if(z.nukeAt>=0 && time>=z.nukeAt) {
            z.nukeAt=-1;
            reinterpret_cast<DamageFn>(aiProfile.damage)(e.address,0,0,nullptr,e.origin.data(),e.health+666,0,kModUnknown,-1,0,0,0);
            if(epoch!=ai.checkpointEpoch)return;
            ++n;continue;
        }
        z.lastOrigin=e.origin;
        // Campaign spawn functions run in the VM after the native spawn and may
        // set health/goal fields; apply the BO1 setup once more before any hit,
        // then the script-side setup once the actor's animscripts initialized.
        if(!z.reconfigured && time>=z.reconfigureAt) {
            z.reconfigured=true;
            if(!z.damaged)ConfigureZombie(e,z.health,z.dog,z.speed);
            RunZombieSetup(z,e);
            if(epoch!=ai.checkpointEpoch)return;
        }
        // do_zombie_rise: no pursuit, attack or failsafe while climbing out.
        if(z.showAt>=0 && time>=z.showAt) {z.showAt=-1;Builtin(kBuiltinShow,ScriptEntityRef(e.number),{});}
        if(time<z.risingUntil) {
            z.failsafeAt=time+static_cast<int>(bo1::kFailsafeIntervalMilliseconds);z.failsafeOrigin=e.origin;
            ++n;continue;
        }
        // The rise anim has ended; make sure the actor left scripted mode.
        if(z.rose && !z.riseStopped) {z.riseStopped=true;Builtin(kBuiltinStopAnimScripted,ScriptEntityRef(e.number),{});}
        // zombie_follow_enemy: SetGoalPos(favoriteenemy.origin) on BO1's cadence.
        const float distance=PointDistanceSquared(e.origin,player);
        if(time>=z.nextRepath) {
            z.nextRepath=time+static_cast<int>(bo1::FollowRepathMilliseconds(distance,ZombieRng()));
            AiPoint goal{};
            if(!ReadAi(e.actor+kActorGoalPosition,goal.data(),sizeof(goal)) || PointDistanceSquared(goal,player)>8.f*8.f)
                reinterpret_cast<GoalFn>(aiProfile.setGoal)(e.actor,player.data(),0);
            float radius=0.f;
            if(Value(e.actor+kActorGoalRadius,radius) && radius!=bo1::kZombieGoalRadius)SetFloat(e.actor+kActorGoalRadius,radius,bo1::kZombieGoalRadius);
            if(!z.dog)SetField<std::uint8_t>(e.sentient+kSentientIgnoreAll,1);
        }
        if(!z.sprintApplied && bo1::LastZombieSpeedUp(c.round,c.queued,c.alive) && z.speed==bo1::MoveSpeed::Walk) {
            z.sprintApplied=true;z.speed=bo1::MoveSpeed::Sprint;SetField<float>(e.actor+kActorWalkDist,0.f);
            Log("ZOMBIE_SPEED_UP entity=%u",e.number);
        }
        ServiceMelee(z,e,s,player,playerHealth,time);
        if(epoch!=ai.checkpointEpoch)return;
        // round_spawn_failsafe: no progress for 30 s and not attacking -> respawn.
        if(time>=z.failsafeAt) {
            if(!z.attacking && PointDistanceSquared(e.origin,z.failsafeOrigin)<bo1::kFailsafeMinimumMoveSquared
                && distance>bo1::kLongChargeRangeSquared) {
                Log("ZOMBIE_FAILSAFE entity=%u recycled=1",e.number);
                zg.controller.retire(z.ticket,bo1::Retirement::Recycle);
                DeleteActor(e);
                if(epoch!=ai.checkpointEpoch)return;
                zg.zombies.erase(zg.zombies.begin()+static_cast<std::ptrdiff_t>(n));continue;
            }
            z.failsafeAt=time+static_cast<int>(bo1::kFailsafeIntervalMilliseconds);z.failsafeOrigin=e.origin;
        }
        ++n;
    }
}
void ResetZombieGame(const char* reason,bool deleteActors) {
    // A pending start (e.g. a map launched from the menu) survives loads; only
    // a running game ends here. RequestZombieStop cancels a pending start.
    zg.stopWanted=false;
    if(!zg.running)return;
    const bool wasRunning=zg.running;
    if(wasRunning) {
        zg.gameOverRound=zg.controller.status().round;
        Log("ZOMBIE_END reason=%s round=%d points=%d total=%d kills=%u headshots=%u hits=%u melee_hits=%u melee_misses=%u campaign_removed=%u",
            reason,zg.gameOverRound,zg.ledger.score,zg.ledger.total,zg.kills,zg.headshots,zg.hits,zg.meleeHits,zg.meleeMisses,zg.campaignRemoved);
    }
    zg.controller.stop(bo1::StopReason::Manual);
    zg.running=false;zg.drops.clear();
    if(deleteActors && !zg.zombies.empty())CleanupOwned("ZOMBIES_END",false,true);
    zg.zombies.clear();
    for(auto& x:ai.owned)x.zombie=false;
    if(zg.restorePause) {ai.pauseWanted=zg.pauseBefore;zg.restorePause=false;}
    ZombieEvent(wasRunning?"GAME OVER":"",zg.lastTime);
    // Spec Ops end screens are disabled on a prepared level, so GAME OVER
    // loads the same map again for a new game (F4 -> Start cancels it).
    if(!std::strcmp(reason,"PLAYER_DIED") && zg.settings.autoRestart && !zg.launchedMap.empty()) {
        zg.relaunchAt=GetTickCount64()+9000;zg.launchSettings=zg.settings;
        Log("ZOMBIE_RELAUNCH scheduled map=%s in_ms=9000",zg.launchedMap.c_str());
    }
}
void BeginZombies(const Snapshot& s,const AiScope& scope,int time) {
    zg.settings=zg.requested;zg.startWanted=false;
    zg.random=static_cast<std::uint32_t>(GetTickCount64()) | 1u;
    zg.ledger={};zg.kills=zg.headshots=zg.hits=zg.campaignRemoved=zg.meleeHits=zg.meleeMisses=0;
    zg.zombies.clear();zg.drops.clear();zg.doublePointsUntil=zg.instaKillUntil=0;zg.nukePointsAt=-1;
    zg.powerups.start(zg.ledger.total,ZombieRng());zg.dogs.start(ZombieRng());
    zg.firstRound=zg.settings.startingRound;zg.previousSpawner=-1;zg.dogFailures=0;zg.announcedRound=0;zg.gameOverRound=0;
    zg.client=scope.client;std::snprintf(zg.map,sizeof(zg.map),"%s",s.map);
    zg.launchedMap=!zg.pendingMap.empty() && zg.pendingMap==s.map?zg.pendingMap:std::string();zg.relaunchAt=0;
    zg.rises=zg.animatedAttacks=zg.riseFailures=zg.attacksBrokenOff=zg.setupZombies=0;
    zg.controller.setPlanner([](int round,bool) -> std::optional<bo1::RoundPlan> {
        // dog_round_tracker listens from the first "between_round_over".
        if(!zg.settings.hellhounds || round==zg.firstRound)return std::nullopt;
        if(!zg.dogs.beginRound(round,ZombieRng()))return std::nullopt;
        if(!zg.dogSpawners) {Log("ZOMBIE_DOG_ROUND round=%d replaced=NO_DOG_SPAWNER_ON_MAP",round);return std::nullopt;}
        Log("ZOMBIE_DOG_ROUND round=%d dog_round_count=%d dogs=%d health=%d next=%d",round,zg.dogs.count(),zg.dogs.total(),zg.dogs.health(),zg.dogs.next());
        return zg.dogs.plan();
    });
    if(!zg.controller.start(zg.settings.startingRound,time)) {Log("ZOMBIE_START refused round=%d",zg.settings.startingRound);return;}
    zg.running=true;zg.startTime=time;zg.nextCleanup=time;zg.nextWatch=time;zg.loadout=false;zg.loadoutAt=time;zg.loadoutTries=0;
    if(!zg.restorePause) {zg.pauseBefore=ai.pauseWanted;zg.restorePause=true;}
    ai.pauseWanted=true;   // native ai_disableSpawn: campaign spawners stay closed
    ZombieEvent("ZOMBIES",time);
    Log("ZOMBIE_START map=%s round=%d hellhounds=%d remove_allies=%d prepared=%d anims=%llu so_guards=%llu dog_spawners=%llu enemy_points=%llu templates=%llu",
        s.map,zg.settings.startingRound,zg.settings.hellhounds,zg.settings.removeAllies,prep.prepared && prep.map==s.map,
        static_cast<unsigned long long>(prep.applied.size()),static_cast<unsigned long long>(prep.guards.size()),
        static_cast<unsigned long long>(ai.dogPoints.size()),static_cast<unsigned long long>(ai.enemyPoints.size()),
        static_cast<unsigned long long>(ai.enemies.size()));
}
void ServiceZombies(const Snapshot& s,const AiScope& scope,int time) {
    zg.lastTime=time;
    zg.dogSpawners=!ai.dogPoints.empty();
    if(zg.stopWanted) {ResetZombieGame("MENU_END",true);return;}
    if(!zg.running) {
        // A map launched from the menu starts once its player has been ready
        // for 3 s, so level start-up scripts have placed the player.
        const bool here=zg.startWanted && zombieBindingsReady && scope.ready && s.state==LevelState::Ready
            && (zg.pendingMap.empty() || zg.pendingMap==s.map);
        if(!here) zg.readySince=-1;
        else if(zg.pendingMap.empty()) BeginZombies(s,scope,time);
        else if(zg.readySince<0 || time<zg.readySince) zg.readySince=time;
        else if(time-zg.readySince>=3000) BeginZombies(s,scope,time);
        return;
    }
    if(zg.client!=scope.client || std::strcmp(zg.map,s.map)) {ResetZombieGame("LIFETIME_CHANGED",false);return;}
    if(time<zg.startTime) {ResetZombieGame("LEVEL_RESTARTED",false);return;}
    ai.pauseWanted=true;
    AiPoint player{};int playerHealth=0;
    if(!ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player) || !Value(s.entity+kAiHealth,playerHealth))return;
    if(!zg.loadout && time>=zg.loadoutAt) {
        zg.loadout=ApplyLoadout();
        if(!zg.loadout && ++zg.loadoutTries<20)zg.loadoutAt=time+250;
        else zg.loadout=true;
    }
    if(time>=zg.nextCleanup) {zg.nextCleanup=time+1000;ClearCampaignActors(s,scope);}
    const auto oldRound=zg.controller.status().round;const auto oldPhase=zg.controller.status().phase;
    const auto request=zg.controller.tick(time);
    const auto& c=zg.controller.status();
    if(!c.running()) {ResetZombieGame(c.phase==bo1::RoundPhase::Complete?"ROUND_LIMIT":bo1::ReasonText(c.reason),true);return;}
    if(c.round!=zg.announcedRound && c.phase==bo1::RoundPhase::Intro) {
        // round_think: chalk_one_up, powerup_round_start, award_grenades_for_survivors.
        zg.announcedRound=c.round;zg.powerups.roundStart();zg.dogFailures=0;
        char text[48];std::snprintf(text,sizeof(text),c.dogs?"ROUND %d - HELLHOUNDS":"ROUND %d",c.round);
        Announce(text);ZombieEvent(text,time);AwardGrenades();
        Log("ZOMBIE_ROUND round=%d total=%d health=%d interval=%.3f dogs=%d move_speed=%d points=%d",
            c.round,c.total,c.health,c.interval,c.dogs,bo1::ZombieMoveSpeed(c.round),zg.ledger.score);
    }
    if(oldPhase!=c.phase || oldRound!=c.round)Log("ZOMBIE_PHASE round=%d phase=%s alive=%d queued=%d killed=%d wait_ms=%lld",
        c.round,bo1::PhaseText(c.phase),c.alive,c.queued,c.killed,static_cast<long long>(c.waitMilliseconds));
    if(request) {
        const bool created=SpawnZombie(*request,s,scope,time);
        zg.controller.confirm(request->ticket,created,time);
        if(!created && request->dogs && ++zg.dogFailures>=3) {
            Log("ZOMBIE_DOG_ROUND spawn_failed=3 remaining_cancelled=%d",zg.controller.status().queued);
            zg.controller.cancelQueued();
        }
    }
    ServiceZombieActors(s,scope,player,playerHealth,time);
    if(!zg.running)return;
    // dog_round_aftermath: max ammo at the last hellhound.
    if(c.dogs && c.phase==bo1::RoundPhase::Between && oldPhase!=bo1::RoundPhase::Between) {
        QueueDrop(bo1::Powerup::MaxAmmo,zg.lastDogKnown?zg.lastDogOrigin:player,time);zg.lastDogKnown=false;
    }
    ServicePowerups(player,time);
    if(!zg.controller.invariant()) {Log("ZOMBIE_INVARIANT_STOP");ResetZombieGame("INVARIANT",true);}
}
bool InstallService() {
    if(clockInstalled)return true;
    if(!aiProfile.resolved || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128))return false;
    originalClock=reinterpret_cast<ClockFn>(aiProfile.base+kFrameClockRva);
    originalSaveRead=reinterpret_cast<SaveReadFn>(aiProfile.base+kSaveReadRva);
    originalSaveWrite=reinterpret_cast<SaveWriteFn>(aiProfile.base+kSaveWriteRva);
    void* initEntry=nullptr;void* damageEntry=nullptr;
    serviceEdits.clear();
    if(!PlanCall(kFrameClockCallRva,kFrameClockRva,reinterpret_cast<std::uintptr_t>(&ClockHook))
        || !PlanCall(kSaveReadCallRva,kSaveReadRva,reinterpret_cast<std::uintptr_t>(&SaveReadHook))
        || !PlanCall(kSaveWriteCallRva,kSaveWriteRva,reinterpret_cast<std::uintptr_t>(&SaveWriteHook))
        || !PlanEntry(kActorInitRva,kActorInitPrologue,reinterpret_cast<std::uintptr_t>(&ActorInitHook),initEntry)
        || !PlanEntry(kDamageRva,kDamagePrologue,reinterpret_cast<std::uintptr_t>(&DamageHook),damageEntry)) {Log("AI_SERVICE profile/code planning failed; no hooks published");return false;}
    originalInit=reinterpret_cast<InitFn>(initEntry);originalDamage=reinterpret_cast<DamageFn>(damageEntry);
    // Publish only the intact CALL instruction from the rendering thread.
    // The actor prologue is changed at the following server-clock callback,
    // outside ActorInit execution on the game's actor/VM thread.
    if(!ExchangeCode(serviceEdits.front(),true)) {Log("AI_SERVICE clock call changed; publication refused");return false;}
    clockInstalled=true;
    Log("AI_SERVICE clock_installed=1 lifecycle_install=QUEUED");return true;
}
bool CompleteServiceInstall() {
    if(ai.status.hookInstalled)return true;
    if(!clockInstalled || serviceEdits.size()!=5)return false;
    std::size_t installed=1;
    for(;installed<serviceEdits.size();++installed)if(!ExchangeCode(serviceEdits[installed],true))break;
    if(installed!=serviceEdits.size()) {
        while(installed>1) {--installed;ExchangeCode(serviceEdits[installed],false);}
        Log("AI_SERVICE lifecycle hook transaction rolled back on native thread");return false;
    }
    ai.status.hookInstalled=true;
    friendlyFireHookReady.store(true,std::memory_order_release);
    Log("AI_SERVICE installed=1 scheduler=SERVER_CLOCK checkpoint_read=1 checkpoint_write=1 mission_slot_reclaim=1 script_parameter_isolation=1 friendly_damage_hook=1 atomic_code_width=16");
    return true;
}
float Distance(const AiEntity& e,const std::array<float,3>& origin) {
    float squared=0;
    for(int n=0;n<3;++n) { const float delta=e.origin[n]-origin[n]; squared+=delta*delta; }
    return std::sqrt(squared);
}
template<class Reader> void AdoptSavedOwned(const AiScope& scope,int count,const std::vector<std::uint8_t>& active,Reader read) {
    unsigned restored=0;
    // A native corpse may copy an actor's string fields. Prefer a living actor.
    // Helpers are shared by every owned actor of one source class and team.
    for(int pass=0;pass<2;++pass)for(int n=2;n<count;++n) {
        if(!active[n])continue;
        const auto address=scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride;
        std::uint8_t type=0;if(!read(address,&type,1) || type!=(pass?kActorCorpseType:kActorType))continue;
        std::uint16_t tag=0;
        if(!read(address+kAiTargetname,&tag,2) || !tag)continue;
        std::string text;if(!ScriptText(tag,text))continue;
        const auto helperNumber=OwnedActorHelper(text);if(!helperNumber)continue;
        const auto helperAddress=scope.pool+static_cast<std::uintptr_t>(*helperNumber)*kAiEntityStride;
        auto helper=ReadAiEntity(helperAddress,static_cast<int>(*helperNumber),read);
        helper.classname=ClassName(helper.classnameId);
        std::uint16_t helperTag=0;std::string helperText;
        if(!helper.valid || !read(helperAddress+kAiTargetname,&helperTag,2) || !ScriptText(helperTag,helperText)
            || helperText!=kOwnedSpawnerTag || !HumanSpawner(helper))continue;
        AiIdentity identity;identity.address=address;identity.number=static_cast<std::uint16_t>(n);
        const auto raw=ReadAiAllocation(identity,read);
        if(!raw.valid || !raw.inUse || raw.client || raw.number!=n)continue;
        identity.serial=raw.serial;identity.type=raw.type;
        if(std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==address && x.identity.serial==raw.serial;}))continue;
        OwnedAi owned;owned.identity=identity;owned.spawner=AiIdentity::capture(helper);
        owned.team=helper.team;owned.sourceName=helper.classname;owned.modelApplied=true;
        if(raw.type==kActorType) {
            const auto actor=ReadAiEntity(address,n,read);
            if(!RestorableOwnedActor(actor,helper,text,helperText))continue;
            owned.identity=AiIdentity::capture(actor);
            if(ReadAppearance(actor,owned.appearance)) {HoldAppearance(owned.appearance);owned.tagsHeld=true;}
        } else if(raw.type!=kActorCorpseType)continue;
        ai.owned.push_back(std::move(owned));++restored;
        Log("AI_CHECKPOINT_ADOPT entity=%d serial=%u helper=%u team=%d type=%u",n,raw.serial,*helperNumber,helper.team,raw.type);
    }
    ai.adoptPending=false;
    Log("AI_CHECKPOINT_OWNERSHIP restored=%u owned=%llu",restored,static_cast<unsigned long long>(ai.owned.size()));
}
void ReadWorld(const Snapshot& s,const AiScope& scope) {
    AiMeasure measure(true);
    const auto readBefore=GetMemoryReadCounts();
    LARGE_INTEGER poolAt{},decodedAt{},doneAt{};QueryPerformanceCounter(&poolAt);
    int count=0;
    if(!scope.ready || !Value(aiProfile.countSlot,count) || count<2 || count>kAiEntityCapacity-2) {ai.status.ready=false;return;}
    auto& pool=ai.worldPool;auto& active=ai.worldActive;
    pool.resize(static_cast<std::size_t>(count)*kAiEntityStride);active.resize(static_cast<std::size_t>(count));
    if(!ReadAi(scope.pool,pool.data(),pool.size()) || !ReadAi(aiProfile.activeArray,active.data(),active.size())) {ai.status.ready=false;return;}
    QueryPerformanceCounter(&decodedAt);ai.status.lastPoolUs=(decodedAt.QuadPart-poolAt.QuadPart)*1000000.0/measure.frequency.QuadPart;
    AiPoint player{};
    if(!ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player)) {ai.status.ready=false;return;}
    const int friendlyTeam=PlayerTeam(s),enemyTeam=friendlyTeam==kAlliesTeam?kAxisTeam:kAlliesTeam;
    auto snapshotRead=[&](std::uintptr_t at,void* out,std::size_t size) {
        return ReadArena(scope.pool,pool.data(),pool.size(),at,out,size,ReadAi);
    };
    if(ai.adoptPending)AdoptSavedOwned(scope,count,active,snapshotRead);
    ai.allyTeam=friendlyTeam;
    const std::string oldEnemy=ai.enemies.empty()?"":ai.enemies[ai.enemyIndex].classname;
    ai.appearances={};
    ai.enemies.clear();ai.friends.clear();ai.enemyPoints.clear();ai.combatants.clear();ai.dogPoints.clear();ai.allPoints.clear();
    ai.playerPosition=player;ai.playerPositionValid=true;
    ai.status.enemyAlive=0;ai.status.friendlyAlive=0;ai.status.otherAlive=0;ai.status.nativeCount=0;
    for(int n=2;n<count;++n) {
        if(!active[n]) continue;
        const auto type=pool[static_cast<std::size_t>(n)*kAiEntityStride];
        if(type!=kActorType && type!=kActorSpawnerType) continue;
        const auto address=scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride;
        auto e=ReadAiEntity(address,n,snapshotRead);if(!e.valid)continue;
        auto& names=ai.names[n];
        if(!names.valid || names.serial!=e.serial || names.classname!=e.classnameId) {
            ++ai.status.nameCacheMisses;names={};names.serial=e.serial;names.classname=e.classnameId;names.classText=ClassName(e.classnameId);names.valid=!names.classText.empty();
        } else ++ai.status.nameCacheHits;
        e.classname=names.classText;
        if(e.type==kActorType) {
            std::string lower=e.classname;
            std::uint16_t targetname=0;
            if(snapshotRead(e.address+kAiTargetname,&targetname,2)) {
                if(names.targetname!=targetname) {names.targetname=targetname;names.targetText=targetname?ClassName(targetname):"";}
                if(targetname)lower+=" "+names.targetText;
            }
            std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            for(int character=1;character<kFriendlyCharacterCount;++character) {
                std::string wanted=kFriendlyCharacters[character];std::transform(wanted.begin(),wanted.end(),wanted.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                std::uint16_t model=0;
                if(lower.find(wanted)!=std::string::npos && e.team==friendlyTeam && Value(e.address+kAiModelIndex,model) && model)
                    ai.appearances[character]=AiIdentity::capture(e);
            }
            ++ai.status.nativeCount;
            if(e.health>0)ai.combatants.push_back(e);
            if(e.health>0) {
                if(e.team==enemyTeam)++ai.status.enemyAlive;
                else if(e.team==friendlyTeam)++ai.status.friendlyAlive;
                else ++ai.status.otherAlive;
            }
        }
        const bool ours=std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==e.address || x.spawner.address==e.address;});
        if(!ours && e.team==enemyTeam && DogSpawner(e))ai.dogPoints.push_back(e);
        if(!ours && HumanSpawner(e))ai.allPoints.push_back(e);
        if(ours || !HumanSpawner(e))continue;
        if(e.team==enemyTeam) {
            ai.enemies.push_back(e);
            if(e.type==kActorSpawnerType)ai.enemyPoints.push_back(e);
        } else if(e.team==friendlyTeam)ai.friends.push_back(e);
    }
    QueryPerformanceCounter(&doneAt);ai.status.lastDecodeUs=(doneAt.QuadPart-decodedAt.QuadPart)*1000000.0/measure.frequency.QuadPart;decodedAt=doneAt;
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
    ai.enemyIndex=selection(ai.enemies,oldEnemy);
    ai.friendlyIndex=std::clamp(ai.friendlyIndex,0,kFriendlyCharacterCount-1);
    ai.status.enemySpawners=static_cast<int>(ai.enemies.size());ai.status.friendlySpawners=ai.friends.empty()?0:kFriendlyCharacterCount;
    ai.status.enemySpawner=ai.enemies.empty()?0:ai.enemyIndex+1;ai.status.friendlySpawner=ai.friends.empty()?0:ai.friendlyIndex+1;
    ai.status.enemyName={};ai.status.friendlyName={};
    if(!ai.enemies.empty())std::snprintf(ai.status.enemyName.data(),ai.status.enemyName.size(),"%s",ai.enemies[ai.enemyIndex].classname.c_str());
    if(!ai.friends.empty())std::snprintf(ai.status.friendlyName.data(),ai.status.friendlyName.size(),"%s%s",kFriendlyCharacters[ai.friendlyIndex],ai.friendlyIndex && !ai.appearances[ai.friendlyIndex].address?" [not loaded]":"");
    ai.status.enemyDistance=ai.enemyPoints.empty()?0.f:Distance(ai.enemyPoints.front(),player);
    ai.status.friendlyDistance=0.f;ai.status.characterLoaded=!ai.friendlyIndex || ai.appearances[ai.friendlyIndex].address;
    if(ai.status.lastEntity)ai.status.lastSpawnDistance=std::sqrt(PointDistanceSquared(ai.status.lastPosition,player));
    int nativeActors=0;if(Value(aiProfile.actorCountSlot,nativeActors))ai.status.engineActors=nativeActors;
    const auto limit=ActorLimit();ai.status.engineLimit=limit.valid?limit.current:NativeActorCapacity();
    ai.status.owned=OwnCount();ai.status.ready=s.state==LevelState::Ready;
    QueryPerformanceCounter(&doneAt);ai.status.lastSortUs=(doneAt.QuadPart-decodedAt.QuadPart)*1000000.0/measure.frequency.QuadPart;
    const auto readAfter=GetMemoryReadCounts();ai.status.worldKernelReads+=readAfter.kernel-readBefore.kernel;ai.status.worldImageReads+=readAfter.image-readBefore.image;
}

} // namespace

bool FriendlyFireHookReady() {return friendlyFireHookReady.load(std::memory_order_acquire);}
bool NativeDamageHookReady() {return FriendlyFireHookReady();}
void SetAllowFriendlyFire(bool enabled) {allowFriendlyFire.store(enabled && FriendlyFireHookReady(),std::memory_order_release);}
void SetHitmarkers(bool enabled) {
    hitmarkersEnabled.store(enabled && NativeDamageHookReady() && MpFeedbackReady(),std::memory_order_release);
    if(!enabled)lastPlayerHit.store(0,std::memory_order_release);
}
void SetSpawnedFriendliesInvincible(bool enabled) {invincibleFriendlies.store(enabled && NativeDamageHookReady(),std::memory_order_release);}
std::uint8_t GetHitmarkerAlpha(ULONGLONG now) {
    static std::uint64_t presented=0;
    if(!hitmarkersEnabled.load(std::memory_order_acquire) || !MpFeedbackReady()){presented=confirmedPlayerHits.load();return 0;}
    const auto count=confirmedPlayerHits.load(std::memory_order_acquire);
    const auto alpha=HitmarkerAlpha(lastPlayerHit.load(std::memory_order_acquire),now);
    if(count!=presented){presented=count;if(alpha)PlayMpHitAlert();}
    return alpha;
}

void InitializeAiEngine(std::uintptr_t base,std::size_t size,std::uintptr_t text,std::size_t textSize,bool supported) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
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
    aiProfile.damage=base+kDamageRva;aiProfile.updateModel=base+kDObjUpdateRva;
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
        && TeamNames() && EntityFields() && PersistenceFields()
        && Call(copyCalls,14)==copyFields && Call(copyCalls,25)==copyDynamic
        && Call(traceCall,38)==trace && Call(positionCall,8)==position && Jump(teamCall,33)==team
        && Call(goalCall,23)==goal && InHost(aiProfile.allocate,1) && InHost(aiProfile.freeEntity,1)
        && InHost(aiProfile.setEnemy,1) && InHost(aiProfile.actorCountSlot,4) && InHost(aiProfile.actorLimitSlot,8)
        && InHost(aiProfile.completeTable,kWeaponCapacity*8) && InHost(aiProfile.definitionTable,kWeaponCapacity*8)
        && boundsMatch && Call(actorTrace,50)==trace && Call(handsCall,7)==builder && Call(handsCall,24)==builder
        && aiProfile.setEnemy==enemyStart && InHost(aiProfile.setString,1);
    std::array<std::uint8_t,15> damageStart{};
    aiProfile.resolved=aiProfile.resolved && InHost(base+kScriptParametersRva,sizeof(NativeScriptParameters))
        && InHost(base+kScriptExecuteDepthRva,4) && ReadAi(base+kDamageRva,damageStart.data(),damageStart.size()) && damageStart==kDamagePrologue
        && Rip(base+0x223180,0,2,6)==base+kScriptParametersRva+12
        && Rip(base+0x223994,0,3,7)==base+kScriptParametersRva
        && Rip(base+0x224934,0,2,6)==base+kScriptExecuteDepthRva
        && Rip(base+0x224777,0,3,7)==base+kScriptVmStateRva
        && Rip(base+0x2237f0,0,3,7)==base+kScriptStackBaseRva
        && Rip(base+0x223867,0,2,10)==base+kScriptStackBaseRva+8
        && Rip(base+0x223804,0,3,7)==base+kScriptFrameBaseRva
        && Rip(base+0x2237f7,0,2,6)==base+kScriptFunctionCountRva
        && Rip(base+0x22380b,0,3,7)==base+kScriptFramePointerRva
        && Rip(base+0x223812,0,3,7)==base+kScriptLocalVariablesBaseRva
        && Rip(base+0x223819,0,3,7)==base+kScriptLocalVariablesPointerRva;
    ai.status.resolved=aiProfile.resolved;
    // Test38 squad following: the native setgoalentity method and the native
    // position-goal routine both call this exact entity-handle setter.
    {
        std::array<std::uint8_t,15> prologue{};
        const auto handle=base+kEntityHandleSetRva;
        aiProfile.goalEntityReady=aiProfile.resolved && ReadAi(handle,prologue.data(),prologue.size()) && prologue==kEntityHandleSetPrologue
            && Call(base+kSetGoalEntityHandleCallRva,0)==handle && Call(base+kGoalPositionHandleCallRva,0)==handle
            && aiProfile.setGoal==base+kGoalPositionHandleCallRva-0x21 && MethodEntry("setgoalentity",base+kSetGoalEntityRva);
        aiProfile.setEntityHandle=aiProfile.goalEntityReady?handle:0;
        Log("AI_FOLLOW goal_entity_binding=%d handle_rva=0x%llx",aiProfile.goalEntityReady,static_cast<unsigned long long>(kEntityHandleSetRva));
    }
    // Test38 zombies: native value pushes, builtins and the command buffer.
    {
        struct Prefix {std::uintptr_t rva;std::array<std::uint8_t,6> bytes;};
        static constexpr Prefix prefixes[]={
            {kScrAddIntRva,{0x40,0x53,0x48,0x83,0xec,0x20}},{kScrAddFloatRva,{0x48,0x83,0xec,0x38,0x0f,0x29}},
            {kScrAddStringRva,{0x40,0x53,0x48,0x83,0xec,0x20}},{kScrAddVectorRva,{0x40,0x53,0x48,0x83,0xec,0x20}},
            {kScrAddEntityNumRva,{0x40,0x53,0x48,0x83,0xec,0x20}},{kScrAddUndefinedRva,{0x48,0x83,0xec,0x28,0xe8,0x67}},
            {kCbufAddTextRva,{0x48,0x89,0x5c,0x24,0x08,0x57}}};
        bool ok=aiProfile.resolved;
        for(const auto& prefix:prefixes) {std::array<std::uint8_t,6> seen{};ok=ok && ReadAi(base+prefix.rva,seen.data(),seen.size()) && seen==prefix.bytes;}
        // Each push begins with the native IncInParam.
        for(const auto rva:{kScrAddIntRva,kScrAddFloatRva,kScrAddStringRva,kScrAddVectorRva,kScrAddUndefinedRva}) {
            std::uint8_t code[24]{};ok=ok && ReadAi(base+rva,code,sizeof(code));
            std::uintptr_t call=0;for(int n=0;n<19 && ok;++n)if(code[n]==0xe8){call=Call(base+rva,n);break;}
            ok=ok && call==base+kScrIncInParamRva;
        }
        unsigned builtins=0;
        for(const auto& b:kZombieBuiltins)if(ok && MethodEntry(b.name,base+b.rva))++builtins;
        ok=ok && builtins==kZombieBuiltins.size();
        cbufReady=aiProfile.resolved && Call(base+kCbufAddTextCallerRva,0)==base+kCbufAddTextRva;
        if(ok) {
            scriptNatives.addInt=reinterpret_cast<decltype(scriptNatives.addInt)>(base+kScrAddIntRva);
            scriptNatives.addFloat=reinterpret_cast<decltype(scriptNatives.addFloat)>(base+kScrAddFloatRva);
            scriptNatives.addString=reinterpret_cast<decltype(scriptNatives.addString)>(base+kScrAddStringRva);
            scriptNatives.addVector=reinterpret_cast<decltype(scriptNatives.addVector)>(base+kScrAddVectorRva);
            scriptNatives.addEntityNum=reinterpret_cast<decltype(scriptNatives.addEntityNum)>(base+kScrAddEntityNumRva);
            scriptNatives.addUndefined=reinterpret_cast<decltype(scriptNatives.addUndefined)>(base+kScrAddUndefinedRva);
            scriptNatives.notify=reinterpret_cast<decltype(scriptNatives.notify)>(base+kScriptNotifyRva);
        }
        zombieBindingsReady=ok && aiProfile.goalEntityReady && scriptNatives.complete();
        Log("ZOMBIE_BINDINGS ready=%d builtins=%u/%llu cbuf=%d goal_entity=%d",zombieBindingsReady,builtins,
            static_cast<unsigned long long>(kZombieBuiltins.size()),cbufReady,aiProfile.goalEntityReady);
    }
    // Test39: level-load hook, Spec Ops end paths, asset database, script
    // strings, Scr_AddAnim and the anim-tree accessors used by animscripted.
    {
        auto bytesAt=[&](std::uintptr_t rva,const auto& expected) {
            std::array<std::uint8_t,64> seen{};
            return expected.size()<=seen.size() && ReadAi(base+rva,seen.data(),expected.size()) && std::equal(expected.begin(),expected.end(),seen.begin());
        };
        static constexpr std::array<std::uint8_t,51> dispatch={0x48,0x8b,0x0d,0xad,0xc1,0x6d,0x01,0x44,0x0f,0xb6,0x01,0x48,0x8d,0x51,0x01,0x44,0x89,0x05,0xbe,0xc1,0x6d,0x01,
            0x48,0x89,0x15,0x97,0xc1,0x6d,0x01,0x41,0x81,0xf8,0x8b,0x00,0x00,0x00,0x77,0xda,0x43,0x8b,0x8c,0x87,0x50,0x76,0x22,0x00,0x49,0x03,0xcf,0xff,0xe1};
        static constexpr std::array<std::uint8_t,15> leaf={0x8b,0xc2,0x48,0x03,0xc0,0x66,0x83,0x7c,0xc1,0x08,0x00,0x0f,0x94,0xc0,0xc3};
        static constexpr std::array<std::uint8_t,32> length={0x48,0x8b,0xc1,0x8b,0xca,0x48,0xff,0xc1,0x48,0x03,0xc9,0x48,0x8b,0x0c,0xc8,0x0f,0xb7,0x41,0x12,
            0x66,0x0f,0x6e,0xc0,0x0f,0x5b,0xc0,0xf3,0x0f,0x5e,0x41,0x2c,0xc3};
        std::uint32_t opEnd=0;
        bool ok=zombieBindingsReady
            && bytesAt(kScrEndLoadScriptsRva,kScrEndLoadScriptsPrologue) && Jump(base+kLevelScriptsLoadedJumpRva,0)==base+kScrEndLoadScriptsRva
            && bytesAt(kScrGetFunctionHandleRva,kScrGetFunctionHandlePrologue)
            && Rip(base+0x21698d,0,2,6)==base+kScrProgramBufferRva && Rip(base+0x216993,0,2,6)==base+kScrProgramSizeRva
            && bytesAt(kVmDispatchRva,dispatch) && Value(base+kVmOpcodeTableRva,opEnd) && base+opEnd==base+kVmOpEndHandlerRva
            && bytesAt(kDbFindXAssetEntryRva,kDbFindXAssetEntryPrologue) && bytesAt(kSlGetStringRva,kSlGetStringPrologue)
            && bytesAt(kScrAddAnimRva,kScrAddAnimPrologue) && Call(base+kScrAddAnimRva,9)==base+kScrIncInParamRva
            && bytesAt(kEntAnimTreeRva,kEntAnimTreePrologue) && bytesAt(0x29d110,leaf) && bytesAt(0x29c920,length);
        std::uint64_t xanimName=0;
        ok=ok && Value(base+kAssetNamesRva+8*kAssetTypeXAnimParts,xanimName) && xanimName>base && xanimName<base+aiProfile.size;
        char xanim[8]{};ok=ok && ReadAi(static_cast<std::uintptr_t>(xanimName),xanim,6) && !std::strcmp(xanim,"xanim");
        unsigned anims=0;
        for(const auto& b:kAnimBuiltins)if(ok && MethodEntry(b.name,base+b.rva))++anims;
        prep.bindings=ok && anims==kAnimBuiltins.size();
        if(prep.bindings)scriptNatives.addAnim=reinterpret_cast<decltype(scriptNatives.addAnim)>(base+kScrAddAnimRva);
        Log("LEVEL_PREP bindings=%d anim_builtins=%u/%llu",prep.bindings,anims,static_cast<unsigned long long>(kAnimBuiltins.size()));
        if(prep.bindings)bo1assets::StartLoad();
        // Scr_ExecEntThreadNum / Scr_FreeThread exactly as animscripted uses
        // them: program-buffer relative handle, VM_Execute, then the return
        // value popped (inparamcount decremented) before the id is returned.
        prep.threads=prep.bindings
            && bytesAt(kScrExecEntThreadNumRva,kScrExecEntThreadNumPrologue) && bytesAt(kScrExecEntThreadRva,kScrExecEntThreadPrologue)
            && Jump(base+kScrExecEntThreadRva,12)==base+kScrExecEntThreadNumRva && bytesAt(kScrFreeThreadRva,kScrFreeThreadPrologue)
            && Call(base+kAnimScriptedExecCallRva,0)==base+kScrExecEntThreadRva && Call(base+kAnimScriptedFreeCallRva,0)==base+kScrFreeThreadRva
            && Rip(base+0x22270a,0,3,7)==base+kScrProgramBufferRva && Call(base+0x22274a,0)==base+0x224700
            && Rip(base+0x22278b,0,2,6)==base+kScriptParametersRva+8;
        Log("ZOMBIE_SETUP thread_calls=%d functions=%llu",prep.threads,static_cast<unsigned long long>(kSetupFunctions.size()));
        // Test40: the level-zone DB_LoadXAssets call, G_ModelIndex and the model methods.
        unsigned modelBuiltins=0;
        for(const auto& b:kModelBuiltins)if(prep.bindings && MethodEntry(b.name,base+b.rva))++modelBuiltins;
        std::uint64_t xmodelName=0;char xmodel[8]{};
        models.bindings=prep.bindings && modelBuiltins==kModelBuiltins.size()
            && bytesAt(kDbLoadXAssetsRva,kDbLoadXAssetsPrologue) && Call(base+kLevelZoneLoadCallRva,0)==base+kDbLoadXAssetsRva
            && bytesAt(kGModelIndexRva,kGModelIndexPrologue)
            && Value(base+kAssetNamesRva+8*kAssetTypeXModel,xmodelName) && xmodelName>base && xmodelName<base+aiProfile.size
            && ReadAi(static_cast<std::uintptr_t>(xmodelName),xmodel,7) && !std::strcmp(xmodel,"xmodel");
        Log("BO1_MODELS bindings=%d model_builtins=%u/%llu",models.bindings,modelBuiltins,static_cast<unsigned long long>(kModelBuiltins.size()));
        if(models.bindings)bo1models::StartBuild();
    }
    InstallExpandedCapacity(base,aiProfile.resolved);
    // Installed after the capacity arena is placed (unchanged layout), before
    // any level loads, so every level's setup functions are known (zombies
    // started on "This level" use them too).
    if(prep.bindings && IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) InstallLevelScriptHook();
    if(models.bindings && IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) InstallLevelZoneHook();
    Log("AI_PROFILE resolved=%d callback_slot_rva=0x%llx spawn_rva=0x%llx delete_rva=0x%llx pool_slot_rva=0x%llx clock_rva=0x%llx",
        aiProfile.resolved,static_cast<unsigned long long>(aiProfile.thinkSlot-base),static_cast<unsigned long long>(spawn-base),
        static_cast<unsigned long long>(remove-base),static_cast<unsigned long long>(aiProfile.poolSlot-base),static_cast<unsigned long long>(aiProfile.timeSlot-base));
}
void UpdateAiControls(const Snapshot& s,ULONGLONG,bool detailed) {
    std::unique_lock<std::recursive_mutex> lock(aiMutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    if(!aiProfile.resolved) return;
    ai.detailed=detailed;
    if(s.state==LevelState::Ready && !clockInstalled)InstallService();
    WorkWanted();
}
void RequestAiOperation(AiOperation op,const Snapshot& s) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    const auto scope=Scope(s);
    if(ai.owned.empty() && ai.recovery.empty() && !ai.queue.pending() && !ai.hands.pending)Lifetime(scope);
    if(s.state!=LevelState::Ready || !scope.ready) { Finish(AiRequestResult::NotReady); return; }
    if(ai.queue.pending()) { ai.status.lastResult=AiRequestResult::Busy; return; }
    const bool spawn=op==AiOperation::SpawnEnemy || op==AiOperation::SpawnFriendly;
    const bool friendly=op==AiOperation::SpawnFriendly;
    const auto& list=friendly?ai.friends:ai.enemies;
    const int selected=friendly?0:ai.enemyIndex;
    AiIdentity source,appearance;
    if(spawn) {
        if(list.empty()) {Finish(AiRequestResult::NoSpawner);return;}
        source=AiIdentity::capture(list[selected]);
        if(friendly && ai.friendlyIndex) {
            appearance=ai.appearances[ai.friendlyIndex];
            if(!appearance.address) {Finish(AiRequestResult::ModelUnavailable);return;}
        }
    } else if(op==AiOperation::RemoveOwned && !OwnCount() && ai.recovery.empty()) {Finish(AiRequestResult::IdentityChanged);return;}
    if(!InstallService()) { Finish(AiRequestResult::HookUnavailable); return; }
    if(!ai.queue.enqueue(op,source,scope,appearance)) { Finish(ai.queue.reason()); return; }
    ai.status.lastResult=AiRequestResult::Queued; ai.status.pending=true;
    nativePending.store(true,std::memory_order_release);
    Log("AI_REQUEST operation=%d source=%u serial=%u map=%s player_time=%d render_thread=%lu",
        static_cast<int>(op),source.number,source.serial,s.map,scope.commandTime,GetCurrentThreadId());
}
void AdjustAiSpawner(bool friendly,int direction) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    if(!direction)return;
    if(friendly) {
        ai.friendlyIndex=(ai.friendlyIndex+kFriendlyCharacterCount+(direction>0?1:-1))%kFriendlyCharacterCount;
        Log("AI_CHARACTER_SELECT name=%s loaded=%d",kFriendlyCharacters[ai.friendlyIndex],!ai.friendlyIndex || ai.appearances[ai.friendlyIndex].address!=0);
    } else {
        if(ai.enemies.empty())return;
        ai.enemyIndex=(ai.enemyIndex+static_cast<int>(ai.enemies.size())+(direction>0?1:-1))%static_cast<int>(ai.enemies.size());
        Log("AI_SPAWNER_SELECT friendly=0 entity=%u classname=%s",ai.enemies[ai.enemyIndex].number,ai.enemies[ai.enemyIndex].classname.c_str());
    }
    ai.nextWorldTime=0;
}
void ToggleAiSpawnPause() {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    if(!ai.status.ready) return;
    ai.pauseWanted=!ai.pauseWanted;WorkWanted();
    Log("AI_SPAWN_PAUSE requested=%d",ai.pauseWanted);
}
AiStatus GetAiStatus() {
    thread_local AiStatus cached;
    std::unique_lock<std::recursive_mutex> lock(aiMutex,std::try_to_lock);
    if(lock.owns_lock())cached=ai.status;
    auto status=cached;status.callbacks=nativeCallbacks.load();status.nativeThread=nativeThread.load();return status;
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
    Log("AI_COMBAT_STATS killed=%llu campaign_reclaimed=%llu resource_shed=%llu character_loaded=%d scheduler=SERVER_CLOCK",s.killed,s.reclaimed,s.resourceShed,s.characterLoaded);
    Log("AI_DAMAGE_OPTIONS hitmarkers=%d invincible_spawned_friendlies=%d confirmed_hits=%llu protected_hits=%llu",
        hitmarkersEnabled.load(),invincibleFriendlies.load(),confirmedPlayerHits.load(),protectedFriendlyHits.load());
    Log("AI_PERF service_updates=%llu service_average_us=%.1f service_peak_us=%.1f world_reads=%llu world_average_us=%.1f world_peak_us=%.1f max_npcs_per_tick=%llu budget_us=%.0f last_batch=%d peak_batch=%d recovery_pending=%d reassertions=%llu render_world_reads=0 disk_flush=CRASH_ONLY",
        s.serviceUpdates,s.serviceAverageUs,s.servicePeakUs,s.worldReads,s.worldAverageUs,s.worldPeakUs,static_cast<unsigned long long>(kFollowMaximumPerTick),
        kFollowBudgetMicroseconds,s.followLastBatch,s.followPeakBatch,s.recovering,s.combatReassertions);
    Log("AI_FOLLOW mode=%s goal_entity_assigns=%llu radius_updates=%llu fixed_nodes_cleared=%llu",
        aiProfile.goalEntityReady?"GOAL_ENTITY_PLAYER":"POSITION_FALLBACK",s.followGoalEntity,s.followRadius,s.followUnfixed);
    Log("AI_SCAN_PERF last_pool_us=%.1f last_decode_us=%.1f last_sort_us=%.1f kernel_reads_per_scan=%.1f image_reads_per_scan=%.1f name_cache_hits=%llu name_cache_misses=%llu",
        s.lastPoolUs,s.lastDecodeUs,s.lastSortUs,s.worldReads?double(s.worldKernelReads)/s.worldReads:0.0,s.worldReads?double(s.worldImageReads)/s.worldReads:0.0,s.nameCacheHits,s.nameCacheMisses);
    LogExpandedResources();
    LogScriptVm(true);
}
std::uint64_t RequestNativeHandDefinitions(const WeaponFields& weapon,std::uintptr_t names,std::uintptr_t normalTree,
    std::uintptr_t cache,const Snapshot& s) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    const auto scope=Scope(s);
    if(!scope.ready || !weapon.valid || !names || !normalTree || !cache || !aiProfile.animationBuilder) return 0;
    if(ai.hands.pending) return SameWeapon(ai.hands.weapon,weapon)?ai.hands.ticket:0;
    if(!InstallService())return 0;
    HandBuild h;h.ticket=++ai.nextHandTicket;h.weapon=weapon;h.names=names;h.normal=normalTree;h.cache=cache;
    h.lifetime.client=scope.client;h.lifetime.pool=scope.pool;h.lifetime.commandTime=scope.commandTime;
    std::strcpy(h.lifetime.map.data(),scope.map);h.pending=true;h.result.state=NativeHandBuildState::Queued;
    ai.hands=h;WorkWanted();return h.ticket;
}
bool ZombieModeAvailable() {std::lock_guard<std::recursive_mutex> lock(aiMutex);return zombieBindingsReady;}
void RequestZombieStart(const ZombieSettings& settings,const char* map) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    if(!zombieBindingsReady || zg.running)return;
    zg.requested=settings;zg.requested.startingRound=std::clamp(settings.startingRound,bo1::kPreviewMinRound,bo1::kPreviewMaxRound);
    zg.pendingMap=map && SafeMapName(map)?map:"";zg.startWanted=true;zg.stopWanted=false;zg.gameOverRound=0;
    if(!InstallService()) {zg.startWanted=false;Log("ZOMBIE_START refused=HOOK_UNAVAILABLE");return;}
    if(!zg.pendingMap.empty())InstallLevelScriptHook();
    zg.relaunchAt=0;
    nativePending.store(true,std::memory_order_release);
    Log("ZOMBIE_REQUEST start round=%d hellhounds=%d remove_allies=%d map=%s",zg.requested.startingRound,
        zg.requested.hellhounds,zg.requested.removeAllies,zg.pendingMap.empty()?"CURRENT":zg.pendingMap.c_str());
}
void RequestZombieStop() {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    if(zg.relaunchAt) {zg.relaunchAt=0;zg.launchedMap.clear();Log("ZOMBIE_RELAUNCH cancelled");}
    if(zg.running) {zg.stopWanted=true;nativePending.store(true,std::memory_order_release);}
    else if(zg.startWanted) {zg.startWanted=false;Log("ZOMBIE_REQUEST cancelled");}
}
void SetZombieHellhounds(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    zg.requested.hellhounds=enabled;if(zg.running)zg.settings.hellhounds=enabled;
    Log("ZOMBIE_HELLHOUNDS enabled=%d dog_spawners=%llu",enabled,static_cast<unsigned long long>(ai.dogPoints.size()));
}
// Load a Spec Ops map the way the Spec Ops menu does (ui.ff: specialops 1,
// ui_load_so_level, coop_mapName, solo_play 1, then "devmap so_<mission>"),
// plus so_nofail. Zombies start when that level's player is ready. The
// command buffer is lock protected.
bool LaunchZombieMap(const char* map,const ZombieSettings& settings) {
    if(!SafeMapName(map) || SpecOpsMapIndex(map)<0)return false;
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);if(!cbufReady || !zombieBindingsReady || zg.running)return false;}
    RequestZombieStart(settings,map);
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);if(!zg.startWanted)return false;}
    char command[384];
    std::snprintf(command,sizeof(command),"set specialops 1; set solo_play 1; set so_nofail 1; set start \"\"; set ui_load_so_level %s; set coop_mapName %s; devmap %s\n",map,map,map);
    reinterpret_cast<CbufFn>(aiProfile.base+kCbufAddTextRva)(0,command);
    Log("ZOMBIE_LAUNCH map=%s command=devmap specialops=1 so_nofail=1 level_hook=%d bo1_assets=%s",map,prep.hookInstalled,
        bo1assets::StateText(bo1assets::GetStatus().state));
    return true;
}
void TickZombieRelaunch() {
    std::string map;ZombieSettings settings;
    {
        std::unique_lock<std::recursive_mutex> lock(aiMutex,std::try_to_lock);
        if(!lock.owns_lock())return;
        // Crash guard: zombies have worn the BO1 models in play long enough.
        if(models.appended && models.firstDressed && GetTickCount64()-models.firstDressed>=bo1models::kGuardConfirmMs) {
            models.firstDressed=0;
            if(bo1models::GuardConfirm()) Log("BO1_MODELS confirmed=1 level_zone=%s dressed=%u",models.zoneFor.c_str(),models.dressed);
        }
        if(!zg.relaunchAt || GetTickCount64()<zg.relaunchAt || zg.running || zg.startWanted)return;
        map=zg.launchedMap;settings=zg.launchSettings;zg.relaunchAt=0;
    }
    if(!map.empty())Log("ZOMBIE_RELAUNCH map=%s accepted=%d",map.c_str(),LaunchZombieMap(map.c_str(),settings));
}
ZombieGameStatus GetZombieGameStatus() {
    thread_local ZombieGameStatus cached;
    std::unique_lock<std::recursive_mutex> lock(aiMutex,std::try_to_lock);
    if(!lock.owns_lock())return cached;
    ZombieGameStatus s;
    s.available=zombieBindingsReady;s.running=zg.running;s.pending=zg.startWanted;
    std::snprintf(s.pendingMap,sizeof(s.pendingMap),"%s",zg.pendingMap.c_str());
    s.settings=zg.running?zg.settings:zg.requested;s.round=zg.controller.status();
    s.points=zg.ledger.score;s.pointsTotal=zg.ledger.total;s.kills=zg.kills;s.headshots=zg.headshots;
    const int now=zg.lastTime;
    s.doublePointsMs=std::max(0,zg.doublePointsUntil-now);s.instaKillMs=std::max(0,zg.instaKillUntil-now);
    s.dogSpawners=!ai.dogPoints.empty();s.nextDogRound=zg.dogs.next();
    for(const auto& d:zg.drops) {
        if(s.dropCount>=static_cast<int>(s.drops.size()))break;
        s.drops[static_cast<std::size_t>(s.dropCount++)]={d.type,d.origin,bo1::PowerupVisible(now-d.spawned)};
    }
    std::snprintf(s.weapon,sizeof(s.weapon),"%s",zg.pistol.c_str());
    std::snprintf(s.event,sizeof(s.event),"%s",zg.event.c_str());s.eventAgeMs=now-zg.eventTime;
    s.gameOverRound=zg.gameOverRound;s.lastPointsDelta=zg.lastDelta;s.pointsDeltaAgeMs=now-zg.deltaTime;
    const auto assets=bo1assets::GetStatus();
    std::snprintf(s.bo1Assets,sizeof(s.bo1Assets),"%s",bo1assets::StateText(assets.state));
    s.bo1AnimsFound=static_cast<int>(assets.found);s.bo1AnimsWanted=static_cast<int>(assets.wanted);
    s.animSlotsApplied=static_cast<int>(prep.applied.size());s.animSlotsWanted=static_cast<int>(std::size(kAnimSlots));
    s.specOpsGuards=static_cast<int>(prep.guards.size());s.levelPrepared=prep.prepared;
    const auto modelStatus=bo1models::GetStatus();
    std::snprintf(s.bo1Models,sizeof(s.bo1Models),"%s",modelStatus.skipped?"SKIPPED":bo1models::StateText(modelStatus.state));
    s.modelsPrecached=0;
    if(models.precached) {   // distinct models (the body list repeats body2 for BO1's weighting)
        std::vector<std::string> all(models.bodies);all.insert(all.end(),models.heads.begin(),models.heads.end());
        std::sort(all.begin(),all.end());s.modelsPrecached=static_cast<int>(std::unique(all.begin(),all.end())-all.begin());
    }
    s.zombiesDressed=models.dressed;
    if(zg.relaunchAt) {const auto now64=GetTickCount64();s.restartInMs=now64>=zg.relaunchAt?0:static_cast<int>(zg.relaunchAt-now64);}
    cached=s;return s;
}
void LogZombieGame() {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    const auto& c=zg.controller.status();
    const auto assets=bo1assets::GetStatus();
    unsigned setupHandles=0;for(const auto h:prep.setup) if(h>0) ++setupHandles;
    Log("ZOMBIE_PREP prepared=%d map=%s anims=%llu/%llu so_guards=%llu bo1_assets=%s found=%u/%u zone=%s rises=%u rise_failures=%u animated_attacks=%u attacks_broken_off=%u setup_threads=%d setup_functions=%u/%llu setup_zombies=%u setup_calls=%u setup_failures=%u relaunch_pending=%d",
        prep.prepared,prep.map.c_str(),static_cast<unsigned long long>(prep.applied.size()),static_cast<unsigned long long>(std::size(kAnimSlots)),
        static_cast<unsigned long long>(prep.guards.size()),bo1assets::StateText(assets.state),assets.found,assets.wanted,
        assets.zone.empty()?"NONE":assets.zone.c_str(),zg.rises,zg.riseFailures,zg.animatedAttacks,zg.attacksBrokenOff,prep.threads,setupHandles,
        static_cast<unsigned long long>(kSetupFunctions.size()),zg.setupZombies,prep.setupCalls,prep.setupFailures,zg.relaunchAt!=0);
    const auto modelStatus=bo1models::GetStatus();
    Log("ZOMBIE_MODELS state=%s skipped=%s cached=%d zone_loaded=%d level_zone=%s precached=%d bodies=%llu heads=%llu dressed=%u dress_failures=%u helmet_swaps=%u textures=%u fallbacks=%u file=%s",
        bo1models::StateText(modelStatus.state),modelStatus.skipped?modelStatus.skipReason.c_str():"-",modelStatus.cached,models.appended,models.zoneFor.empty()?"-":models.zoneFor.c_str(),models.precached,
        static_cast<unsigned long long>(models.bodies.size()),static_cast<unsigned long long>(models.heads.size()),models.dressed,models.dressFailures,
        models.helmetSwaps,modelStatus.textures,modelStatus.fallbacks,modelStatus.file.empty()?"-":modelStatus.file.c_str());
    Log("ZOMBIE_STATE available=%d running=%d pending=%d map=%s round=%d phase=%s alive=%d queued=%d killed=%d dogs=%d points=%d total=%d kills=%u headshots=%u hits=%u melee_hits=%u melee_misses=%u drops=%llu double_points_ms=%d insta_kill_ms=%d campaign_removed=%u weapon=%s dog_spawners=%llu next_dog_round=%d",
        zombieBindingsReady,zg.running,zg.startWanted,zg.running?zg.map:zg.pendingMap.c_str(),c.round,bo1::PhaseText(c.phase),c.alive,c.queued,c.killed,c.dogs,
        zg.ledger.score,zg.ledger.total,zg.kills,zg.headshots,zg.hits,zg.meleeHits,zg.meleeMisses,static_cast<unsigned long long>(zg.drops.size()),
        std::max(0,zg.doublePointsUntil-zg.lastTime),std::max(0,zg.instaKillUntil-zg.lastTime),zg.campaignRemoved,zg.pistol.c_str(),
        static_cast<unsigned long long>(ai.dogPoints.size()),zg.dogs.next());
}
NativeHandBuildResult GetNativeHandBuild(std::uint64_t ticket) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    return ticket && ticket==ai.hands.ticket?ai.hands.result:NativeHandBuildResult{};
}
} // namespace mod
