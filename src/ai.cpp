#include "runtime.hpp"
#include <cstdio>
#include <vector>
#include <limits>
#include <cctype>
#include <intrin.h>
#include "native_hooks.hpp"
#include "ai_behavior.hpp"
#include "checked_memory.hpp"
#include "script_text.hpp"

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
} aiProfile;
struct OwnedAi {
    AiIdentity identity,spawner;int team=0,lastGoalTime=-1;
    std::uintptr_t target=0;AiPoint goal{};bool hasGoal=false;
    AiAppearance appearance;int appearanceAt=0;bool modelApplied=false;
    std::string sourceName;
    bool tagsHeld=false;
};
struct RecoveryAi {
    AiIdentity previous;
    std::string sourceName;
    int team=0,health=100;
    AiAppearance appearance;
    bool tagsHeld=false;
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
    std::vector<AiEntity> enemies,friends,enemyPoints,combatants;
    AiPoint playerPosition{};
    bool playerPositionValid=false;
    std::array<AiIdentity,kFriendlyCharacterCount> appearances{};
    int enemyIndex=0,friendlyIndex=0;
    int nextWorldTime=-1,nextRecoveryTime=-1,allyTeam=kAlliesTeam;
    std::size_t followCursor=0;
    bool detailed=false,recoverDeferred=false;
    std::vector<std::uint8_t> worldPool,worldActive;
    struct Names {bool valid=false;std::uint32_t serial=0;std::uint16_t classname=0,targetname=0;std::string classText,targetText;};
    std::array<Names,kAiEntityCapacity> names{};
    double worldTotalUs=0,serviceTotalUs=0;
    bool pauseWanted=false,scopeActive=false;
    bool cleanupDeferred=false;
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
std::atomic<bool> nativePending{false};
std::atomic<unsigned long long> nativeCallbacks{0};
std::atomic<DWORD> nativeThread{0};
thread_local bool servicing=false;
thread_local bool spawningOwned=false;
thread_local bool cleaningOwned=false;
thread_local int traceIgnoreEntity=-1;

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
unsigned CleanupOwned(const char* reason,bool one=false);
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
void BeforeSave(const char* reason,bool write) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    if(servicing || cleaningOwned) {
        // Never free an NPC while a native damage/model/spawn call has that
        // NPC on its stack. Complete the cleanup at the following clock boundary.
        ai.cleanupDeferred=true;ai.recoverDeferred=write;nativePending=true;
        Log("AI_CLEANUP deferred=1 reason=%s",reason);return;
    }
    ai.queue.cancel(AiRequestResult::LifetimeChanged);
    ai.hands.pending=false;ai.handCache.clear();
    if(write)CaptureRecovery();else ClearRecovery(true);
    const auto count=CleanupOwned(reason);
    // A protected/refused allocation stays alive; do not create a duplicate.
    for(auto it=ai.recovery.begin();it!=ai.recovery.end();) {
        if(SameAiAllocation(it->previous,ReadAiAllocation(it->previous,ReadAi))) {
            if(it->tagsHeld)for(auto& tag:it->appearance.tags)reinterpret_cast<ScriptStringFn>(aiProfile.setString)(reinterpret_cast<std::uintptr_t>(&tag),0);
            it=ai.recovery.erase(it);
        } else ++it;
    }
    ai.nextRecoveryTime=-1;nativePending=nativePending || !ai.recovery.empty();
    if(count) {ai.status.lastResult=AiRequestResult::Deleted;ai.nextWorldTime=0;}
}
void SaveReadHook(std::uintptr_t save) {
    BeforeSave("CHECKPOINT_READ",false);
    {std::lock_guard<std::recursive_mutex> lock(aiMutex);ai.names={};ai.nextWorldTime=0;}
    originalSaveRead(save);
}
void SaveWriteHook(bool flag,std::uintptr_t save) {
    // Player feature code takes its lock before the AI lock. Keep that order;
    // a save nested inside an active native AI call defers its AI cleanup.
    const bool suspend=!servicing && !cleaningOwned;
    if(suspend)SuspendRecoilForCheckpoint(true);
    struct Resume {bool active;~Resume(){if(active)SuspendRecoilForCheckpoint(false);}} resume{suspend};
    BeforeSave("CHECKPOINT_WRITE",true);originalSaveWrite(flag,save);
}
int ActorInitHook(std::uintptr_t entity) {
    if(!spawningOwned) {
        std::lock_guard<std::recursive_mutex> lock(aiMutex);
        int count=0,limit=kAiNativeActorCapacity;std::uintptr_t dvar=0;
        if(Value(aiProfile.actorLimitSlot,dvar) && dvar)Value(dvar+0x10,limit);
        if((!ai.owned.empty() || !ai.recovery.empty()) && limit>0 && limit<kAiNativeActorCapacity
            && SetCount(dvar+0x10,limit,kAiNativeActorCapacity)) {
            ai.capacity.applied(dvar,limit,kAiNativeActorCapacity);limit=kAiNativeActorCapacity;
        }
        while(limit>0 && limit<=kAiNativeActorCapacity && Value(aiProfile.actorCountSlot,count) && count>=limit && !ai.owned.empty()) {
            const auto removed=CleanupOwned("CAMPAIGN_NEEDS_SLOT",true);
            ai.status.reclaimed+=removed;
            if(!removed)break;
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
bool PlanActorInit() {
    const auto at=aiProfile.base+kActorInitRva;
    std::array<std::uint8_t,15> observed{};
    if(!ReadAi(at,observed.data(),observed.size()) || observed!=kActorInitPrologue)return false;
    auto* trampoline=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!trampoline)return false;
    std::memcpy(trampoline,observed.data(),observed.size());
    const auto tail=AbsoluteJump(at+observed.size());std::memcpy(trampoline+observed.size(),tail.data(),tail.size());
    DWORD old=0;if(!VirtualProtect(trampoline,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,29);serviceMemory.push_back(trampoline);
    originalInit=reinterpret_cast<InitFn>(trampoline);
    CodeEdit edit;edit.at=at;if(!ReadAi(at,edit.before.data(),16))return false;edit.after=edit.before;
    const auto jump=AbsoluteJump(reinterpret_cast<std::uintptr_t>(&ActorInitHook));
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
void WorkWanted() { ai.status.recovering=static_cast<int>(ai.recovery.size());nativePending=ai.queue.pending() || ai.hands.pending || !ai.owned.empty() || !ai.recovery.empty() || ai.cleanupDeferred || ai.detailed || ai.pauseWanted || ai.pause.owned; }
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
        ai.nextWorldTime=-1;ai.nextRecoveryTime=-1;ai.followCursor=0;
        ai.names={};
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
        && Value(d.address+0x10,d.current) && d.current>=0 && d.current<=kAiNativeActorCapacity;
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
unsigned CleanupOwned(const char* reason,bool one) {
    if(cleaningOwned)return 0;
    cleaningOwned=true;
    struct ClearCleanupFlag {~ClearCleanupFlag(){cleaningOwned=false;}} cleanupFlag;
    unsigned removed=0;
    if(one) {
        // Reserve campaign slots by retiring corpses/hostiles before allies.
        auto priority=[&](const OwnedAi& x) {
            const auto raw=ReadAiAllocation(x.identity,ReadAi);
            return raw.type==kActorCorpseType?0:x.team!=ai.allyTeam?1:2;
        };
        std::stable_sort(ai.owned.begin(),ai.owned.end(),[&](const auto& a,const auto& b){return priority(a)<priority(b);});
    }
    for(auto it=ai.owned.begin();it!=ai.owned.end();) {
        const auto raw=ReadAiAllocation(it->identity,ReadAi);
        if(!raw.valid) {++it;continue;}
        const bool ours=SameAiAllocation(it->identity,raw);
        if(ours && (raw.type==kActorType || raw.type==kActorCorpseType) && raw.number>1) {
            int protectedNumber=-1;
            if(!Value(aiProfile.protectedSlot,protectedNumber) || protectedNumber==raw.number) {++it;continue;}
            Log("AI_DELETE_BEGIN entity=%u serial=%u type=%u reason=%s",raw.number,raw.serial,raw.type,reason);
            if(one && it->team==ai.allyTeam)CaptureRecovery(&it->identity);
            // Native delete sends notifications and performs brain/sentient/VM cleanup.
            reinterpret_cast<DeleteFn>(aiProfile.remove)(raw.number);
            const auto after=ReadAiAllocation(it->identity,ReadAi);
            if(SameAiAllocation(it->identity,after)) {Log("AI_DELETE refused entity=%u",raw.number);++it;continue;}
            ++removed;++ai.status.deleted;
        } else if(ours) {++it;continue;}
        const auto helper=ReadAiAllocation(it->spawner,ReadAi);
        if(SameAiAllocation(it->spawner,helper) && helper.type==kActorSpawnerType)
            reinterpret_cast<EntityFn>(aiProfile.freeEntity)(it->spawner.address);
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
        if(ai.recovery.size()>=kAiNativeActorCapacity)break;
        if(std::any_of(ai.recovery.begin(),ai.recovery.end(),[&](const auto& r){return r.previous.address==owned.identity.address && r.previous.serial==owned.identity.serial;}))continue;
        const auto e=Entity(owned.identity,ai.pool,false);
        if(!owned.identity.matches(e) || e.health<=0)continue;
        RecoveryAi r;r.previous=owned.identity;r.sourceName=owned.sourceName;r.team=owned.team;r.health=e.health;
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
void ReadWorld(const Snapshot&,const AiScope&);
void FollowOwned(const Snapshot& s,const AiScope& scope,int time) {
    AiPoint player{};
    if(!scope.ready || !ReadAi(s.entity+kAiOrigin,player.data(),sizeof(player)) || !FinitePoint(player))return;
    const int ally=PlayerTeam(s);
    // Native notifications may re-enter checkpoint cleanup. Iterate a snapshot,
    // then commit only to a still-owned allocation instead of keeping iterators
    // across engine calls that can erase ownership records.
    std::vector<OwnedAi> ownedSnapshot;
    const auto count=std::min<std::size_t>(4,ai.owned.size());
    ownedSnapshot.reserve(count);
    for(std::size_t n=0;n<count;++n)ownedSnapshot.push_back(ai.owned[(ai.followCursor+n)%ai.owned.size()]);
    if(!ai.owned.empty())ai.followCursor=(ai.followCursor+count)%ai.owned.size();
    for(const auto& original:ownedSnapshot) {
        auto update=original;
        auto* it=&update;
        auto tracked=[&]() {return std::any_of(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==it->identity.address && x.identity.serial==it->identity.serial;});};
        if(!tracked())continue;
        const auto raw=ReadAiAllocation(it->identity,ReadAi);
        if(raw.valid && !SameAiAllocation(it->identity,raw)) {
            const auto helper=ReadAiAllocation(it->spawner,ReadAi);
            if(SameAiAllocation(it->spawner,helper) && helper.type==kActorSpawnerType)reinterpret_cast<EntityFn>(aiProfile.freeEntity)(it->spawner.address);
            for(auto& x:ai.owned)if(x.identity.address==it->identity.address && x.identity.serial==it->identity.serial && x.tagsHeld)ReleaseAppearance(x.appearance);
            ai.owned.erase(std::remove_if(ai.owned.begin(),ai.owned.end(),[&](const auto& x){return x.identity.address==it->identity.address && x.identity.serial==it->identity.serial;}),ai.owned.end());continue;
        }
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
        const auto chosen=ChooseAiTarget(self,ally,s.entity,player,ai.combatants);
        const auto target=chosen.entity;auto goal=chosen.position;const auto distance=chosen.squaredDistance;
        std::uint16_t currentEnemy=0;
        const auto desiredEnemy=target?static_cast<std::uint16_t>((target-scope.pool)/kAiEntityStride+1):0;
        const bool enemyRead=Value(e.sentient+0x44,currentEnemy);
        if(target!=it->target || (target && enemyRead && currentEnemy!=desiredEnemy)) {
            reinterpret_cast<EnemyFn>(aiProfile.setEnemy)(e.sentient,target,1,0);
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
        // A friendly fights in place when it has a nearby enemy. Replacing its
        // goal every half-second interrupts native cover/firing state and paths.
        const bool follow=it->team==ally && (!target || PointDistanceSquared(e.origin,player)>640.f*640.f);
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
        const bool nativeGoalRead=ReadAi(e.actor+0x1498,nativeGoal.data(),sizeof(nativeGoal)) && FinitePoint(nativeGoal);
        const bool redirected=nativeGoalRead && PointDistanceSquared(nativeGoal,goal)>96.f*96.f;
        if((follow || advance) && (NeedsAiGoal(it->hasGoal,it->goal,goal) || redirected)) {
            reinterpret_cast<GoalFn>(aiProfile.setGoal)(e.actor,goal.data(),0);
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
        for(auto& live:ai.owned)if(live.identity.address==it->identity.address && live.identity.serial==it->identity.serial) {live=std::move(update);break;}
    }
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
void SpawnOwned(const AiRequest& request,const Snapshot& s,const AiScope& scope,int time,const RecoveryAi* recovery=nullptr);
void Service() {
    if(servicing || !nativePending.load(std::memory_order_acquire)) return;
    servicing=true;
    struct ClearServiceFlag { ~ClearServiceFlag() { servicing=false; } } clearServiceFlag;
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
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
        ai.cleanupDeferred=false;if(ai.recoverDeferred)CaptureRecovery();else ClearRecovery(true);
        ai.recoverDeferred=false;CleanupOwned("DEFERRED_CHECKPOINT");
        ai.queue.cancel(AiRequestResult::LifetimeChanged);ai.hands.pending=false;ai.handCache.clear();WorkWanted();
    }
    if(s.state==LevelState::PlayerDead && scope.ready) {
        ClearRecovery(true);
        CleanupOwned("PLAYER_DEATH");ai.queue.cancel(AiRequestResult::LifetimeChanged);
        ai.hands.pending=false;ai.hands.result.state=NativeHandBuildState::Canceled;ai.handCache.clear();
        ai.lastServiceTime=time;WorkWanted();return;
    }
    if(!scope.ready) {Pause(s);WorkWanted();return;}
    ai.lastServiceTime=time;
    if(ai.nextWorldTime<0 || time>=ai.nextWorldTime || ai.queue.pending()) {ai.nextWorldTime=time+500;ReadWorld(s,scope);}
    Pause(s);ServiceHands(scope);FollowOwned(s,scope,time);
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
            int health=1;if(hit && Value(e.address+kAiHealth,health) && health<=0)++killed;
            const auto after=Entity(identity,scope.pool,false);
            if(identity.matches(after)) {std::uint8_t current=0;if(Value(e.address+0x139,current) && current==1)SetByte(e.address+0x139,current,damage);}
        }
        ai.status.killed+=killed;Log("AI_KILL_ALL selected=%llu killed=%u",static_cast<unsigned long long>(targets.size()),killed);
        Finish(killed?AiRequestResult::Killed:AiRequestResult::NativeRefused);return;
    }
    SpawnOwned(*request,s,scope,time);
}
void SpawnOwned(const AiRequest& input,const Snapshot& s,const AiScope& scope,int time,const RecoveryAi* recovery) {
    const auto* request=&input;
    const auto source=Entity(request->source,scope.pool);
    const bool friendly=request->operation==AiOperation::SpawnFriendly;
    const int playerTeam=PlayerTeam(s),team=friendly?playerTeam:(playerTeam==kAlliesTeam?kAxisTeam:kAlliesTeam);
    if(!request->source.matches(source) || !HumanSpawner(source)) {Finish(AiRequestResult::IdentityChanged);return;}
    const auto enabled=Bool(aiProfile.spawnEnabledSlot,"g_spawnai"),disabled=Bool(aiProfile.spawnDisabledSlot,"ai_disableSpawn");
    if(!enabled.valid || !enabled.current || !disabled.valid || disabled.current || ai.pauseWanted) {Finish(AiRequestResult::SpawnPaused);return;}
    const auto limit=ActorLimit(); int actorCount=0;
    if(!limit.valid || !Value(aiProfile.actorCountSlot,actorCount) || actorCount<0 || actorCount>=kAiNativeActorCapacity) {Finish(AiRequestResult::Limit);return;}
    if(!recovery && actorCount+static_cast<int>(ai.recovery.size())>=kAiNativeActorCapacity) {Finish(AiRequestResult::Limit);return;}
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
    struct SpawnScope {SpawnScope(){spawningOwned=true;}~SpawnScope(){spawningOwned=false;}};
    std::uintptr_t spawned=0;{SpawnScope spawnScope;spawned=reinterpret_cast<SpawnFn>(aiProfile.spawn)(temporary,0,1,0);}
    if(!spawned) {Finish(AiRequestResult::NativeRefused);return;}
    if(spawned<scope.pool || (spawned-scope.pool)%kAiEntityStride
        || (spawned-scope.pool)/kAiEntityStride>=static_cast<std::uintptr_t>(kAiEntityCapacity-2)) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    // Roll back an allocated NPC before its helper if initialization/validation
    // fails. Otherwise a living NPC can retain a dangling self.spawner reference.
    struct SpawnedRollback {
        AiIdentity identity;bool keep=false;
        ~SpawnedRollback() {
            if(keep)return;
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
    reinterpret_cast<EnemyFn>(aiProfile.setEnemy)(actor.sentient,friendly?0:s.entity,1,0);
    actor=ReadAiEntity(spawned,actor.number,ReadAi);
    if(!actor.valid || actor.team!=team) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    if(recovery && actor.health!=recovery->health)SetCount(actor.address+kAiHealth,actor.health,recovery->health);
    const auto temp=ReadAiEntity(temporary,static_cast<int>((temporary-scope.pool)/kAiEntityStride),ReadAi);
    if(!temp.valid) {Finish(AiRequestResult::NativeReturnedInvalid);return;}
    ai.combatants.push_back(actor);
    ai.owned.push_back({AiIdentity::capture(actor),AiIdentity::capture(temp),team,-1,0,{},false,appearance,time+1000,false,source.classname});cleanup.keep=true;rollback.keep=true;++ai.status.created;
    if(appearance.model) {HoldAppearance(ai.owned.back().appearance);ai.owned.back().tagsHeld=true;}
    ai.status.lastEntity=actor.number;ai.status.lastTeam=team;ai.status.lastPosition=actor.origin;
    ai.status.lastSpawnDistance=std::sqrt(PointDistanceSquared(actor.origin,player));ai.status.placementAttempts=placement->attempts;
    std::snprintf(ai.status.lastName.data(),ai.status.lastName.size(),"%s",source.classname.c_str());
    Log("AI_SPAWN_ACCEPT entity=%u serial=%u actor=%p sentient=%p team=%d health=%d origin=%.1f,%.1f,%.1f distance=%.1f original_spawner_untouched=1",
        actor.number,actor.serial,reinterpret_cast<void*>(actor.actor),reinterpret_cast<void*>(actor.sentient),actor.team,actor.health,
        actor.origin[0],actor.origin[1],actor.origin[2],ai.status.lastSpawnDistance);
    Finish(AiRequestResult::Created);
}
bool InstallService() {
    if(clockInstalled)return true;
    if(!aiProfile.resolved || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128))return false;
    originalClock=reinterpret_cast<ClockFn>(aiProfile.base+kFrameClockRva);
    originalSaveRead=reinterpret_cast<SaveReadFn>(aiProfile.base+kSaveReadRva);
    originalSaveWrite=reinterpret_cast<SaveWriteFn>(aiProfile.base+kSaveWriteRva);
    serviceEdits.clear();
    if(!PlanCall(kFrameClockCallRva,kFrameClockRva,reinterpret_cast<std::uintptr_t>(&ClockHook))
        || !PlanCall(kSaveReadCallRva,kSaveReadRva,reinterpret_cast<std::uintptr_t>(&SaveReadHook))
        || !PlanCall(kSaveWriteCallRva,kSaveWriteRva,reinterpret_cast<std::uintptr_t>(&SaveWriteHook))
        || !PlanActorInit()) {Log("AI_SERVICE profile/code planning failed; no hooks published");return false;}
    // Publish only the intact CALL instruction from the rendering thread.
    // The actor prologue is changed at the following server-clock callback,
    // outside ActorInit execution on the game's actor/VM thread.
    if(!ExchangeCode(serviceEdits.front(),true)) {Log("AI_SERVICE clock call changed; publication refused");return false;}
    clockInstalled=true;
    Log("AI_SERVICE clock_installed=1 lifecycle_install=QUEUED");return true;
}
bool CompleteServiceInstall() {
    if(ai.status.hookInstalled)return true;
    if(!clockInstalled || serviceEdits.size()!=4)return false;
    std::size_t installed=1;
    for(;installed<serviceEdits.size();++installed)if(!ExchangeCode(serviceEdits[installed],true))break;
    if(installed!=serviceEdits.size()) {
        while(installed>1) {--installed;ExchangeCode(serviceEdits[installed],false);}
        Log("AI_SERVICE lifecycle hook transaction rolled back on native thread");return false;
    }
    ai.status.hookInstalled=true;
    Log("AI_SERVICE installed=1 scheduler=SERVER_CLOCK checkpoint_read=1 checkpoint_write=1 mission_slot_reclaim=1 atomic_code_width=16");
    return true;
}
float Distance(const AiEntity& e,const std::array<float,3>& origin) {
    float squared=0;
    for(int n=0;n<3;++n) { const float delta=e.origin[n]-origin[n]; squared+=delta*delta; }
    return std::sqrt(squared);
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
    ai.allyTeam=friendlyTeam;
    const std::string oldEnemy=ai.enemies.empty()?"":ai.enemies[ai.enemyIndex].classname;
    ai.appearances={};
    ai.enemies.clear();ai.friends.clear();ai.enemyPoints.clear();ai.combatants.clear();
    ai.playerPosition=player;ai.playerPositionValid=true;
    ai.status.enemyAlive=0;ai.status.friendlyAlive=0;ai.status.otherAlive=0;ai.status.nativeCount=0;
    for(int n=2;n<count;++n) {
        if(!active[n]) continue;
        const auto type=pool[static_cast<std::size_t>(n)*kAiEntityStride];
        if(type!=kActorType && type!=kActorSpawnerType) continue;
        const auto address=scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride;
        auto snapshotRead=[&](std::uintptr_t at,void* out,std::size_t size) {
            return ReadArena(scope.pool,pool.data(),pool.size(),at,out,size,ReadAi);
        };
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
    const auto limit=ActorLimit();ai.status.engineLimit=limit.valid?limit.current:kAiNativeActorCapacity;
    ai.status.owned=OwnCount();ai.status.ready=s.state==LevelState::Ready;
    QueryPerformanceCounter(&doneAt);ai.status.lastSortUs=(doneAt.QuadPart-decodedAt.QuadPart)*1000000.0/measure.frequency.QuadPart;
    const auto readAfter=GetMemoryReadCounts();ai.status.worldKernelReads+=readAfter.kernel-readBefore.kernel;ai.status.worldImageReads+=readAfter.image-readBefore.image;
}

} // namespace

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
void UpdateAiControls(const Snapshot& s,ULONGLONG,bool detailed) {
    std::unique_lock<std::recursive_mutex> lock(aiMutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    if(!aiProfile.resolved) return;
    ai.detailed=detailed;
    if(detailed && s.state==LevelState::Ready && !clockInstalled)InstallService();
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
    Log("AI_COMBAT_STATS killed=%llu campaign_reclaimed=%llu character_loaded=%d scheduler=SERVER_CLOCK",s.killed,s.reclaimed,s.characterLoaded);
    Log("AI_PERF service_updates=%llu service_average_us=%.1f service_peak_us=%.1f world_reads=%llu world_average_us=%.1f world_peak_us=%.1f max_npcs_per_tick=4 recovery_pending=%d reassertions=%llu render_world_reads=0 disk_flush=CRASH_ONLY",
        s.serviceUpdates,s.serviceAverageUs,s.servicePeakUs,s.worldReads,s.worldAverageUs,s.worldPeakUs,s.recovering,s.combatReassertions);
    Log("AI_SCAN_PERF last_pool_us=%.1f last_decode_us=%.1f last_sort_us=%.1f kernel_reads_per_scan=%.1f image_reads_per_scan=%.1f name_cache_hits=%llu name_cache_misses=%llu",
        s.lastPoolUs,s.lastDecodeUs,s.lastSortUs,s.worldReads?double(s.worldKernelReads)/s.worldReads:0.0,s.worldReads?double(s.worldImageReads)/s.worldReads:0.0,s.nameCacheHits,s.nameCacheMisses);
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
NativeHandBuildResult GetNativeHandBuild(std::uint64_t ticket) {
    std::lock_guard<std::recursive_mutex> lock(aiMutex);
    return ticket && ticket==ai.hands.ticket?ai.hands.result:NativeHandBuildResult{};
}
} // namespace mod
