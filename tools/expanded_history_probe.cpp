// Execute native history writing, generation validation, interpolation, shot
// rewind and restoration. The live pool is 256; history/scratch have 32 slots.
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <array>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdio>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";unsigned relinks=0;
constexpr std::size_t Pool=0x5000000,Entities=0xd9e070,History=0x19b07b0,Counter=0x19b48b0,Clock=0xd96ec8;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* context){auto* u=static_cast<ucontext_t*>(context);char b[220];int n=std::snprintf(b,sizeof(b),"HISTORY_FAULT phase=%s rva=0x%llx actor=%llu fault=%p\n",phase,
    static_cast<unsigned long long>(u->uc_mcontext.gregs[REG_RIP]-reinterpret_cast<std::uintptr_t>(image)),
    static_cast<unsigned long long>(u->uc_mcontext.gregs[REG_RSI]),info->si_addr);
    if(n>0){auto ignored=write(2,b,std::min(n,219));(void)ignored;}_exit(71);}
void Detour(std::size_t at,std::uintptr_t target){auto j=mod::AbsoluteJump(target);std::memcpy(image+at,j.data(),j.size());}
void Jump(std::size_t at,std::size_t target){image[at]=0xe9;Put(image,at+1,std::int32_t(target-at-5));}
void ABI Relink(void* entity){const auto at=static_cast<std::uint8_t*>(entity)-image;Check(at>=Entities && (at-Entities)%0x2a0==0,"Invalid relink entity");++relinks;}
void ABI OldShotValidation(void* record,void** origins,void** angles,std::uint8_t* flags){*origins=record;*angles=static_cast<std::uint8_t*>(record)+0x180;std::memset(flags,1,32);}
using Empty=void(ABI*)();using Validate=void(ABI*)(void*,void**,void**,std::uint8_t*);
using Sample=bool(ABI*)(int,void*,void*,std::uint8_t*);using Begin=void(ABI*)(int,void*);using Restore=void(ABI*)(void*);
std::uint8_t* Entity(unsigned n){return image+Entities+(n+2)*0x2a0;}
void Pose(unsigned n,float delta){auto* e=Entity(n);for(unsigned v=0;v<6;++v){const auto bits=std::uint32_t(0x3f800001);float first;std::memcpy(&first,&bits,4);Put(e,0xe8+v*4,v?float(n*10+v)+delta:first+delta);}}
void Populate(bool all){std::memset(image+Pool,0,kActors*kStride);for(unsigned n=0;n<256;++n){auto* a=image+Pool+n*kStride;auto* e=Entity(n);std::memset(e,0,0x2a0);Put(e,0x78,int(n+7));Pose(n,0.f);a[0x14ff]=(all || n<32)?1:0;Put(a,0,reinterpret_cast<std::uintptr_t>(e));}}
std::array<std::array<float,6>,256> Poses(){std::array<std::array<float,6>,256> out{};for(unsigned n=0;n<256;++n)std::memcpy(out[n].data(),Entity(n)+0xe8,24);return out;}
void HistoryFrames(){std::memset(image+History,0,20*0x340);Put(image,Counter,int(0));for(unsigned n=0;n<20;++n)reinterpret_cast<Empty>(image+0x232d80)();Put(image,Clock,int(1000));}
void* GuardedScratch(void*& allocation){allocation=mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);Check(allocation!=MAP_FAILED,"scratch mmap failed");Check(mprotect(static_cast<char*>(allocation)+4096,4096,PROT_NONE)==0,"scratch guard failed");auto* p=static_cast<char*>(allocation)+4096-0x320;std::memset(p,0xa5,0x320);return p;}
void InlineRestore(void* scratch){
    // Entry shim into the first weapon's real restore loop. Only its following
    // full-weapon epilogue jumps back to the shim in this disposable fixture.
    constexpr std::size_t at=0x57e0000;std::vector<std::uint8_t> b{0x55,0x53,0x56,0x57,0x48,0x83,0xec,0x28,0x48,0x8d,0x69,0x60,0x31,0xff,0xe9};
    const auto rel=std::int32_t(0x1a15ee - (at+b.size()+4));const auto* r=reinterpret_cast<const std::uint8_t*>(&rel);b.insert(b.end(),r,r+4);
    const auto epilogue=at+b.size();
    for(auto c:{0x48,0x83,0xc4,0x28,0x5f,0x5e,0x5b,0x5d,0xc3})b.push_back(static_cast<std::uint8_t>(c));
    std::memcpy(image+at,b.data(),b.size());Jump(0x1a166c,epilogue);reinterpret_cast<Restore>(image+at)(scratch);
}
void SecondInlineRestore(void* scratch){
    // This weapon uses RSP-relative arrays instead of an RBP-relative record.
    // Supply its real stack geometry; its own native restore code runs intact.
    constexpr std::size_t at=0x57e0100;
    std::vector<std::uint8_t> b{0x55,0x53,0x56,0x57,0x48,0x81,0xec,0x98,0x06,0,0,
        0x48,0x89,0xce,0x48,0x8d,0x7c,0x24,0x50,0xb9,0x20,0x03,0,0,0xfc,0xf3,0xa4,0x31,0xff,0x31,0xed,0xe9};
    const auto rel=std::int32_t(0x1a342a - (at+b.size()+4));const auto* r=reinterpret_cast<const std::uint8_t*>(&rel);b.insert(b.end(),r,r+4);
    const auto epilogue=at+b.size();
    for(auto c:{0x48,0x81,0xc4,0x98,0x06,0,0,0x5f,0x5e,0x5b,0x5d,0xc3})b.push_back(static_cast<std::uint8_t>(c));
    std::memcpy(image+at,b.data(),b.size());Jump(0x1a34ac,epilogue);reinterpret_cast<Restore>(image+at)(scratch);
}
}
int main(int argc,char**argv){if(argc!=3)return 2;const rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
    std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Bad private fixture");
    image=static_cast<std::uint8_t*>(mmap(nullptr,0x6000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED,"image mmap failed");std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
    struct sigaction a{};a.sa_sigaction=Fault;a.sa_flags=SA_SIGINFO;sigemptyset(&a.sa_mask);sigaction(SIGSEGV,&a,nullptr);
    Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(image+Pool));
    Detour(0x234e60,reinterpret_cast<std::uintptr_t>(&Relink));
    const auto fill=std::vector<std::uint8_t>{0x57,0x48,0x89,0xcf,0x89,0xd0,0x4c,0x89,0xc1,0xfc,0xf3,0xaa,0x5f,0xc3};std::memcpy(image+0x2e42d0,fill.data(),fill.size());
    if(std::strcmp(argv[2],"old-writer")==0){Populate(false);Put(image,Counter,int(0));auto* pool=image+Pool;Put(pool,0x1560,std::uint64_t(0));pool[0x2a5f]=1;phase="old writer";reinterpret_cast<Empty>(image+0x232d80)();Check(false,"Old history unexpectedly survived");}
    if(std::strcmp(argv[2],"old-shoot")==0){Populate(false);for(unsigned n=32;n<256;++n)Put(image+Pool,n*kStride,std::uint64_t(0));Put(image,Clock,int(1000));Put(image,Counter,int(20));for(unsigned h=0;h<20;++h){auto* record=image+History+h*0x340;for(unsigned n=0;n<32;++n)std::memcpy(record+n*12,Entity(n)+0xe8,12);}Detour(0x1a2eb0,reinterpret_cast<std::uintptr_t>(&OldShotValidation));std::array<char,0x1000> scratch{};phase="old shot rewind";reinterpret_cast<Begin>(image+0x1a1760)(800,scratch.data());Check(false,"Old shot unexpectedly survived");}
    Populate(true);
    // Legitimate appended perception bytes must not be interpreted as old
    // actors' active flags/entity pointers. Poison all seven unrolled offsets.
    for(unsigned n=1;n<8;++n){image[Pool+n*0x1560+0x14ff]=1;Put(image+Pool,n*0x1560,std::uint64_t(0));}
    phase="native frame history";HistoryFrames();unsigned historyCases=0;
    for(unsigned h=0;h<20;++h){auto* frame=image+History+h*0x340;for(unsigned n=0;n<32;++n){Check(frame[0x320+n]==1,"History active flag wrong");Check(frame[0x300+n]==n+7,"History generation wrong");Check(!std::memcmp(frame+n*12,Entity(n)+0xe8,12) && !std::memcmp(frame+0x180+n*12,Entity(n)+0xf4,12),"History pose came from wrong actor");++historyCases;}}
    auto validate=reinterpret_cast<Validate>(image+0x1a2eb0);unsigned validationCases=0;
    phase="native history validation";for(unsigned changed=0;changed<32;++changed){
        image[Pool+changed*kStride+0x14ff]=0;std::array<std::uint8_t,34> flags{};flags[32]=0xa5;flags[33]=0x5a;void* origins=nullptr;void* angles=nullptr;
        validate(image+History,&origins,&angles,flags.data());Check(origins==image+History && angles==image+History+0x180,"History pointers changed");
        for(unsigned n=0;n<32;++n){Check(flags[n]==(n!=changed),"Validation missed inactive actor");++validationCases;}Check(flags[32]==0xa5 && flags[33]==0x5a,"Validation crossed flags");
        image[Pool+changed*kStride+0x14ff]=1;Put(Entity(changed),0x78,int(changed+8));validate(image+History,&origins,&angles,flags.data());for(unsigned n=0;n<32;++n){Check(flags[n]==(n!=changed),"Validation missed recycled entity");++validationCases;}Put(Entity(changed),0x78,int(changed+7));
    }
    phase="native temporal interpolation";
    for(unsigned h=17;h<=18;++h)for(unsigned n=0;n<32;++n)for(unsigned v=0;v<6;++v){const auto offset=(v<3?0:0x180)+n*12+(v%3)*4;Put(image+History+h*0x340,offset,Get<float>(Entity(n),0xe8+v*4)+float((h-16)*100));}
    std::array<float,96> origins{},angles{};std::array<std::uint8_t,32> valid{};
    Check(reinterpret_cast<Sample>(image+0x1a2aa0)(875,origins.data(),angles.data(),valid.data()),"Native historical sample failed");
    for(unsigned n=0;n<32;++n){Check(valid[n]==1,"Interpolated actor missing");for(unsigned v=0;v<6;++v){const auto actual=(v<3?origins:angles)[n*3+v%3];Check(actual==Get<float>(Entity(n),0xe8+v*4)+150.f,"Native temporal interpolation changed");}}
    Pose(0,10.f);Put(image,Counter,int(20));reinterpret_cast<Empty>(image+0x232d80)();Check(Get<int>(image,Counter)==21 && !std::memcmp(image+History,Entity(0)+0xe8,12),"History ring wrap failed");Pose(0,0.f);
    unsigned shotCases=0;auto begin=reinterpret_cast<Begin>(image+0x1a1760);auto restore=reinterpret_cast<Restore>(image+0x1a16b0);
    for(unsigned sparse=0;sparse<2;++sparse){
        phase="guarded native shot rewind and restore";for(unsigned n=0;n<32;++n)image[Pool+n*kStride+0x14ff]=(!sparse || n%3==0)?1:0;HistoryFrames();const auto historical=Poses();for(unsigned n=0;n<256;++n)Pose(n,1000.f);const auto current=Poses();
        void* allocation=nullptr;auto* scratch=static_cast<std::uint8_t*>(GuardedScratch(allocation));relinks=0;begin(800,scratch);const auto rewound=Poses();unsigned active=0;
        for(unsigned n=0;n<256;++n){const bool moved=n<32 && (!sparse || n%3==0);if(moved)++active;Check(rewound[n]==(moved?historical[n]:current[n]),"Shot rewound wrong actor or high current pose");++shotCases;}
        Check(relinks==active,"Rewind relink count wrong");restore(scratch);Check(Poses()==current && relinks==active*2,"Restore lost live pose");for(unsigned n=0;n<32;++n)Check(!scratch[0x300+n],"Restore retained active flag");
        // The two inline weapon restore loops must share the same 32-slot bound.
        Check(Get<int>(image,0x1a1666)==32*int(kStride) && Get<int>(image,0x1a34a3)==32*int(kStride),"Weapon restore byte spans mismatch scratch");
        begin(800,scratch);InlineRestore(scratch);Check(Poses()==current,"Inline weapon restore corrupted pose");++shotCases;
        begin(800,scratch);SecondInlineRestore(scratch);Check(Poses()==current,"Second inline weapon restore corrupted pose");++shotCases;
        munmap(allocation,8192);
    }
    std::cout<<"{\"history_pose_cases\":"<<historyCases<<",\"generation_validation_cases\":"<<validationCases<<",\"temporal_interpolation_actor_cases\":32,\"history_ring_wrap\":true,\"shot_pose_cases\":"<<shotCases<<",\"live_actor_capacity\":256,\"native_history_slots\":32,\"guarded_800_byte_shot_scratch\":true,\"high_actor_poses_preserved\":true,\"native_interpolation_and_restore\":true,\"native_weapon_restore_loops_executed\":2,\"booted_game_tested\":false}\n";
    munmap(image,0x6000000);
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 1;}}
