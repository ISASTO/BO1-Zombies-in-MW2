#include "runtime.hpp"
#include "checked_memory.hpp"
#include "native_hooks.hpp"
#include "expanded_actor_layout.hpp"
#include "expanded_capacity_patches.hpp"
#include <algorithm>
#include <climits>
#include <new>
namespace mod {
namespace {
using namespace expansion;
std::atomic<int> capacity{32};
std::uintptr_t host=0,arena=0;
constexpr RUNTIME_FUNCTION CrowdUnwind(const CrowdRelay& relay) {
    return {relay.begin-kArenaRva,relay.end-kArenaRva,relay.unwind-kArenaRva};
}
auto relayFunctions=[] {
    std::array<RUNTIME_FUNCTION,4+kCrowdRelays.size()+kCrowdBridgeRelays.size()> result{{
    {kThreatProbeRva-0x4000000,kThreatProbeEndRva-0x4000000,kThreatProbeUnwindRva-0x4000000},
    {kAwarenessProbeRva-0x4000000,kAwarenessProbeEndRva-0x4000000,kAwarenessProbeUnwindRva-0x4000000},
    {kGlassAllocateRva-kArenaRva,kGlassAllocateEndRva-kArenaRva,kGlassAllocateUnwindRva-kArenaRva},
    {kGlassFreeRva-kArenaRva,kGlassFreeEndRva-kArenaRva,kGlassFreeUnwindRva-kArenaRva},
    }};
    std::size_t n=4;
    for(const auto& relay:kCrowdRelays)result[n++]=CrowdUnwind(relay);
    for(const auto& relay:kCrowdBridgeRelays)result[n++]=CrowdUnwind(relay);
    return result;
}();
// Separate from executable relays. Bounds are published only after both owned
// buffers and their end guards exist. Native frame generations/reset semantics
// are retained; old static buffers are never enlarged or freed.
struct SkeletonStorage {
    void* client=nullptr;void* server=nullptr;bool published=false;
    std::array<void*,2> surfaces{};
    std::array<void*,2> reverse{},tables{};
    ~SkeletonStorage(){if(!published){if(client)VirtualFree(client,0,MEM_RELEASE);if(server)VirtualFree(server,0,MEM_RELEASE);for(const auto& group:{surfaces,reverse,tables})for(auto* data:group)if(data)VirtualFree(data,0,MEM_RELEASE);}}
    bool Allocate() {
        const auto allocate=[](std::size_t bytes)->void* {
            auto* data=VirtualAlloc(nullptr,bytes+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            DWORD old=0;
            if(data && VirtualProtect(static_cast<std::uint8_t*>(data)+bytes,4096,PAGE_NOACCESS,&old))return data;
            if(data)VirtualFree(data,0,MEM_RELEASE);
            return nullptr;
        };
        client=allocate(kClientSkeletonBytes);if(!client)return false;
        server=allocate(kServerSkeletonBytes);if(!server)return false;
        for(auto& data:surfaces){data=allocate(kCrowdSurfaceBytes);if(!data)return false;}
        for(auto& data:reverse){data=allocate(kSurfaceReverseBytes);if(!data)return false;}
        for(auto& data:tables){data=allocate(kSurfaceTableBytes);if(!data)return false;}
        return true;
    }
};
std::vector<SaveField> fields;
thread_local bool legacyRead=false;
using SelectWrite=void(*)(std::uintptr_t,int);
using SelectRead=void(*)(std::uintptr_t,int);
using MemWrite=void(*)(std::uintptr_t,int,const void*);
using MemRead=void(*)(std::uintptr_t,int,void*);
using ActorWrite=void(*)(const SaveField*,const void*,void*,int,std::uintptr_t);
using ActorRead=void(*)(const SaveField*,void*,int,std::uintptr_t);
std::uintptr_t Address(std::uint32_t rva) {
    if(rva&0x80000000u)return host+(rva&0x7fffffffu);
    return rva>=0x4000000 && rva<0x4800000?arena+(rva-0x4000000):host+rva;
}
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
std::uintptr_t ExpandedResourceStateAddress() {
    return capacity.load(std::memory_order_acquire)==256?Address(kResourcePointersRva):0;
}
void LogExpandedResources() {
    if(capacity.load(std::memory_order_acquire)!=256)return;
    ArmStaticCommandWatch();LogStaticWatchHits();
    unsigned clientUsed=0,serverUsed=0,serverGeneration=0,rebuilds=0,ignored=0;
    if(!CheckedReadMemory(host+0x548cf4,&clientUsed,4) ||
       !CheckedReadMemory(host+0x19c3870,&serverUsed,4) ||
       !CheckedReadMemory(host+0x19c386c,&serverGeneration,4) ||
       !CheckedReadMemory(Address(kResourcePointersRva+16),&rebuilds,4) ||
       !CheckedReadMemory(Address(kResourcePointersRva+20),&ignored,4))return;
    Log("AI_CROWD_RESOURCES client_skeleton_used=%u client_skeleton_limit=%u server_skeleton_used=%u server_skeleton_limit=%u server_skeleton_generation=%u glass_free_list_rebuilds=%u glass_ignored_frees=%u",
        clientUsed,static_cast<unsigned>(kClientSkeletonBytes-16),serverUsed,static_cast<unsigned>(kServerSkeletonBytes-16),serverGeneration,rebuilds,ignored);
    CrowdResourceState state;
    if(!CheckedReadMemory(Address(kResourcePointersRva),&state,sizeof(state)))return;
    Log("AI_RENDER_CROWD skin_limit_bytes=%u skin_limit_vertices=%u gpu_peak_bytes=%u cpu_peak_bytes=%u surface_limit_bytes=%u surface_used_0=%u surface_used_1=%u surface_peak_0=%u surface_peak_1=%u gpu_failures=%u cpu_failures=%u surface_failures=%u bad_frames=%u frame_resets=%u",
        kSkinCacheBytes,kSkinCacheBytes/32,state.gpuPeak,state.cpuPeak,kCrowdSurfaceBytes,
        state.frames[0].used,state.frames[1].used,state.frames[0].peak,state.frames[1].peak,
        state.gpuFailures,state.cpuFailures,state.surfaceFailures,state.badFrames,state.resets);
    Log("AI_RENDER_OVERFLOW entity_buffer=%u models=%u client_models=%u special_models=%u light_cache=%u scene_entities=%u temp_skin=%u skin_cache=%u native_surface_pool=%u native_draw_list=%u scene_dobj_refs=%u",
        state.warnings[0],state.warnings[1],state.warnings[2],state.warnings[3],state.warnings[4],
        state.warnings[5],state.warnings[6],state.warnings[7],state.warnings[8],state.warnings[25],state.warnings[32]);
    // Include every observed category in an ordinary log collection, even if
    // a remaining drawing limit does not crash. This runs only with AI info.
    for(unsigned n=9;n<state.warnings.size();++n)
        if(n!=25 && n!=32 && state.warnings[n])
            Log("AI_RENDER_WARNING category=%u count=%u",n,state.warnings[n]);
    Log("AI_RENDER_HANDLES capacity_per_frame=%u used_0=%u used_1=%u exhausted=%u invalid_records=%u packed_bits=16",
        kSurfaceHandles,state.handleCounts[0],state.handleCounts[1],state.handleFailures,state.mapFailures);
    Log("AI_PHYSICS_GUARDS nonfinite_glass_queries=%u invalid_features=%u first_query_state=%u last_invalid_feature=%u",
        state.invalidGlassQueries,state.invalidFeatures,state.queryClaim,state.lastInvalidFeature);
    Log("AI_GLASS_FRACTURE invalid_fragments=%u output_limit_rejections=%u invalid_inputs=%u last_vertices=%u last_output_bytes=%u geometry_limit_bytes=924",
        state.invalidFragments,state.fragmentOutputRejections,state.fragmentInputRejections,state.lastFragmentVertices,state.lastFragmentBytes);
    const auto rva=[](std::uint64_t address)->unsigned long long {
        return host && address>=host && address<host+0x5000000?address-host:address;
    };
    Log("AI_GLASS_STATE missing_place_shatters=%u last_missing_place=%u rejected_place_damage=%u last_rejected_place=%u last_rejected_caller_rva=0x%llx ignored_free_records=%u",
        state.glassMissingPlaces,state.glassLastMissingPlace,state.glassRejectedDamage,state.glassLastRejectedPlace,
        rva(state.glassLastRejectedCaller),state.glassFreeRingNext);
    Log("AI_COMPASS rejected_actor_slots=%u last_rejected_slot=%u unfound_slot_writes_skipped=%u native_actor_slots=32",
        state.compassRejected,state.compassLastSlot,state.compassWriterSkips);
    {
        // Free-list walk of the expanded native anim-info pool (diagnostic
        // only; bounded, read-only). Record 0 is the circular list head.
        // One bounded copy, then walk locally: no per-record memory queries.
        static std::vector<std::uint8_t> snapshot(kAnimInfoBytes);
        unsigned free=0;
        if(CheckedReadMemory(Address(kAnimInfoRva),snapshot.data(),snapshot.size())) {
            std::uint16_t next=0;std::memcpy(&next,snapshot.data()+0xa,2);
            while(next && next<kAnimInfoRecords && free<kAnimInfoRecords) {++free;std::memcpy(&next,snapshot.data()+std::size_t(next)*64+0xa,2);}
        }
        static std::atomic<unsigned> peak{0};
        const unsigned used=free<kAnimInfoRecords?kAnimInfoRecords-1-free:0;
        if(used>peak.load())peak.store(used);
        Log("AI_ANIM_INFO capacity=%u used=%u peak=%u native_stock_capacity=4095",kAnimInfoRecords-1,used,peak.load());
    }
    static std::atomic<std::uint32_t> printedFrees{0};
    if(state.glassFreeRingNext!=printedFrees.load()) {
        const auto total=state.glassFreeRingNext;
        const auto fresh=total-printedFrees.exchange(total);
        const auto count=std::min<std::uint32_t>(std::min<std::uint32_t>(fresh,total),kGlassFreeRingEntries);
        for(std::uint32_t n=total-count;n<total;++n) {
            const auto& record=state.glassFreeRing[n%kGlassFreeRingEntries];
            Log("AI_GLASS_IGNORED_FREE sequence=%u piece=%u caller_rva=0x%llx",n,record.piece,rva(record.caller));
        }
    }
    static std::atomic<bool> reported{false};
    if(state.queryClaim==2 && !reported.exchange(true)) {
        std::array<unsigned,3> center{},axis{};
        std::uintptr_t reference=0;
        std::memcpy(center.data(),state.query.data()+0x38,sizeof(center));
        std::memcpy(axis.data(),state.query.data()+0x44,sizeof(axis));
        std::memcpy(&reference,state.query.data()+0x90,sizeof(reference));
        Log("AI_PHYSICS_FIRST_BAD_GLASS stage=%u source=%p context=%p reference=%p origin_bits=%08x,%08x,%08x axis_bits=%08x,%08x,%08x",
            state.queryStage,reinterpret_cast<void*>(state.firstQuery),reinterpret_cast<void*>(state.firstVertexData),reinterpret_cast<void*>(reference),
            center[0],center[1],center[2],axis[0],axis[1],axis[2]);
    }
}
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
    // Initialization runs on the first D3D export, before the game's device
    // and native vertex buffers exist. A late install cannot enlarge live GPU
    // resources safely, so keep original code/pools intact in that case.
    for(const auto rva:std::array<std::uint32_t,4>{0x4116590,0x41165a8,
            kRenderFrameBaseRva+0x419d0,kRenderFrameBaseRva+kRenderFrameStride+0x419d0}) {
        std::uintptr_t pointer=0;
        if(!CheckedReadMemory(base+rva,&pointer,sizeof(pointer)) || pointer) {
            Log("AI_CAPACITY_EXPANSION enabled=0 reason=RENDER_STORAGE_ALREADY_INITIALIZED rva=0x%x",rva);return false;
        }
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
    SkeletonStorage skeletons;
    if(!skeletons.Allocate()) {
        VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;
        Log("AI_CAPACITY_EXPANSION enabled=0 reason=GUARDED_SKELETON_STORAGE_FAILED");return false;
    }
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
    auto* resourceState=new(reinterpret_cast<void*>(Address(kResourcePointersRva))) CrowdResourceState{};
    resourceState->clientSkeleton=reinterpret_cast<std::uintptr_t>(skeletons.client);
    resourceState->serverSkeleton=reinterpret_cast<std::uintptr_t>(skeletons.server);
    resourceState->secondFrameLow[0]=static_cast<std::uint32_t>(base+kRenderFrameBaseRva+kRenderFrameStride);
    for(unsigned n=0;n<2;++n) {
        auto& frame=resourceState->frames[n];
        frame.surfaces=reinterpret_cast<std::uintptr_t>(skeletons.surfaces[n]);
        frame.reverse=reinterpret_cast<std::uintptr_t>(skeletons.reverse[n]);
        frame.table=reinterpret_cast<std::uintptr_t>(skeletons.tables[n]);
        resourceState->framePointers[n]=reinterpret_cast<std::uintptr_t>(&frame);
        auto* table=static_cast<std::uintptr_t*>(skeletons.tables[n]);
        for(unsigned i=0;i<kSurfaceHandles;++i)table[i]=base+kRenderFrameBaseRva+n*kRenderFrameStride+0x42980+i*4;
    }
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
    // Publish each relay's own unwind description before any native entry can
    // reach it. Failure leaves all original native instructions intact.
    for(const auto& write:writes)if(write.before.empty())
        std::memcpy(reinterpret_cast<void*>(write.address),write.after.data(),write.after.size());
    if(!RtlAddFunctionTable(relayFunctions.data(),static_cast<DWORD>(std::size(relayFunctions)),arena)) {
        for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->write->address),it->write->after.size(),it->old,&ignored);}
        VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE);arena=0;fields.clear();
        Log("AI_CAPACITY_EXPANSION enabled=0 reason=RELAY_UNWIND_REGISTRATION_FAILED");return false;
    }
    for(const auto& write:writes)if(!write.before.empty())
        std::memcpy(reinterpret_cast<void*>(write.address),write.after.data(),write.after.size());
    // Idle pools only; G_Init's checked direct references also point here.
    *reinterpret_cast<std::uintptr_t*>(base+0xd96ea0)=Address(0x4000000);
    *reinterpret_cast<std::uintptr_t*>(base+0xd96e98)=Address(0x4500000);
    std::memset(reinterpret_cast<void*>(base+0xf50040),0,32*8);std::memset(reinterpret_cast<void*>(base+0xf50b40),0,112*8);
    for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->write->address),it->write->after.size(),it->old,&ignored);}
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    skeletons.published=true;
    capacity.store(256,std::memory_order_release);
    Log("AI_CAPACITY_EXPANSION enabled=1 experimental=1 actors=256 sentients=274 stride=0x48c0 snapshot_bytes=17412 temporary_trees=336 patches=%u arena=%p checkpoint=VERSIONED_V1_LEGACY_MIGRATION native_rewind_slots=32 high_actor_collision=CURRENT_SERVER_POSE native_threat_candidates=274 threat_stack_bytes=6760 threat_unwind=REGISTERED awareness_candidates=274 awareness_stack_bytes=4512 relay_unwind_entries=%u arena_guard_pages=11 anim_info_records=16384 anim_transfer_records=16384 compass_actor_slots=32 skeleton_guard_pages=2 renderer_guard_pages=6 client_skeleton_bytes=9437184 server_skeleton_bytes=8388608 skin_cache_bytes=75497472 skinned_surface_bytes_per_frame=2097152 animated_handles_per_frame=32768 packed_draw_bits=16 render_reservations=BOUNDED_ATOMIC glass_free_list=OCCUPANCY_VALIDATED glass_collision=FINITE_CHECKED ai_count_domain=256",static_cast<unsigned>(protections.size()),reinterpret_cast<void*>(arena),static_cast<unsigned>(relayFunctions.size()));
    return true;
}
} // namespace mod
