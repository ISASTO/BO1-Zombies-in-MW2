// Early native capacity, applied from DllMain before iw4sp.exe runs any game
// code (the proxy is a static import). Test33 stopped at
// "exceeded maximum number of child1 script variables" (Estate 179 actors,
// Contingency ~111). Fails closed: if any original byte differs, nothing is
// written and the game runs stock.
#include "script_vm.hpp"
#include "script_vm_patches.hpp"
#include "script_heap.hpp"
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <climits>
#include <cstring>
#include <cwchar>
#include <vector>
namespace mod {
void Log(const char* format,...);
namespace {
using namespace script_vm;
constexpr std::uint32_t kImageBytes=0x4942000,kTimeStamp=0x6aaaa257;
constexpr std::uint32_t kNativeMt=0x1466080,kNativeMtBitmap=0x1566080,kMtAllocIndex=0x216f00,kScriptError=0x2906b0,kMtError=0x34cf30;
constexpr std::uint32_t kEntityInUse=0xd9b820,kDobjCount=0x1c0f234;
constexpr std::uint32_t kSlotStructs[2]={0x1156e80,0x1157380};
ScriptVmStatus status;
std::uintptr_t host=0,region=0,heapBase=0;
std::uintptr_t Address(std::uint32_t rva){return rva>=kRegionRva?region+(rva-kRegionRva):host+rva;}
std::vector<std::uint8_t> Hex(const char* text){
    std::vector<std::uint8_t> out;
    auto digit=[](char c)->unsigned{return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};
    for(;text[0] && text[1];text+=2)out.push_back(static_cast<std::uint8_t>(digit(text[0])*16+digit(text[1])));
    return out;
}
// ---------------------------------------------------------------- MT heap
constexpr std::size_t kHeapReserve=std::size_t(512)<<20,kHeapCommitStep=std::size_t(8)<<20;
struct SrwLock {SRWLOCK value=SRWLOCK_INIT;void lock(){AcquireSRWLockExclusive(&value);}void unlock(){ReleaseSRWLockExclusive(&value);}};
using Commit=bool(*)(std::uintptr_t,std::size_t);
bool CommitPages(std::uintptr_t at,std::size_t bytes){return VirtualAlloc(reinterpret_cast<void*>(at),bytes,MEM_COMMIT,PAGE_READWRITE)!=nullptr;}
ScriptHeap<SrwLock,Commit> heap;
std::atomic<unsigned> fallbacks{0};
void* ScriptMtAlloc(int size,int type){
    if(size<0 || size>=0x10000) {
        reinterpret_cast<void(*)(const char*)>(host+kScriptError)(reinterpret_cast<const char*>(host+kMtError));
        return nullptr;
    }
    if(void* block=heap.allocate(size))return block;
    // Reserve exhausted: the native tree still reports true exhaustion.
    fallbacks.fetch_add(1);
    const auto index=reinterpret_cast<unsigned(*)(int,int)>(host+kMtAllocIndex)(size,type);
    return reinterpret_cast<void*>(host+kNativeMt+static_cast<std::uintptr_t>(index)*16);
}
void ScriptMtFree(void* pointer,int size){heap.release(pointer,size);}
void ScriptMtReset(){heap.reset();}
void* GuardedBuffer(std::size_t bytes){
    auto* data=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,bytes+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    DWORD old=0;
    if(data && VirtualProtect(data+bytes,4096,PAGE_NOACCESS,&old))return data;
    if(data)VirtualFree(data,0,MEM_RELEASE);
    return nullptr;
}
bool Fail(const char* reason,std::uint32_t rva=0){status.reason=reason;status.failedRva=rva;return false;}
struct Write {std::uintptr_t address;std::vector<std::uint8_t> before,after;};
bool Apply(){
    status.attempted=true;
    host=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));status.host=host;
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    const wchar_t* file=std::wcsrchr(path,L'\\');file=file?file+1:path;
    if(_wcsicmp(file,L"iw4sp.exe"))return Fail("NOT_SP_HOST");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(host+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=kTimeStamp
       || nt->OptionalHeader.SizeOfImage!=kImageBytes)return Fail("UNSUPPORTED_EXECUTABLE");
    // Owned region above the image (32-bit image-relative displacements must
    // reach it) and below the actor-capacity arena's search window.
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const std::uintptr_t step=system.dwAllocationGranularity;
    for(std::uintptr_t at=(host+kRegionRva+step-1)&~(step-1);at+kRegionBytes<host+0x70000000;at+=step) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info)))break;
        if(info.State!=MEM_FREE || info.RegionSize<kRegionBytes)continue;
        if(void* p=VirtualAlloc(reinterpret_cast<void*>(at),kRegionBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)){region=reinterpret_cast<std::uintptr_t>(p);break;}
    }
    if(!region)return Fail("REGION_ALLOCATION_FAILED");
    status.region=region;
    const auto release=[&](const char* reason,std::uint32_t rva=0){
        VirtualFree(reinterpret_cast<void*>(region),0,MEM_RELEASE);region=0;status.region=0;
        if(heapBase){heap.attach(0,0,0,&CommitPages);VirtualFree(reinterpret_cast<void*>(heapBase),0,MEM_RELEASE);heapBase=0;}
        for(auto& save:status.saves)if(save){VirtualFree(reinterpret_cast<void*>(save),0,MEM_RELEASE);save=0;}
        return Fail(reason,rva);
    };
    heapBase=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,kHeapReserve,MEM_RESERVE,PAGE_NOACCESS));
    if(!heapBase)return release("MT_HEAP_RESERVE_FAILED");
    heap.attach(heapBase,kHeapReserve,kHeapCommitStep,&CommitPages);
    status.heap=heapBase;
    for(unsigned n=0;n<2;++n) {
        status.saves[n]=reinterpret_cast<std::uintptr_t>(GuardedBuffer(kSaveBytes));
        if(!status.saves[n])return release("CHECKPOINT_BUFFER_FAILED");
    }
    std::vector<Write> writes;writes.reserve(kPatches.size());
    for(const auto& patch:kPatches) {
        Write w{Address(patch.rva),Hex(patch.before),Hex(patch.after)};
        if(patch.rva>=kRegionRva) {
            if(patch.rva-kRegionRva+w.after.size()>0x1000)return release("RELAY_LAYOUT",patch.rva);
        } else {
            if(w.before.size()!=w.after.size() || std::memcmp(reinterpret_cast<const void*>(w.address),w.before.data(),w.before.size()))
                return release("ORIGINAL_BYTES_DIFFER",patch.rva);
        }
        for(const auto& f:patch.fixups) {
            std::int64_t value=static_cast<std::int64_t>(Address(f.target));
            value-=f.relative?static_cast<std::int64_t>(Address(f.end)):static_cast<std::int64_t>(host+f.end);
            if(f.width!=4 || f.offset+4>w.after.size() || value<INT32_MIN || value>INT32_MAX)return release("FIXUP_RANGE",patch.rva);
            const auto v=static_cast<std::int32_t>(value);std::memcpy(w.after.data()+f.offset,&v,4);
        }
        writes.push_back(std::move(w));
    }
    // Owned data: counters start at zero (VirtualAlloc), cells published.
    const std::uintptr_t cells[]={reinterpret_cast<std::uintptr_t>(&ScriptMtAlloc),reinterpret_cast<std::uintptr_t>(&ScriptMtFree),
        reinterpret_cast<std::uintptr_t>(&ScriptMtReset),heapBase,kHeapReserve};
    std::memcpy(reinterpret_cast<void*>(Address(kMtCellsRva)),cells,sizeof(cells));
    std::memcpy(reinterpret_cast<void*>(Address(kSaveCellsRva)),status.saves.data(),sizeof(std::uintptr_t)*2);
    for(const auto& w:writes)if(w.before.empty())std::memcpy(reinterpret_cast<void*>(w.address),w.after.data(),w.after.size());
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(region),0x1000,PAGE_EXECUTE_READ,&old)
       || !VirtualProtect(reinterpret_cast<void*>(Address(kListGuardRva)),0x1000,PAGE_NOACCESS,&old)
       || !VirtualProtect(reinterpret_cast<void*>(Address(kDobjGuardRva)),0x1000,PAGE_NOACCESS,&old))return release("REGION_PROTECTION_FAILED");
    static std::array<RUNTIME_FUNCTION,kRelays.size()> functions{};
    for(std::size_t n=0;n<kRelays.size();++n)functions[n]={kRelays[n].begin-kRegionRva,kRelays[n].end-kRegionRva,kRelays[n].unwind-kRegionRva};
    if(!RtlAddFunctionTable(functions.data(),static_cast<DWORD>(functions.size()),region))return release("RELAY_UNWIND_REGISTRATION_FAILED");
    // Preflight every protection change, then write all native edits.
    struct Protection {std::uintptr_t address;std::size_t bytes;DWORD old;};
    std::vector<Protection> protections;
    for(const auto& w:writes)if(!w.before.empty()) {
        DWORD previous=0;
        if(!VirtualProtect(reinterpret_cast<void*>(w.address),w.after.size(),PAGE_EXECUTE_READWRITE,&previous)) {
            for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->address),it->bytes,it->old,&ignored);}
            RtlDeleteFunctionTable(functions.data());
            return release("NATIVE_PROTECTION_FAILED");
        }
        protections.push_back({w.address,w.after.size(),previous});
    }
    for(const auto& w:writes)if(!w.before.empty())std::memcpy(reinterpret_cast<void*>(w.address),w.after.data(),w.after.size());
    for(auto it=protections.rbegin();it!=protections.rend();++it){DWORD ignored;VirtualProtect(reinterpret_cast<void*>(it->address),it->bytes,it->old,&ignored);}
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    status.patches=static_cast<unsigned>(protections.size());status.relays=static_cast<unsigned>(functions.size());
    status.enabled=true;status.reason="OK";
    return true;
}
template<class T> T Read(std::uintptr_t at){T v{};std::memcpy(&v,reinterpret_cast<const void*>(at),sizeof(v));return v;}
} // namespace

