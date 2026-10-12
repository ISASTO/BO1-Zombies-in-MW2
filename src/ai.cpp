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
struct AiProfile {
    std::uintptr_t base=0;
    std::size_t size=0;
    bool resolved=false;
    std::uintptr_t think=0,thinkSlot=0,poolSlot=0,countSlot=0,activeArray=0,timeSlot=0;
    std::uintptr_t spawnEnabledSlot=0,spawnDisabledSlot=0,stringPoolSlot=0,spawn=0,remove=0,protectedSlot=0,fixedPool=0;
} aiProfile;
struct AiRuntime {
    AiStatus status;
    AiRequestQueue queue;
    std::array<AiIdentity,kAiOwnedLimit> owned{};
    std::vector<AiEntity> enemies,friends;
    int enemyIndex=0,friendlyIndex=0;
    ULONGLONG nextRead=0;
    bool pauseWanted=false,scopeActive=false;
    ScalarOwnership<std::uint8_t> pause;
    std::uintptr_t client=0,pool=0;
    int commandTime=0,lastServiceTime=-1;
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
AiEntity Entity(const AiIdentity& identity,std::uintptr_t pool) {
    const auto address=pool+static_cast<std::uintptr_t>(identity.number)*kAiEntityStride;
    if(address!=identity.address) return {};
    auto e=ReadAiEntity(address,identity.number,ReadAi); e.classname=ClassName(e.classnameId); return e;
}
int OwnCount() { return static_cast<int>(std::count_if(ai.owned.begin(),ai.owned.end(),[](const auto& x){return x.address!=0;})); }
void Lifetime(const AiScope& s) {
    const bool changed=!s.ready || (ai.scopeActive && (ai.client!=s.client || ai.pool!=s.pool || std::strcmp(ai.map,s.map)
        || s.commandTime<ai.commandTime));
    if(changed) {
        if(ai.queue.pending()) { ai.queue.cancel(AiRequestResult::LifetimeChanged); ai.status.lastResult=AiRequestResult::LifetimeChanged; }
        nativePending=false; ai.status.pending=false; ai.owned={}; ai.scopeActive=false;
        ai.enemies.clear(); ai.friends.clear(); ai.nextRead=0; ai.lastServiceTime=-1;
    }
    if(s.ready) {
        ai.scopeActive=true; ai.client=s.client; ai.pool=s.pool; ai.commandTime=s.commandTime;
        std::strncpy(ai.map,s.map,sizeof(ai.map)-1); ai.map[sizeof(ai.map)-1]='\0';
    }
}
void Finish(AiRequestResult result) {
    ai.status.lastResult=result; ai.status.pending=ai.queue.pending();
    nativePending=ai.queue.pending(); ai.nextRead=0;
    Log("AI_RESULT result=%s native_thread=%lu owned=%d",AiResultText(result),GetCurrentThreadId(),OwnCount());
}
void Service() {
    if(servicing || !nativePending.load(std::memory_order_acquire)) return;
    servicing=true;
    struct ClearServiceFlag { ~ClearServiceFlag() { servicing=false; } } clearServiceFlag;
    std::lock_guard<std::mutex> lock(aiMutex);
    const auto s=ReadSnapshot(); const auto scope=Scope(s);
    int time=0;
    if(!Value(aiProfile.timeSlot,time) || time==ai.lastServiceTime) { servicing=false; return; }
    ai.lastServiceTime=time;
    const auto request=ai.queue.take(scope);
    if(!request) { Finish(ai.queue.reason()); servicing=false; return; }
    if(request->operation==AiOperation::RemoveOwned) {
        unsigned removed=0; bool protectedActor=false;
        for(auto& owned:ai.owned) if(owned.address) {
            const auto entity=Entity(owned,scope.pool);
            if(owned.matches(entity) && entity.type==kActorType && entity.number>1) {
                int protectedNumber=-1;
                if(Value(aiProfile.protectedSlot,protectedNumber) && protectedNumber!=entity.number) {
                    Log("AI_DELETE_BEGIN entity=%u serial=%u",entity.number,entity.serial);
                    reinterpret_cast<DeleteFn>(aiProfile.remove)(entity.number);
                    ++removed; ++ai.status.deleted;
                } else { protectedActor=true; continue; }
            }
            owned={};
        }
        Finish(removed?AiRequestResult::Deleted:protectedActor?AiRequestResult::Protected:AiRequestResult::IdentityChanged); servicing=false; return;
    }
    const auto source=Entity(request->source,scope.pool);
    const int team=request->operation==AiOperation::SpawnFriendly?kAlliesTeam:kAxisTeam;
    if(!request->source.matches(source) || !EligibleSpawner(source,team)) {
        Finish(AiRequestResult::IdentityChanged); servicing=false; return;
    }
    const auto enabled=Bool(aiProfile.spawnEnabledSlot,"g_spawnai");
    const auto disabled=Bool(aiProfile.spawnDisabledSlot,"ai_disableSpawn");
    if(!enabled.valid || !enabled.current || !disabled.valid || disabled.current) {
        Finish(AiRequestResult::SpawnPaused); servicing=false; return;
    }
    int count=0;
    if(!Value(aiProfile.countSlot,count) || count>=kAiEntityCapacity-2 || OwnCount()>=kAiOwnedLimit) {
        Finish(AiRequestResult::Limit); servicing=false; return;
    }
    if(source.lastSpawn>=time) { Finish(AiRequestResult::NativeRefused); servicing=false; return; }
    const auto budget=SpawnerBudgetLease::plan(source.spawnCount);
    if(!SetCount(source.address+kAiSpawnCount,source.spawnCount,budget.submitted)) {
        Finish(AiRequestResult::IdentityChanged); servicing=false; return;
    }
    Log("AI_SPAWN_BEGIN team=%d source=%u serial=%u classname=%s budget=%d submitted=%d native_thread=%lu",
        team,source.number,source.serial,source.classname.c_str(),budget.original,budget.submitted,GetCurrentThreadId());
    // Same four-argument Windows x64 call as the exact dospawn builtin, no force mode.
    const auto spawned=reinterpret_cast<SpawnFn>(aiProfile.spawn)(source.address,0,0,0);
    const auto after=Entity(request->source,scope.pool);
    if(request->source.matches(after)) {
        if(budget.canRestore(after.spawnCount)) SetCount(after.address+kAiSpawnCount,after.spawnCount,budget.original);
        if(spawned && after.lastSpawn==source.lastSpawn) SetCount(after.address+kAiLastSpawn,after.lastSpawn,time);
    }
    if(!spawned) { Finish(AiRequestResult::NativeRefused); servicing=false; return; }
    if(spawned<scope.pool || (spawned-scope.pool)%kAiEntityStride ||
        (spawned-scope.pool)/kAiEntityStride>=static_cast<std::uintptr_t>(kAiEntityCapacity-2)) {
        Finish(AiRequestResult::NativeReturnedInvalid); servicing=false; return;
    }
    const int index=static_cast<int>((spawned-scope.pool)/kAiEntityStride);
    const auto actor=ReadAiEntity(spawned,index,ReadAi);
    if(!actor.valid || actor.type!=kActorType) {
        Finish(AiRequestResult::NativeReturnedInvalid); servicing=false; return;
    }
    for(auto& slot:ai.owned) if(!slot.address) { slot=AiIdentity::capture(actor); break; }
    ++ai.status.created;
    Log("AI_SPAWN_ACCEPT entity=%u serial=%u actor=%p sentient=%p team=%d expected_team=%d team_initializing=%d health=%d origin=%.1f,%.1f,%.1f",
        actor.number,actor.serial,reinterpret_cast<void*>(actor.actor),reinterpret_cast<void*>(actor.sentient),actor.team,
        team,actor.team!=team,actor.health,actor.origin[0],actor.origin[1],actor.origin[2]);
    Finish(AiRequestResult::Created); servicing=false;
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
        ai.queue.cancel(AiRequestResult::NativeReturnedInvalid); nativePending=false;
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
    if(!scope.ready || !Value(aiProfile.countSlot,count) || count<2 || count>kAiEntityCapacity-2) { ai.status.ready=false; return; }
    std::vector<std::uint8_t> pool(static_cast<std::size_t>(count)*kAiEntityStride);
    std::vector<std::uint8_t> active(static_cast<std::size_t>(count));
    if(!ReadAi(scope.pool,pool.data(),pool.size()) || !ReadAi(aiProfile.activeArray,active.data(),active.size())) { ai.status.ready=false; return; }
    std::array<float,3> playerOrigin{};
    if(!ReadAi(s.entity+kAiOrigin,playerOrigin.data(),sizeof(playerOrigin))) { ai.status.ready=false; return; }
    const auto oldEnemy=ai.enemies.empty()?0:ai.enemies[ai.enemyIndex].number;
    const auto oldFriendly=ai.friends.empty()?0:ai.friends[ai.friendlyIndex].number;
    ai.enemies.clear(); ai.friends.clear();
    ai.status.enemyAlive=0; ai.status.friendlyAlive=0; ai.status.otherAlive=0; ai.status.nativeCount=0;
    for(int n=2;n<count;++n) {
        if(!active[n]) continue;
        const auto type=pool[static_cast<std::size_t>(n)*kAiEntityStride];
        if(type!=kActorType && type!=kActorSpawnerType) continue;
        const auto address=scope.pool+static_cast<std::uintptr_t>(n)*kAiEntityStride;
        auto snapshotRead=[&](std::uintptr_t at,void* out,std::size_t size) {
            if(at==address && size==0x188) { std::memcpy(out,pool.data()+static_cast<std::size_t>(n)*kAiEntityStride,size); return true; }
            return ReadAi(at,out,size);
        };
        auto e=ReadAiEntity(address,n,snapshotRead);
        if(!e.valid) continue;
        if(e.type==kActorType) {
            ++ai.status.nativeCount;
            if(e.health>0) {
                if(e.team==kAxisTeam) ++ai.status.enemyAlive;
                else if(e.team==kAlliesTeam) ++ai.status.friendlyAlive;
                else ++ai.status.otherAlive;
            }
        } else {
            e.classname=ClassName(e.classnameId);
            if(EligibleSpawner(e,kAxisTeam)) ai.enemies.push_back(e);
            else if(EligibleSpawner(e,kAlliesTeam)) ai.friends.push_back(e);
        }
    }
    auto sort=[&](auto& list) {
        std::sort(list.begin(),list.end(),[&](const auto& a,const auto& b){const auto da=Distance(a,playerOrigin),db=Distance(b,playerOrigin);return da==db?a.number<b.number:da<db;});
        if(list.size()>kAiSpawnerListLimit) list.resize(kAiSpawnerListLimit);
    };
    sort(ai.enemies); sort(ai.friends);
    auto selection=[](const auto& list,int old) {
        for(std::size_t n=0;n<list.size();++n) if(list[n].number==old) return static_cast<int>(n);
        return 0;
    };
    ai.enemyIndex=selection(ai.enemies,oldEnemy); ai.friendlyIndex=selection(ai.friends,oldFriendly);
    ai.status.enemySpawners=static_cast<int>(ai.enemies.size()); ai.status.friendlySpawners=static_cast<int>(ai.friends.size());
    ai.status.enemySpawner=ai.enemies.empty()?0:ai.enemyIndex+1;
    ai.status.friendlySpawner=ai.friends.empty()?0:ai.friendlyIndex+1;
    ai.status.enemyName={}; ai.status.friendlyName={};
    if(!ai.enemies.empty()) {
        std::snprintf(ai.status.enemyName.data(),ai.status.enemyName.size(),"%s",ai.enemies[ai.enemyIndex].classname.c_str());
        ai.status.enemyDistance=Distance(ai.enemies[ai.enemyIndex],playerOrigin);
    }
    if(!ai.friends.empty()) {
        std::snprintf(ai.status.friendlyName.data(),ai.status.friendlyName.size(),"%s",ai.friends[ai.friendlyIndex].classname.c_str());
        ai.status.friendlyDistance=Distance(ai.friends[ai.friendlyIndex],playerOrigin);
    }
    for(auto& owned:ai.owned) if(owned.address && !owned.matches(Entity(owned,scope.pool))) owned={};
    ai.status.owned=OwnCount(); ai.status.ready=true;
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
    if(!think || !dispatch || !array || !fields || !method || !methodEntry || !spawn || !init || !links || !sentientLinks || !handler || !actorType || !actorSelf || !remove || !guard
        || !serial || !serialCommit || !strings || !spawnReg || !disableReg) return;
    aiProfile.think=think; aiProfile.thinkSlot=Rip(dispatch,12,3,7)+kActorHandler*80;
    aiProfile.poolSlot=Rip(array,12,3,7); aiProfile.countSlot=Rip(array,0,2,6); aiProfile.activeArray=Rip(array,24,3,7);
    aiProfile.timeSlot=Rip(think,25,2,6); aiProfile.spawn=spawn; aiProfile.remove=remove;
    aiProfile.spawnEnabledSlot=Rip(init,26,3,7); aiProfile.spawnDisabledSlot=Rip(spawn,13,3,7);
    aiProfile.stringPoolSlot=Rip(strings,4,3,7); aiProfile.protectedSlot=Rip(guard,4,2,6);
    aiProfile.fixedPool=Rip(remove,51,3,7);
    std::uintptr_t original=0;
    aiProfile.resolved=InHost(aiProfile.thinkSlot,8) && Value(aiProfile.thinkSlot,original) && original==think
        && Call(method,11)==spawn && method>methodEntry && method-methodEntry<256
        && Rip(methodEntry,24,2,6)==aiProfile.timeSlot && Rip(method,31,2,6)==aiProfile.timeSlot
        && Rip(spawnReg,27,3,7)==aiProfile.spawnEnabledSlot
        && Rip(disableReg,36,3,7)==aiProfile.spawnDisabledSlot && Rip(serial,22,3,7)==aiProfile.poolSlot
        && InHost(aiProfile.poolSlot,8) && InHost(aiProfile.countSlot,4) && InHost(aiProfile.activeArray,kAiEntityCapacity)
        && InHost(aiProfile.timeSlot,4) && InHost(aiProfile.stringPoolSlot,8) && InHost(aiProfile.protectedSlot,4)
        && InHost(aiProfile.fixedPool,kAiEntityStride*kAiEntityCapacity) && MethodEntry("delete",remove) && MethodEntry("dospawn",methodEntry)
        && TeamNames() && EntityFields();
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
        ai.status.ready=false; ai.status.owned=0; ai.status.enemySpawner=0; ai.status.friendlySpawner=0;
        ai.status.enemySpawners=0; ai.status.friendlySpawners=0;
        ai.status.enemyAlive=0; ai.status.friendlyAlive=0; ai.status.otherAlive=0; ai.status.nativeCount=0;
        ai.status.enemyName={}; ai.status.friendlyName={}; return;
    }
    if(now>=ai.nextRead) { ai.nextRead=now+500; ReadWorld(s,scope); }
    ai.status.pending=ai.queue.pending();
}
void RequestAiOperation(AiOperation op,const Snapshot& s) {
    std::lock_guard<std::mutex> lock(aiMutex);
    const auto scope=Scope(s); Lifetime(scope);
    if(!scope.ready || !ai.status.ready || ai.status.nativeCount==0) { Finish(AiRequestResult::NotReady); return; }
    if(ai.queue.pending()) { ai.status.lastResult=AiRequestResult::Busy; return; }
    if(op!=AiOperation::RemoveOwned && OwnCount()>=kAiOwnedLimit) { Finish(AiRequestResult::Limit); return; }
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
}
} // namespace mod
