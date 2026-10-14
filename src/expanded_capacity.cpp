#include "runtime.hpp"
#include "checked_memory.hpp"
#include "native_hooks.hpp"
#include "expanded_actor_layout.hpp"
#include "expanded_capacity_patches.hpp"
#include <algorithm>
#include <climits>
namespace mod {
namespace {
using namespace expansion;
std::atomic<int> capacity{32};
std::uintptr_t host=0,arena=0;
RUNTIME_FUNCTION stackProbeFunctions[]{
    {kThreatProbeRva-0x4000000,kThreatProbeEndRva-0x4000000,kThreatProbeUnwindRva-0x4000000},
    {kAwarenessProbeRva-0x4000000,kAwarenessProbeEndRva-0x4000000,kAwarenessProbeUnwindRva-0x4000000}};
std::vector<SaveField> fields;
thread_local bool legacyRead=false;
using SelectWrite=void(*)(std::uintptr_t,int);
using SelectRead=void(*)(std::uintptr_t,int);
using MemWrite=void(*)(std::uintptr_t,int,const void*);
using MemRead=void(*)(std::uintptr_t,int,void*);
using ActorWrite=void(*)(const SaveField*,const void*,void*,int,std::uintptr_t);
using ActorRead=void(*)(const SaveField*,void*,int,std::uintptr_t);
std::uintptr_t Address(std::uint32_t rva) {return rva>=0x4000000 && rva<0x4800000?arena+(rva-0x4000000):host+rva;}
void HeaderWrite(std::uintptr_t mem,int segment) {
    reinterpret_cast<SelectWrite>(host+0x270080)(mem,segment);
    const PopulationChunkHeader header;
    reinterpret_cast<MemWrite>(host+0x270c90)(mem,sizeof(header),&header);
}
void HeaderRead(std::uintptr_t save,int segment) {
    const auto select=reinterpret_cast<SelectRead>(host+0x1af040);
    select(save,segment);
    PopulationChunkHeader header;
    reinterpret_cast<MemRead>(host+0x26feb0)(save+0x480,sizeof(header),&header);
    const PopulationChunkHeader expected;
    if(header.magic==expected.magic) {
        if(!SupportedPopulationChunk(header)) {
            Log("AI_CHECKPOINT rejected=UNSUPPORTED_EXPANDED_LAYOUT version=%u actors=%u sentients=%u",header.version,header.actors,header.sentients);
            reinterpret_cast<void(*)(int,const char*,...)>(host+0x1fb5f0)(1,"BO1MW2: unsupported expanded AI checkpoint layout");
            RaiseException(0xe000b022,EXCEPTION_NONCONTINUABLE,0,nullptr);
            return;
        }
        legacyRead=false;
    } else {
        // Segment selection resets native compression/RLE state as well as
        // cursor position. A raw cursor rewind would corrupt old save decoding.
        select(save,segment);legacyRead=true;
    }
    *reinterpret_cast<int*>(Address(0x4590000))=legacyRead?32:256;
    *reinterpret_cast<int*>(Address(0x4590004))=legacyRead?50:274;
    Log("AI_CHECKPOINT read_format=%s actors=%d sentients=%d",legacyRead?"STOCK_MIGRATED":"EXPANDED_V1",legacyRead?32:256,legacyRead?50:274);
}
void WriteActor(const SaveField*,const void* source,void*,int,std::uintptr_t mem) {
    std::array<std::uint8_t,kStride> temporary;
    std::memcpy(temporary.data(),source,kStride);
    reinterpret_cast<ActorWrite>(host+0x1adfb0)(fields.data(),source,temporary.data(),kStride,mem);
}
void ReadActor(const SaveField* stock,void* destination,int,std::uintptr_t save) {
    const auto read=reinterpret_cast<ActorRead>(host+0x1adf50);
    if(!legacyRead){read(fields.data(),destination,kStride,save);return;}
    read(stock,destination,0x1558,save);
    std::array<std::uint8_t,kOldStride> legacy{};std::memcpy(legacy.data(),destination,0x1558);
    MigrateLegacyActor(legacy.data(),legacy.size(),destination,kStride);
}
std::vector<std::uint8_t> Hex(const char* text) {
    std::vector<std::uint8_t> result;
    auto digit=[](char c)->unsigned{return c<='9'?c-'0':c-'a'+10;};
    for(;*text;text+=2)result.push_back(static_cast<std::uint8_t>(digit(text[0])*16+digit(text[1])));
    return result;
}
struct Write {std::uintptr_t address;std::vector<std::uint8_t> before,after;};
}
int NativeActorCapacity() {return capacity.load(std::memory_order_acquire);}
void CheckStockCheckpoint(std::uintptr_t save) {
    if(NativeActorCapacity()!=32 || !host)return;
    const auto select=reinterpret_cast<SelectRead>(host+0x1af040);
    select(save,3);PopulationChunkHeader header;
    reinterpret_cast<MemRead>(host+0x26feb0)(save+0x480,sizeof(header),&header);
    select(save,2);
    const PopulationChunkHeader expected;
    if(header.magic==expected.magic) {
        Log("AI_CHECKPOINT rejected=EXPANDED_SAVE_WITH_STOCK_CAPACITY");
        reinterpret_cast<void(*)(int,const char*,...)>(host+0x1fb5f0)(1,"BO1MW2: this checkpoint requires expanded AI. Enable the 256 build or restore your pre-Test22 save backup.");
        RaiseException(0xe000b022,EXCEPTION_NONCONTINUABLE,0,nullptr);
    }
}
bool ExpandedArenaContains(std::uintptr_t at,std::size_t bytes) {
    return capacity.load(std::memory_order_acquire)==256 && at>=arena && ArenaReadable(at-arena,bytes);
}
bool InstallExpandedCapacity(std::uintptr_t base,bool exactResolved) {
    if(capacity.load()==256)return true;
    if(!exactResolved){Log("AI_CAPACITY_EXPANSION enabled=0 reason=PROFILE_UNRESOLVED");return false;}
    host=base;
    wchar_t exe[32768]{};const auto chars=GetModuleFileNameW(nullptr,exe,32768);
    if(chars && chars<32768) {
        std::wstring path(exe);const auto slash=path.find_last_of(L"\\/");
        if(slash!=std::wstring::npos){path.resize(slash+1);path+=L"bo1mw2\\use-stock-ai.flag";
            if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES){Log("AI_CAPACITY_EXPANSION enabled=0 reason=USER_STOCK_OVERRIDE");return false;}}
    }
    int count=-1;std::uintptr_t sentients=0;
    if(!CheckedReadMemory(base+0xd96ee0,&count,4) || count!=0 || !CheckedReadMemory(base+0xd96e98,&sentients,8)) {
        Log("AI_CAPACITY_EXPANSION enabled=0 reason=ACTOR_POOL_NOT_IDLE count=%d",count);return false;
    }
    if(sentients)for(unsigned n=0;n<50;++n) {
        std::uint8_t active=1;
        if(!CheckedReadMemory(sentients+n*0x88+0x82,&active,1) || active) {
            Log("AI_CAPACITY_EXPANSION enabled=0 reason=SENTIENT_POOL_NOT_IDLE index=%u",n);return false;
        }
    }
    // Allocate ABOVE the full image, keeping all 32-bit RIP/level-relative
    // displacements representable. Synthetic fixture RVAs are never live pools.
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const auto granularity=system.dwAllocationGranularity;
    const auto start=(base+0x5000000+granularity-1)&~(static_cast<std::uintptr_t>(granularity)-1);
    for(auto candidate=start;candidate<base+0x60000000;candidate+=granularity) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(candidate),&info,sizeof(info)))break;
        if(info.State!=MEM_FREE || info.RegionSize<kArenaBytes)continue;
        const auto memory=VirtualAlloc(reinterpret_cast<void*>(candidate),kArenaBytes,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
        if(memory){arena=reinterpret_cast<std::uintptr_t>(memory);break;}
    }
    if(!arena){Log("AI_CAPACITY_EXPANSION enabled=0 reason=NEAR_ARENA_ALLOCATION_FAILED");return false;}
    std::vector<SaveField> original;
    for(unsigned n=0;n<400;++n) {
        SaveField field;
        if(!CheckedReadMemory(base+0x340bb0+n*sizeof(field),&field,sizeof(field)))break;
        original.push_back(field);if(!field.kind)break;
    }
    if(original.empty() || original.back().kind){VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;return false;}
    fields=ExpandedSaveFields(original);
    std::vector<Write> writes;
    bool valid=true;
    for(const auto& patch:kCapacityPatches) {
        Write w{Address(patch.rva),Hex(patch.before),Hex(patch.after)};
        if(patch.rva>=kArenaRva && !ArenaReadable(patch.rva-kArenaRva,w.after.size())) {
            Log("AI_CAPACITY_EXPANSION invalid_arena_fragment rva=0x%x",patch.rva);valid=false;break;
        }
        for(const auto& f:patch.fixups) {
            std::int64_t value=static_cast<std::int64_t>(Address(f.target));
            value-=f.relative?static_cast<std::int64_t>(Address(f.end)):static_cast<std::int64_t>(host+f.end);
            if(f.offset+f.width>w.after.size() || (f.width==4 && (value<INT32_MIN || value>INT32_MAX)) || (f.width==1 && (value<-128 || value>127))) {valid=false;break;}
            std::memcpy(w.after.data()+f.offset,&value,f.width);
        }
        if(patch.rva<0x4000000) {
            std::vector<std::uint8_t> current(w.before.size());
            if(w.before.size()!=w.after.size() || !CheckedReadMemory(w.address,current.data(),current.size()) || current!=w.before) {
                Log("AI_CAPACITY_EXPANSION validation_mismatch rva=0x%x",patch.rva);valid=false;break;
            }
        }
        if(!valid)break;
        writes.push_back(std::move(w));
    }
    if(!valid){VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;fields.clear();return false;}
    for(const auto& guard:kArenaGuards) {
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(Address(guard.rva)),guard.bytes,PAGE_NOACCESS,&old)) {
            VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;fields.clear();
            Log("AI_CAPACITY_EXPANSION enabled=0 reason=ARENA_GUARD_PROTECTION_FAILED");return false;
        }
    }
    const std::uintptr_t callbacks[]={reinterpret_cast<std::uintptr_t>(&HeaderWrite),reinterpret_cast<std::uintptr_t>(&HeaderRead),reinterpret_cast<std::uintptr_t>(&WriteActor),reinterpret_cast<std::uintptr_t>(&ReadActor)};
    for(unsigned n=0;n<4;++n){const auto jump=AbsoluteJump(callbacks[n]);std::memcpy(reinterpret_cast<void*>(Address(0x47f0000+n*16)),jump.data(),jump.size());}
    *reinterpret_cast<int*>(Address(0x4590000))=256;*reinterpret_cast<int*>(Address(0x4590004))=274;
    // Preserve already-created idle animation resources until native level
    // teardown/init visits the expanded arrays. Never duplicate ownership.
    std::memcpy(reinterpret_cast<void*>(Address(0x4560000)),reinterpret_cast<void*>(base+0xf50040),32*8);
    std::memcpy(reinterpret_cast<void*>(Address(0x45a0000)),reinterpret_cast<void*>(base+0xf50b40),112*8);
    std::memcpy(reinterpret_cast<void*>(Address(0x45a0a80)),reinterpret_cast<void*>(base+0xf50ec0),112);
    DWORD cursor=0;std::memcpy(&cursor,reinterpret_cast<void*>(base+0xf50f30),4);std::memcpy(reinterpret_cast<void*>(Address(0x45a0bd0)),&cursor,4);
    // Preflight all protection changes, then mutate only after every check.
    struct Protection {Write* write;DWORD old;};std::vector<Protection> protections;
    for(auto& write:writes)if(!write.before.empty()) {
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(write.address),write.after.size(),PAGE_EXECUTE_READWRITE,&old)) {valid=false;break;}
        protections.push_back({&write,old});
    }
    if(!valid) {
        for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->write->address),it->write->after.size(),it->old,&ignored);}
        VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;fields.clear();return false;
    }
    // Publish the probe's own unwind description before any native entry can
    // reach it. Failure leaves all original native instructions intact.
    for(const auto& write:writes)if(write.before.empty())
        std::memcpy(reinterpret_cast<void*>(write.address),write.after.data(),write.after.size());
    if(!RtlAddFunctionTable(stackProbeFunctions,static_cast<DWORD>(std::size(stackProbeFunctions)),arena)) {
        for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->write->address),it->write->after.size(),it->old,&ignored);}
        VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;fields.clear();
        Log("AI_CAPACITY_EXPANSION enabled=0 reason=STACK_PROBE_UNWIND_REGISTRATION_FAILED");return false;
    }
    for(const auto& write:writes)if(!write.before.empty())
        std::memcpy(reinterpret_cast<void*>(write.address),write.after.data(),write.after.size());
    // Idle pools only; G_Init's checked direct references also point here.
    *reinterpret_cast<std::uintptr_t*>(base+0xd96ea0)=Address(0x4000000);
    *reinterpret_cast<std::uintptr_t*>(base+0xd96e98)=Address(0x4500000);
    std::memset(reinterpret_cast<void*>(base+0xf50040),0,32*8);std::memset(reinterpret_cast<void*>(base+0xf50b40),0,112*8);
    for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->write->address),it->write->after.size(),it->old,&ignored);}
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    capacity.store(256,std::memory_order_release);
    Log("AI_CAPACITY_EXPANSION enabled=1 experimental=1 actors=256 sentients=274 stride=0x48c0 snapshot_bytes=17412 temporary_trees=336 patches=%u arena=%p checkpoint=VERSIONED_V1_LEGACY_MIGRATION native_rewind_slots=32 high_actor_collision=CURRENT_SERVER_POSE native_threat_candidates=274 threat_stack_bytes=6760 threat_unwind=REGISTERED awareness_candidates=274 awareness_stack_bytes=4512 stack_probe_unwind_entries=2 arena_guard_pages=8 ai_count_domain=256",static_cast<unsigned>(protections.size()),reinterpret_cast<void*>(arena));
    return true;
}
} // namespace mod