bool InstallScriptVmExpansion(){return Apply();}
const ScriptVmStatus& GetScriptVmStatus(){return status;}
ScriptVmUsage ReadScriptVmUsage(){
    ScriptVmUsage u;
    if(!status.enabled)return u;
    u.poolCapacity=kPoolStride-2;u.childCapacity=u.poolCapacity*kPools;
    for(unsigned k=0;k<kPools;++k){
        u.children[k]=Read<unsigned>(Address(kCountsRva)+k*4);u.childTotal+=u.children[k];
        if(u.children[k]>u.childPeakPool)u.childPeakPool=u.children[k];
    }
    // Parent objects: entries 1..0x9000; the first kPools are list heads.
    const auto list=Address(kListRva);
    u.parentCapacity=kParentEnd-1-kPools;
    for(unsigned id=kPools;id<kParentEnd-1;++id)if(Read<std::uint32_t>(list+(id+1)*24+0x10)&0x60)++u.parents;
    unsigned freeNodes=0;
    for(unsigned n=0;n<0x800;++n){auto bits=Read<std::uint32_t>(host+kNativeMtBitmap+n*4);while(bits){bits&=bits-1;++freeNodes;}}
    u.mtNativeUsedNodes=u.mtNativeNodes-freeNodes;
    const auto h=heap.stats();
    u.heapUsed=h.used;u.heapPeak=h.peak;u.heapReserved=h.reserved;u.heapCommitted=h.committed;
    u.heapAllocations=h.allocations;u.heapFallbacks=fallbacks.load();u.heapRejectedFrees=h.rejectedFrees;u.heapSizeMismatches=h.sizeMismatches;
    for(unsigned n=0;n<2;++n){
        // Memfile at slot+0x480: buffer, size(+8), cursor(+0x10).
        u.saveSize[n]=Read<std::uint32_t>(host+kSlotStructs[n]+0x488);u.saveUsed[n]=Read<std::uint32_t>(host+kSlotStructs[n]+0x490);
    }
    for(unsigned n=0;n<2048;++n)if(Read<std::uint8_t>(host+kEntityInUse+n))++u.entitiesInUse;
    u.dobjTransfers=Read<std::uint32_t>(host+kDobjCount);u.dobjCapacity=kDobjRecords;
    u.valid=true;return u;
}
const char* ScriptVmSpawnRefusal(const ScriptVmUsage& u){
    if(!u.valid)return nullptr;
    // Headroom is sized to volatility, not one actor: Test34 Contingency at
    // 160-200 actors swung by ~7,000 parent objects within 10 s of combat
    // (threads), and ~2.4 entities per actor.
    const unsigned parentMargin=std::max(4096u,u.parentCapacity/8);
    if(u.childTotal+16384>u.childCapacity)return "SCRIPT_CHILD_VARIABLES";
    if(u.childPeakPool+4096>u.poolCapacity)return "SCRIPT_CHILD_POOL";
    if(u.parents+parentMargin>u.parentCapacity)return "SCRIPT_PARENT_VARIABLES";
    if(u.mtNativeUsedNodes+4096>u.mtNativeNodes)return "SCRIPT_STRINGS";
    if(u.entitiesInUse+128>u.entityCapacity)return "ENTITIES";
    return nullptr;
}
const char* ScriptVmEmergency(const ScriptVmUsage& u){
    if(!u.valid)return nullptr;
    if(u.parents+2048>u.parentCapacity)return "SCRIPT_PARENT_VARIABLES";
    if(u.childPeakPool+1024>u.poolCapacity)return "SCRIPT_CHILD_POOL";
    if(u.mtNativeUsedNodes+1024>u.mtNativeNodes)return "SCRIPT_STRINGS";
    if(u.entitiesInUse+32>u.entityCapacity)return "ENTITIES";
    return nullptr;
}
void LogScriptVm(bool detailed){
    if(!status.enabled){Log("SCRIPT_VM enabled=0 attempted=%d reason=%s rva=0x%x",status.attempted,status.reason,status.failedRva);return;}
    if(!detailed){
        Log("SCRIPT_VM enabled=1 pools=%u pool_capacity=%u parents_capacity=%u region=%p patches=%u relays=%u mt_heap=%p mt_heap_reserve=%llu checkpoint_slot_bytes=%u checkpoint_slots=%p,%p dobj_transfer_records=%u",
            kPools,kPoolStride-2,kParentEnd-1-kPools,reinterpret_cast<void*>(region),status.patches,status.relays,reinterpret_cast<void*>(heapBase),
            static_cast<unsigned long long>(kHeapReserve),kSaveBytes,reinterpret_cast<void*>(status.saves[0]),reinterpret_cast<void*>(status.saves[1]),kDobjRecords);
        return;
    }
    const auto u=ReadScriptVmUsage();
    static unsigned peakChildren=0,peakParents=0,peakNodes=0,peakEntities=0,peakSave=0;
    if(u.childTotal>peakChildren)peakChildren=u.childTotal;
    if(u.parents>peakParents)peakParents=u.parents;
    if(u.mtNativeUsedNodes>peakNodes)peakNodes=u.mtNativeUsedNodes;
    if(u.entitiesInUse>peakEntities)peakEntities=u.entitiesInUse;
    for(auto v:u.saveUsed)if(v>peakSave)peakSave=v;
    Log("SCRIPT_VM_USAGE children=%u/%u peak=%u pools=%u,%u,%u,%u,%u,%u,%u,%u pool_limit=%u parents=%u/%u peak=%u",
        u.childTotal,u.childCapacity,peakChildren,u.children[0],u.children[1],u.children[2],u.children[3],u.children[4],u.children[5],u.children[6],u.children[7],
        u.poolCapacity,u.parents,u.parentCapacity,peakParents);
    Log("SCRIPT_MEMORY strings_tree_nodes=%u/%u peak=%u pointer_heap_bytes=%llu peak=%llu committed=%llu reserve=%llu allocations=%u native_fallbacks=%u rejected_frees=%u size_mismatches=%u",
        u.mtNativeUsedNodes,u.mtNativeNodes,peakNodes,static_cast<unsigned long long>(u.heapUsed),static_cast<unsigned long long>(u.heapPeak),
        static_cast<unsigned long long>(u.heapCommitted),static_cast<unsigned long long>(u.heapReserved),u.heapAllocations,u.heapFallbacks,
        u.heapRejectedFrees,u.heapSizeMismatches);
    Log("ENGINE_LIMITS entities=%u/%u peak=%u checkpoint_cursor=%u,%u checkpoint_size=%u,%u checkpoint_peak=%u dobj_transfers=%u/%u",
        u.entitiesInUse,u.entityCapacity,peakEntities,u.saveUsed[0],u.saveUsed[1],u.saveSize[0],u.saveSize[1],peakSave,u.dobjTransfers,u.dobjCapacity);
}
} // namespace mod
