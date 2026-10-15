// Test32 diagnostic: catch the writer that damages static console command
// entries. Eight returned crashes (Test24..Test31, several maps) fault while
// walking the native command list because the name or link field of one of
// the statically registered nodes at 0x3fe908..0x3fea20 holds float-like data.
// The dumps show the damage but never the store that caused it.
//
// x64 debug registers watch four of those 8-byte fields for writes on every
// thread that exists once a level is running. A hit records the faulting
// instruction and nearby image return addresses, then execution continues
// unchanged. Nothing here alters game memory or control flow.
#include "runtime.hpp"
#include <tlhelp32.h>
#include <array>
#include <cstdio>
namespace mod {
namespace {
struct WatchHit {
    std::atomic<std::uint64_t> sequence{0};
    std::uint64_t rip=0,address=0,value=0;
    std::array<std::uint64_t,6> returns{};
    DWORD thread=0;
};
// Name fields of the nodes seen damaged, plus the 0x3fe908 link that produced
// a noncanonical next pointer. Registration happens before arming.
constexpr std::array<std::uint32_t,4> kWatchRva{0x3fe938,0x3fea00,0x3fea28,0x3fe908};
std::array<WatchHit,32> hits;
std::atomic<std::uint64_t> nextHit{0};
std::atomic<bool> armed{false},armRequested{false};
std::atomic<unsigned> armedThreads{0},skippedThreads{0};
std::uintptr_t imageBase=0,imageEnd=0;
std::uint64_t printedHits=0;

LONG CALLBACK WatchHandler(PEXCEPTION_POINTERS info) {
    if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP)return EXCEPTION_CONTINUE_SEARCH;
    auto* context=info->ContextRecord;
    const auto status=context->Dr6&0xf;
    if(!status || !armed.load(std::memory_order_acquire))return EXCEPTION_CONTINUE_SEARCH;
    unsigned slot=0;while(slot<4 && !(status&(1u<<slot)))++slot;
    const auto address=imageBase+kWatchRva[slot<4?slot:0];
    const auto n=nextHit.fetch_add(1,std::memory_order_acq_rel)+1;
    auto& hit=hits[(n-1)%hits.size()];
    hit.rip=context->Rip;hit.address=address;hit.thread=GetCurrentThreadId();
    std::memcpy(&hit.value,reinterpret_cast<const void*>(address),sizeof(hit.value));
    unsigned found=0;
    const auto* stack=reinterpret_cast<const std::uint64_t*>(context->Rsp);
    for(unsigned i=0;i<64 && found<hit.returns.size();++i) {
        const auto value=stack[i];
        if(value>=imageBase && value<imageEnd)hit.returns[found++]=value;
    }
    for(;found<hit.returns.size();++found)hit.returns[found]=0;
    hit.sequence.store(n,std::memory_order_release);
    context->Dr6=0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI ArmThreads(void*) {
    const auto self=GetCurrentThreadId();const auto process=GetCurrentProcessId();
    DWORD64 control=0;
    for(unsigned i=0;i<kWatchRva.size();++i)control|=(1ull<<(i*2))|(0x9ull<<(16+i*4)); // local enable, write, 8 bytes
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry)) {
        if(entry.th32OwnerProcessID!=process || entry.th32ThreadID==self)continue;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
        if(!thread){skippedThreads.fetch_add(1);continue;}
        if(SuspendThread(thread)!=static_cast<DWORD>(-1)) {
            CONTEXT context{};context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
            // Never take over debug registers something else already uses.
            if(GetThreadContext(thread,&context) && !(context.Dr7&0xff)) {
                context.Dr0=imageBase+kWatchRva[0];context.Dr1=imageBase+kWatchRva[1];
                context.Dr2=imageBase+kWatchRva[2];context.Dr3=imageBase+kWatchRva[3];
                context.Dr6=0;context.Dr7=control;
                if(SetThreadContext(thread,&context))armedThreads.fetch_add(1);else skippedThreads.fetch_add(1);
            } else skippedThreads.fetch_add(1);
            ResumeThread(thread);
        } else skippedThreads.fetch_add(1);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return 0;
}
}

void ArmStaticCommandWatch() {
    if(armRequested.exchange(true))return;
    imageBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase+dos->e_lfanew);
    imageEnd=imageBase+nt->OptionalHeader.SizeOfImage;
    if(!AddVectoredExceptionHandler(1,WatchHandler)){Log("AI_STATIC_WATCH armed=0 reason=HANDLER");return;}
    armed.store(true,std::memory_order_release);
    HANDLE thread=CreateThread(nullptr,0,ArmThreads,nullptr,0,nullptr);
    if(!thread){Log("AI_STATIC_WATCH armed=0 reason=THREAD");return;}
    WaitForSingleObject(thread,2000);CloseHandle(thread);
    Log("AI_STATIC_WATCH armed=1 threads=%u skipped=%u fields=0x3fe938,0x3fea00,0x3fea28,0x3fe908",armedThreads.load(),skippedThreads.load());
}

void LogStaticWatchHits() {
    const auto last=nextHit.load(std::memory_order_acquire);
    for(auto n=last>hits.size()&&printedHits<last-hits.size()?last-hits.size()+1:printedHits+1;n<=last;++n) {
        const auto& hit=hits[(n-1)%hits.size()];
        if(hit.sequence.load(std::memory_order_acquire)!=n)continue;
        const auto rva=[](std::uint64_t v){return v?v-imageBase:0;};
        Log("AI_STATIC_WATCH_HIT sequence=%llu thread=%lu field_rva=0x%llx value=0x%016llx rip_rva=0x%llx returns=0x%llx,0x%llx,0x%llx,0x%llx,0x%llx,0x%llx",
            static_cast<unsigned long long>(n),hit.thread,static_cast<unsigned long long>(hit.address-imageBase),
            static_cast<unsigned long long>(hit.value),static_cast<unsigned long long>(rva(hit.rip)),
            static_cast<unsigned long long>(rva(hit.returns[0])),static_cast<unsigned long long>(rva(hit.returns[1])),
            static_cast<unsigned long long>(rva(hit.returns[2])),static_cast<unsigned long long>(rva(hit.returns[3])),
            static_cast<unsigned long long>(rva(hit.returns[4])),static_cast<unsigned long long>(rva(hit.returns[5])));
    }
    printedHits=last;
}

int FormatStaticWatchHits(char* out,int bytes) {
    // Crash-report form: async-signal tolerant, no allocation or locks.
    int used=0;const auto last=nextHit.load(std::memory_order_acquire);
    for(auto n=last>hits.size()?last-hits.size()+1:1;n<=last && used<bytes-1;++n) {
        const auto& hit=hits[(n-1)%hits.size()];
        if(hit.sequence.load(std::memory_order_acquire)!=n)continue;
        const int size=std::snprintf(out+used,bytes-used,"static_watch_hit=%llu field_rva=0x%llx value=0x%016llx rip_rva=0x%llx return0_rva=0x%llx\r\n",
            static_cast<unsigned long long>(n),static_cast<unsigned long long>(hit.address-imageBase),static_cast<unsigned long long>(hit.value),
            static_cast<unsigned long long>(hit.rip-imageBase),static_cast<unsigned long long>(hit.returns[0]?hit.returns[0]-imageBase:0));
        if(size<=0)break;
        used+=size<bytes-used?size:bytes-used-1;
    }
    return used;
}
} // namespace mod
