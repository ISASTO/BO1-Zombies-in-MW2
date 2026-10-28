// Test44: run the retail x64 DB_Init (0xeff10) on a private mapped image
// after the loaded_sound pool was enlarged the way src/asset_pools.cpp does it
// (DB_XAssetPool[13] -> a zeroed 8 + 4096 x 0x40 byte pool, g_poolSize[13] ->
// 4096). DB_Init links every pool with its own init routine; only the
// database thread start (0x203bb0) is replaced.
//
// Then the loaded_sound free list must hold exactly 4096 entries in order,
// the retail allocator (0xeee20) must hand out 4096 distinct entries inside
// the new pool and then report exhaustion, and the retail free routine
// (0xee6c0) must take them all back. The stock pools of the other types are
// checked to be linked as before (head = first entry).
// (Free-list pools only; single-asset types use their own allocators.)
#include "script_call.hpp"
#include "zombie_bindings.hpp"
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mod;
namespace {
constexpr std::uintptr_t kBase=0x140000000;
constexpr std::size_t kSize=0x5000000;
constexpr std::uintptr_t kSpawnDatabaseThread=0x203bb0;
std::uint8_t* image=nullptr;
void Check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
template<class T> T At(std::uintptr_t rva) {T v{};std::memcpy(&v,image+rva,sizeof(v));return v;}
template<class T> T Mem(std::uintptr_t at) {T v{};std::memcpy(&v,reinterpret_cast<const void*>(at),sizeof(v));return v;}
void Jump(std::uintptr_t rva,const void* target) {
    auto* p=image+rva;p[0]=0x48;p[1]=0xb8;std::uint64_t t=reinterpret_cast<std::uint64_t>(target);std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;
}
bool MOD_NATIVE_ABI SpawnDatabaseThread(void*) {return true;}
}

