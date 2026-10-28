// Test44: a larger loaded_sound pool (see include/asset_pools.hpp and the
// Test44 section of include/zombie_bindings.hpp). Runs from DllMain: the
// proxy is a static import of iw4sp.exe, so this happens before WinMain and
// therefore before DB_Init links the pools. Kernel32 only.
#include "asset_pools.hpp"
#include "zombie_bindings.hpp"
#include <windows.h>
#include <cstring>
#include <cwchar>

namespace mod {
void Log(const char* format,...);
namespace assetpools {
namespace {
constexpr std::uint32_t kImageBytes=0x4942000,kTimeStamp=0x6aaaa257;
Status status;
template<class T> T At(std::uintptr_t at) {T v{};std::memcpy(&v,reinterpret_cast<const void*>(at),sizeof(v));return v;}
template<std::size_t N> bool Same(std::uintptr_t at,const std::array<std::uint8_t,N>& bytes) {
    return !std::memcmp(reinterpret_cast<const void*>(at),bytes.data(),N);
}
std::uintptr_t Slot(std::uintptr_t host,std::uintptr_t table,int type) {return host+table+static_cast<std::uintptr_t>(type)*8;}
bool Fail(const char* reason) {status.reason=reason;return false;}
} // namespace

const char* Refusal(std::uintptr_t host) {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return "NOT_AN_IMAGE";
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(host+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=kTimeStamp || nt->OptionalHeader.SizeOfImage!=kImageBytes)
        return "UNSUPPORTED_EXECUTABLE";
    constexpr int t=kAssetTypeLoadedSound;
    const char* name=reinterpret_cast<const char*>(At<std::uintptr_t>(Slot(host,kAssetNamesRva,t)));
    if(reinterpret_cast<std::uintptr_t>(name)<host || reinterpret_cast<std::uintptr_t>(name)>=host+kImageBytes-16 || std::strcmp(name,"loaded_sound"))
        return "ASSET_NAME_DIFFERS";
    if(At<int>(host+kPoolSizeRva+static_cast<std::uintptr_t>(t)*4)!=kLoadedSoundStockCount) return "POOL_SIZE_DIFFERS";
    if(At<std::uintptr_t>(Slot(host,kXAssetPoolRva,t))!=host+kLoadedSoundPoolRva) return "POOL_POINTER_DIFFERS";
    if(At<std::uintptr_t>(Slot(host,kPoolInitTableRva,t))!=host+kLoadedSoundPoolInitRva || !Same(host+kLoadedSoundPoolInitRva,kLoadedSoundPoolInitBytes))
        return "POOL_INIT_DIFFERS";
    if(At<std::uintptr_t>(Slot(host,kPoolAllocTableRva,t))!=host+kPoolAllocRva || !Same(host+kPoolAllocRva,kPoolAllocBytes)) return "POOL_ALLOC_DIFFERS";
    if(At<std::uintptr_t>(Slot(host,kPoolFreeTableRva,t))!=host+kPoolFreeRva || !Same(host+kPoolFreeRva,kPoolFreeBytes)) return "POOL_FREE_DIFFERS";
    // DB_Init: "mov byte ptr [0xb69438], 1" then the pool loop; its one call site.
    const std::uintptr_t flagStore=host+kDbInitRva+0x16;
    if(At<std::uint8_t>(flagStore)!=0xc6 || At<std::uint8_t>(flagStore+1)!=0x05
       || flagStore+7+At<std::int32_t>(flagStore+2)!=host+kDbInitDoneRva || At<std::uint8_t>(flagStore+6)!=1)
        return "DB_INIT_DIFFERS";
    const std::uintptr_t call=host+kDbInitCallRva;
    if(At<std::uint8_t>(call)!=0xe8 || call+5+At<std::int32_t>(call+1)!=host+kDbInitRva) return "DB_INIT_CALL_DIFFERS";
    if(At<std::uint8_t>(host+kDbInitDoneRva)!=0 || At<std::uintptr_t>(host+kLoadedSoundPoolRva)!=0) return "POOLS_ALREADY_LINKED";
    return nullptr;
}

bool Expand() {
    status.attempted=true;
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    const wchar_t* file=std::wcsrchr(path,L'\\');file=file?file+1:path;
    if(_wcsicmp(file,L"iw4sp.exe")) return Fail("NOT_SP_HOST");
    const auto host=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(const char* refusal=Refusal(host)) return Fail(refusal);
    // The free head, then kLoadedSoundExpandedCount entries; zeroed like the
    // static pool, linked by the game's own init routine in DB_Init.
    const std::size_t bytes=8+static_cast<std::size_t>(kLoadedSoundExpandedCount)*kLoadedSoundEntryBytes;
    void* pool=VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!pool) return Fail("POOL_ALLOCATION_FAILED");
    const auto poolSlot=Slot(host,kXAssetPoolRva,kAssetTypeLoadedSound);
    const auto sizeSlot=host+kPoolSizeRva+static_cast<std::uintptr_t>(kAssetTypeLoadedSound)*4;
    DWORD oldPool=0,oldSize=0;
    if(!VirtualProtect(reinterpret_cast<void*>(poolSlot),8,PAGE_READWRITE,&oldPool)) {VirtualFree(pool,0,MEM_RELEASE);return Fail("TABLE_PROTECTION_FAILED");}
    if(!VirtualProtect(reinterpret_cast<void*>(sizeSlot),4,PAGE_READWRITE,&oldSize)) {
        DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(poolSlot),8,oldPool,&ignored);
        VirtualFree(pool,0,MEM_RELEASE);return Fail("TABLE_PROTECTION_FAILED");
    }
    const auto address=reinterpret_cast<std::uintptr_t>(pool);
    const int count=kLoadedSoundExpandedCount;
    std::memcpy(reinterpret_cast<void*>(poolSlot),&address,8);
    std::memcpy(reinterpret_cast<void*>(sizeSlot),&count,4);
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(sizeSlot),4,oldSize,&ignored);
    VirtualProtect(reinterpret_cast<void*>(poolSlot),8,oldPool,&ignored);
    status.applied=true;status.reason="OK";status.loadedSoundCapacity=count;status.pool=address;
    return true;
}

const Status& GetStatus() {return status;}

int LoadedSoundsInUse() {
    const auto host=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const std::uintptr_t pool=status.applied?status.pool:host+kLoadedSoundPoolRva;
    const int capacity=status.applied?status.loadedSoundCapacity:kLoadedSoundStockCount;
    const std::uintptr_t first=pool+8,end=first+static_cast<std::uintptr_t>(capacity)*kLoadedSoundEntryBytes;
    // Before DB_Init the head is null and the pool unlinked.
    if(!At<std::uint8_t>(host+kDbInitDoneRva)) return -1;
    int free=0;
    for(std::uintptr_t at=At<std::uintptr_t>(pool);at;at=At<std::uintptr_t>(at)) {
        // The loading thread may change the list while it is walked.
        if(at<first || at>=end || (at-first)%kLoadedSoundEntryBytes || ++free>capacity) return -1;
    }
    return capacity-free;
}

void LogStatus() {
    Log("ASSET_POOLS attempted=%d applied=%d reason=%s loaded_sound=%d stock=%d pool=%p",status.attempted,status.applied,status.reason,
        status.applied?status.loadedSoundCapacity:kLoadedSoundStockCount,kLoadedSoundStockCount,reinterpret_cast<void*>(status.pool));
}
} // namespace assetpools
} // namespace mod
