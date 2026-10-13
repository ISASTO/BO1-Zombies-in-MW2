// Execute the real actor-team iterator with guarded pool bounds and both
// pointer-sign cases. No engine/script callbacks are substituted on this path.
#include "expanded_actor_layout.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;constexpr std::size_t kMapped=0x6000000,kPool=0x5000000,kSentient=0x5500000;
void Fault(int,siginfo_t* info,void* context) {
    auto* u=static_cast<ucontext_t*>(context);char b[240];
    const auto ip=static_cast<std::uintptr_t>(u->uc_mcontext.gregs[REG_RIP]);
    const auto actor=static_cast<unsigned long long>(u->uc_mcontext.gregs[REG_R8]);
    const auto n=std::snprintf(b,sizeof(b),"ITERATOR_OVERRUN rva=0x%llx actor_index=%llu fault=%p\n",static_cast<unsigned long long>(ip-reinterpret_cast<std::uintptr_t>(image)),actor,info->si_addr);
    if(n>0){const auto written=write(2,b,std::min(n,239));(void)written;}_exit(71);
}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>void Put(void* base,std::size_t at,T value){std::memcpy(static_cast<char*>(base)+at,&value,sizeof(value));}
}
int main(int argc,char**argv) {
    if(argc!=3)return 2;const rlimit noCore{0,0};setrlimit(RLIMIT_CORE,&noCore);
    try {
        const bool negative=std::strcmp(argv[2],"negative")==0;
        std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
        Check(f.is_open() && bytes.size()==0x5800000,"Invalid iterator fixture");
        // Exact placement is intentional: the bad Test22 EAX comparison has
        // different failures when the pointer's low DWORD is positive/negative.
        const std::uintptr_t desired=negative?0x600080000000ull:0x600010000000ull;
        image=static_cast<std::uint8_t*>(mmap(reinterpret_cast<void*>(desired),kMapped,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
        Check(image!=MAP_FAILED,"Cannot reserve fixed non-overwriting fixture address");
        struct Release {~Release(){if(image!=MAP_FAILED)munmap(image,kMapped);}} release;
        std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
        struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
        auto* pool=image+kPool;auto* sentient=image+kSentient;
        Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(pool));Put(sentient,8,int(2));
        std::memset(pool,0,kActors*kStride);
        Check(mprotect(pool+kActors*kStride,0x5000,PROT_NONE)==0,"Pool guard setup failed");
        using Next=void*(__attribute__((ms_abi))*)(void*,unsigned);
        const auto next=reinterpret_cast<Next>(image+0x1286a0);
        auto* before=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(pool)-kStride);
        unsigned cases=0;
        Check(next(before,0x1e)==nullptr,"Empty pool must stop at actor 256");++cases;
        for(unsigned n=0;n<kActors;++n) {
            auto* actor=pool+n*kStride;
            if(n)pool[(n-1)*kStride+0x14ff]=0;
            actor[0x14ff]=1;Put(actor,8,reinterpret_cast<std::uintptr_t>(sentient));
            Check(next(before,1u<<2)==actor,"Sparse team iterator lost an expanded actor");++cases;
            Check(next(actor,1u<<2)==nullptr,"Iterator must stop after the last matching actor");++cases;
            Check(next(before,1u<<1)==nullptr,"No matching team must stop within the pool");++cases;
        }
        // A dense pool exercises each successive actor, its current-actor skip,
        // and the final slot-255 exhaustion without touching the guard page.
        for(unsigned n=0;n<kActors;++n){auto* actor=pool+n*kStride;actor[0x14ff]=1;Put(actor,8,reinterpret_cast<std::uintptr_t>(sentient));}
        void* current=before;
        for(unsigned n=0;n<kActors;++n){current=next(current,1u<<2);Check(current==pool+n*kStride,"Dense team iterator lost order");++cases;}
        Check(next(current,1u<<2)==nullptr,"Dense iterator crossed slot 255");++cases;
        std::cout<<"{\"guarded_iterator_cases\":"<<cases<<",\"actor_capacity\":256,\"original_native_function_rva\":\"0x1286a0\",\"pool_guard\":true,\"negative_pointer_low_dword\":"<<(negative?"true":"false")<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
