// Isolated Linux/x86-64 MS-ABI probe of three SHA256-verified native routines.
// Input is a PRIVATE mapped-image fixture made by run_capacity_probe.py.
// This does not load MW2, call imports, or write the original executable.
#include <sys/mman.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t kSize=0x5000000,kPool=0x4a00000,kSentients=0x4c00000,kLimit=0x49fe000,kRelay=0x49f0000;
constexpr std::size_t kStride=0x1560,kCapacity=256,kActorPointer=0xd96ea0,kSentientPointer=0xd96e98,kActorCount=0xd96ee0,kLimitPointer=0xd9c0e0;
void Check(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
template<class T> void Put(std::uint8_t* base,std::size_t at,T value) {std::memcpy(base+at,&value,sizeof(value));}
template<class T> T Get(const std::uint8_t* base,std::size_t at) {T out{};std::memcpy(&out,base+at,sizeof(out));return out;}
void Jump(std::uint8_t* base,std::size_t at,std::size_t target) {
    base[at]=0xe9;Put(base,at+1,static_cast<std::int32_t>(target-at-5));
}
void ExpandBound(std::uint8_t* image,std::size_t at,std::size_t relay) {
    // Each verified window is CMP r64, imm8; JL/JGE rel8. Rebuild it intact with
    // an imm32 compare and a rel32 condition, preserving registers/flags.
    Check(image[at+1]==0x83 && image[at+2]==0xf8 && image[at+3]==32 && (image[at+4]==0x7c || image[at+4]==0x7d),"Unexpected native bound window");
    const auto condition=static_cast<std::uint8_t>(image[at+4]+0x10);
    const auto loop=static_cast<std::size_t>(static_cast<std::intptr_t>(at+6)+static_cast<std::int8_t>(image[at+5]));
    image[relay]=image[at];image[relay+1]=0x81;image[relay+2]=0xf8;
    Put(image,relay+3,std::uint32_t(kCapacity));
    image[relay+7]=0x0f;image[relay+8]=condition;
    Put(image,relay+9,static_cast<std::int32_t>(loop-relay-13));
    Jump(image,relay+13,at+6);Jump(image,at,relay);image[at+5]=0x90;
}
using Allocate=void*(__attribute__((ms_abi))*)();
using Iterate=void(__attribute__((ms_abi))*)();
using ClearPerception=void(__attribute__((ms_abi))*)(void*,void*);
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0xf50140 && bytes.size()<kRelay,"Invalid mapped-image fixture");
        auto* image=static_cast<std::uint8_t*>(mmap(nullptr,kSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        struct Release {void* address;~Release(){munmap(address,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        Put(image,kActorPointer,reinterpret_cast<std::uintptr_t>(image+kPool));
        Put(image,kSentientPointer,reinterpret_cast<std::uintptr_t>(image+kSentients));
        Put(image,kLimitPointer,reinterpret_cast<std::uintptr_t>(image+kLimit));
        Put(image,kLimit+0x10,int(kCapacity));Put(image,kActorCount,0);
        // Native memset has Windows imports elsewhere. Substitute only that
        // dependency with an MS-ABI REP STOSB stub; native defaults run intact.
        const std::uint8_t fill[]={0x57,0x48,0x89,0xcf,0x89,0xd0,0x4c,0x89,0xc1,0xfc,0xf3,0xaa,0x5f,0xc3};
        std::memcpy(image+0x2e42d0,fill,sizeof(fill));
        ExpandBound(image,0x12398a,kRelay);ExpandBound(image,0x123bfc,kRelay+0x40);
        ExpandBound(image,0x123c77,kRelay+0x80);ExpandBound(image,0x123cb7,kRelay+0xc0);
        std::memset(image+kPool-16,0xa5,16);std::memset(image+kPool+kStride*kCapacity,0xa5,16);
        const auto allocate=reinterpret_cast<Allocate>(image+0x123950);
        for(std::size_t n=0;n<kCapacity;++n) {
            auto* actor=static_cast<std::uint8_t*>(allocate());
            Check(actor==image+kPool+n*kStride,"Native allocator returned wrong expanded slot");
            Check(actor[0x14ff]==1,"Native allocator did not set active flag");
            Put(actor,8,reinterpret_cast<std::uintptr_t>(image+kSentients+n*0x88));
            Put(image,kSentients+n*0x88+8,int(n==255?2:1));
        }
        Check(Get<int>(image,kActorCount)==256 && allocate()==nullptr,"Native allocator did not stop exactly at 256");
        for(unsigned n=0;n<16;++n)Check(image[kPool-16+n]==0xa5 && image[kPool+kStride*kCapacity+n]==0xa5,"Expanded actor pool boundary overwritten");
        // Instrument the iterator's predicate, avoiding all native callbacks.
        // Return false after recording each visited actor; only native bound,
        // stride, active-flag and index-division code executes in the loop.
        constexpr auto visits=kLimit+0x100,last=kLimit+0x108,predicate=kRelay+0x200;
        image[predicate]=0x48;image[predicate+1]=0xb8;
        Put(image,predicate+2,reinterpret_cast<std::uintptr_t>(image+visits));
        image[predicate+10]=0xff;image[predicate+11]=0x00;
        image[predicate+12]=0x48;image[predicate+13]=0xb8;
        Put(image,predicate+14,reinterpret_cast<std::uintptr_t>(image+last));
        const std::uint8_t record[]={0x48,0x89,0x08,0x31,0xc0,0xc3};
        std::memcpy(image+predicate+22,record,sizeof(record));
        Check(image[0x123c3a]==0xe8,"Unexpected native iteration predicate call");
        image[0x123c3a]=0xe8;Put(image,0x123c3b,static_cast<std::int32_t>(predicate-0x123c3f));
        const auto iterate=reinterpret_cast<Iterate>(image+0x123bc0);iterate();
        Check(Get<unsigned>(image,visits)==256 && Get<std::uintptr_t>(image,last)==reinterpret_cast<std::uintptr_t>(image+kPool+255*kStride+0xfa4),"Native iterator did not visit all 256 slots");
        // Stop ClearPerception at its own complete epilogue, before callbacks.
        // The unchanged native index math and all three 16-byte writes execute.
        Check(image[0x12483a]==0xe8,"Unexpected perception callback window");
        Jump(image,0x12483a,0x1248b5);
        const auto clear=reinterpret_cast<ClearPerception>(image+0x1247e0);
        std::memset(image+kPool+0xca8,0xa5,48);
        clear(image+kPool,image+kSentients+49*0x88);
        for(unsigned n=0;n<48;++n)Check(image[kPool+0xca8+n]==0xa5,"Native slot 49 unexpectedly crossed perception boundary");
        clear(image+kPool,image+kSentients+50*0x88);
        for(unsigned n=0;n<48;++n)Check(image[kPool+0xca8+n]==0,"Extra sentient did not reproduce expected actor-tail corruption");
        std::cout<<"{\n  \"probe\": \"isolated exact-native allocator/iterator/perception\",\n"
            "  \"native_allocator_slots_verified\": 256,\n  \"native_257th_allocation_refused\": true,\n"
            "  \"pool_boundary_guards_intact\": true,\n  \"native_iterator_slots_visited\": 256,\n  \"native_iterator_last_slot\": 255,\n"
            "  \"native_sentient_49_tail_guard_intact\": true,\n  \"native_sentient_50_actor_tail_corruption_bytes\": 48,\n"
            "  \"runtime_capacity_expansion_enabled\": false,\n"
            "  \"scope\": \"Three routines in a private Linux MS-ABI harness; MW2/Windows gameplay, animation, snapshots and save/reload are not executed.\"\n}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
