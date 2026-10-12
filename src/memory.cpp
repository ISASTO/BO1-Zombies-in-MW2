#include "runtime.hpp"
#include "checked_memory.hpp"
#include <vector>
namespace mod {
namespace {
struct Span {std::uintptr_t begin,end;};
std::vector<Span> hostSpans;
std::atomic<bool> spansReady{false};
thread_local MemoryReadCounts readCounts;
}
bool InitializeHostReads(std::uintptr_t base,std::size_t size) {
    // Called once, after the exact executable hash matches. MEM_IMAGE pages
    // belong to the main EXE and remain mapped for the process lifetime. Heap,
    // string-pool and asset pointers retain ReadProcessMemory's failure checks.
    if(spansReady.load(std::memory_order_acquire))return true;
    if(!ContainsBytes(base,size,base,size))return false;
    const auto end=base+size;
    std::vector<Span> spans;
    for(auto cursor=base;cursor<end;) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof(info)))return false;
        const auto start=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(info.RegionSize>std::numeric_limits<std::uintptr_t>::max()-start)return false;
        const auto next=std::min(end,start+info.RegionSize);
        if(next<=cursor)return false;
        const bool readable=info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY);
        if(info.Type==MEM_IMAGE && info.AllocationBase==reinterpret_cast<void*>(base) && info.State==MEM_COMMIT && readable && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))spans.push_back({cursor,next});
        cursor=next;
    }
    hostSpans=std::move(spans);spansReady.store(true,std::memory_order_release);
    Log("MEMORY_READ image_spans=%u mode=VALIDATED_EXE_COPY heap_mode=CHECKED_KERNEL",static_cast<unsigned>(hostSpans.size()));return true;
}
std::size_t HostReadableBytes(std::uintptr_t address) {
    if(!spansReady.load(std::memory_order_acquire))return 0;
    const auto it=std::upper_bound(hostSpans.begin(),hostSpans.end(),address,[](auto at,const auto& span){return at<span.begin;});
    if(it==hostSpans.begin())return 0;
    const auto& span=*(it-1);
    return address<span.end?span.end-address:0;
}
bool CheckedReadMemory(std::uintptr_t address,void* output,std::size_t size) {
    if(!address || !output || !size)return false;
    if(size<=HostReadableBytes(address)) {++readCounts.image;std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}
    ++readCounts.kernel;SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&copied) && copied==size;
}
MemoryReadCounts GetMemoryReadCounts() {return readCounts;}
} // namespace mod