int main(int argc,char** argv) {
    if(argc<2) return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4071280 && bytes.size()<kSize,"Invalid private image");
        image=static_cast<std::uint8_t*>(mmap(reinterpret_cast<void*>(kBase),kSize,PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
        Check(image==reinterpret_cast<std::uint8_t*>(kBase),"Preferred image base unavailable");
        std::memcpy(image,bytes.data(),bytes.size());
        constexpr int t=kAssetTypeLoadedSound;
        // The checks src/asset_pools.cpp makes before writing anything.
        Check(!std::strcmp(reinterpret_cast<const char*>(At<std::uintptr_t>(kAssetNamesRva+t*8)),"loaded_sound"),"asset name");
        Check(At<int>(kPoolSizeRva+t*4)==kLoadedSoundStockCount,"stock pool size");
        Check(At<std::uintptr_t>(kXAssetPoolRva+t*8)==kBase+kLoadedSoundPoolRva,"stock pool pointer");
        Check(At<std::uintptr_t>(kPoolInitTableRva+t*8)==kBase+kLoadedSoundPoolInitRva,"pool init entry");
        Check(!std::memcmp(image+kLoadedSoundPoolInitRva,kLoadedSoundPoolInitBytes.data(),kLoadedSoundPoolInitBytes.size()),"pool init bytes");
        Check(At<std::uintptr_t>(kPoolAllocTableRva+t*8)==kBase+kPoolAllocRva && !std::memcmp(image+kPoolAllocRva,kPoolAllocBytes.data(),kPoolAllocBytes.size()),"pool alloc");
        Check(At<std::uintptr_t>(kPoolFreeTableRva+t*8)==kBase+kPoolFreeRva && !std::memcmp(image+kPoolFreeRva,kPoolFreeBytes.data(),kPoolFreeBytes.size()),"pool free");
        Check(At<std::uint8_t>(kDbInitDoneRva)==0 && At<std::uintptr_t>(kLoadedSoundPoolRva)==0,"pools unlinked before DB_Init");
        // The other pools as the image declares them, to compare after DB_Init.
        std::vector<std::pair<int,std::uintptr_t>> stock;
        // (free-list pools: the generic allocator; single-asset types have their own.)
        for(int type=0;type<43;++type) if(type!=t && At<std::uintptr_t>(kXAssetPoolRva+type*8) && At<int>(kPoolSizeRva+type*4)>0
                                          && At<std::uintptr_t>(kPoolAllocTableRva+type*8)==kBase+kPoolAllocRva)
            stock.push_back({type,At<std::uintptr_t>(kXAssetPoolRva+type*8)});
        // The expansion.
        const std::size_t poolBytes=8+static_cast<std::size_t>(kLoadedSoundExpandedCount)*kLoadedSoundEntryBytes;
        auto* pool=static_cast<std::uint8_t*>(mmap(nullptr,poolBytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(pool!=MAP_FAILED,"pool allocation");
        const auto address=reinterpret_cast<std::uintptr_t>(pool);
        std::memcpy(image+kXAssetPoolRva+t*8,&address,8);
        const int count=kLoadedSoundExpandedCount;std::memcpy(image+kPoolSizeRva+t*4,&count,4);
        // DB_Init, unmodified except for the database thread.
        Jump(kSpawnDatabaseThread,reinterpret_cast<void*>(&SpawnDatabaseThread));
        reinterpret_cast<void(MOD_NATIVE_ABI*)()>(kBase+kDbInitRva)();
        Check(At<std::uint8_t>(kDbInitDoneRva)==1,"DB_Init flag");
        // Free list: entries in order, stride 0x40, null-terminated.
        const std::uintptr_t first=address+8,end=first+static_cast<std::uintptr_t>(count)*kLoadedSoundEntryBytes;
        int linked=0;
        for(std::uintptr_t at=Mem<std::uintptr_t>(address);at;at=Mem<std::uintptr_t>(at)) {
            Check(at==first+static_cast<std::uintptr_t>(linked)*kLoadedSoundEntryBytes,"free list order");
            Check(++linked<=count,"free list too long");
        }
        Check(linked==count,"free list length "+std::to_string(linked));
        Check(Mem<std::uintptr_t>(address+8+static_cast<std::uintptr_t>(count-1)*kLoadedSoundEntryBytes)==0,"free list end");
        // Retail allocator and free routine on the enlarged pool.
        using AllocFn=void*(MOD_NATIVE_ABI*)(void*);using FreeFn=void(MOD_NATIVE_ABI*)(void*,void*);
        const auto alloc=reinterpret_cast<AllocFn>(At<std::uintptr_t>(kPoolAllocTableRva+t*8));
        const auto release=reinterpret_cast<FreeFn>(At<std::uintptr_t>(kPoolFreeTableRva+t*8));
        std::set<std::uintptr_t> handed;
        for(int n=0;n<count;++n) {
            const auto p=reinterpret_cast<std::uintptr_t>(alloc(pool));
            Check(p>=first && p<end && (p-first)%kLoadedSoundEntryBytes==0 && handed.insert(p).second,"allocation "+std::to_string(n));
        }
        Check(alloc(pool)==nullptr,"exhaustion after 4096");
        for(const auto p:handed) release(pool,reinterpret_cast<void*>(p));
        int back=0;for(std::uintptr_t at=Mem<std::uintptr_t>(address);at && back<=count;at=Mem<std::uintptr_t>(at)) ++back;
        Check(back==count,"all entries freed");
        // Every other pool linked in place: head = its first entry.
        for(const auto& [type,p]:stock) Check(Mem<std::uintptr_t>(p)==p+8,"stock pool "+std::to_string(type)+" not linked in place");
        // Asset entry pool (0xa8e858 head) untouched by the change.
        Check(At<std::uintptr_t>(0xa8e858)==kBase+0xa8e878,"asset entry pool");
        std::printf("{\"loaded_sound\":{\"stock\":%d,\"expanded\":%d,\"linked\":%d,\"allocated\":%zu,\"exhausted_after\":%d,\"freed\":%d},"
                    "\"other_pools_linked\":%zu}\n",kLoadedSoundStockCount,count,linked,handed.size(),count,back,stock.size());
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
