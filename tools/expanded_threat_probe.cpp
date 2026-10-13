// Execute the actual threat producer / score selection / team traversals.
// Game-world predicates and enemy commit are isolated; candidate writes and
// the caller's nonvolatile registers are checked across the 50-row boundary.
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <cmath>
#define ABI __attribute__((ms_abi))
namespace {
std::uint8_t* image=nullptr;unsigned expected=0,scored=0,captured=0;std::uintptr_t selected=0;
constexpr std::size_t Pool=0x5000000,Sentients=0x5500000,Entities=0xd9e070,Dummy=0x56f0000;
const char* phase="init";
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Detour(std::size_t at,std::uintptr_t fn){auto jump=mod::AbsoluteJump(fn);std::memcpy(image+at,jump.data(),jump.size());}
void Fault(int,siginfo_t* info,void* context){auto* u=static_cast<ucontext_t*>(context);char b[200];int n=std::snprintf(b,sizeof(b),"Fault phase=%s rva=%llx addr=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,n);(void)ignored;}_exit(71);}
int ABI Score(void*,void* sentient,float distance){++scored;Check(distance>0,"Native distance lost");return int((static_cast<char*>(sentient)-reinterpret_cast<char*>(image+Sentients))/0x88)*10;}
void ABI Commit(void*,std::uintptr_t target,int,int){selected=target;}
void ABI Secondary(void*,const void* records,int count){
    captured=count;Check(count==int(expected),"Threat candidates omitted");std::set<std::uintptr_t> seen;
    for(int n=0;n<count;++n){const auto* row=static_cast<const char*>(records)+n*24;auto entity=Get<std::uintptr_t>(row,0);
        Check(entity>=reinterpret_cast<std::uintptr_t>(image+Entities+2*0x2a0) && entity<=reinterpret_cast<std::uintptr_t>(image+Entities+(expected+1)*0x2a0),"Candidate entity pointer damaged");
        Check(seen.insert(entity).second,"Candidate entity duplicated");Check(Get<int>(row,8)>0,"Candidate score damaged");
        Check(std::abs(Get<float>(row,0x10)-1.f)<.0001f && Get<float>(row,0x14)==0.f,"Native direction normalization damaged");
    }
}
extern "C" void GuardedThreat(void* fn,void* actor,void* out);
asm(R"(
.text
.global GuardedThreat
GuardedThreat:
 push %rbx
 push %rbp
 push %r12
 push %r13
 push %r14
 push %r15
 sub $40,%rsp
 mov %rdx,32(%rsp)
 mov %rdi,%rax
 mov %rsi,%rcx
 mov $0x11223344,%ebx
 mov $0x22334455,%ebp
 mov $0x33445566,%edi
 mov $0x44556677,%esi
 mov $0x55667788,%r12d
 mov $0x66778899,%r13d
 mov $0x778899aa,%r14d
 mov $0x8899aabb,%r15d
 pcmpeqd %xmm6,%xmm6
 pcmpeqd %xmm7,%xmm7
 pcmpeqd %xmm8,%xmm8
 pcmpeqd %xmm9,%xmm9
 pcmpeqd %xmm10,%xmm10
 pcmpeqd %xmm11,%xmm11
 pcmpeqd %xmm12,%xmm12
 pcmpeqd %xmm13,%xmm13
 pcmpeqd %xmm14,%xmm14
 pcmpeqd %xmm15,%xmm15
 call *%rax
 mov 32(%rsp),%rdx
 mov %rbx,0(%rdx)
 mov %rbp,8(%rdx)
 mov %rdi,16(%rdx)
 mov %rsi,24(%rdx)
 mov %r12,32(%rdx)
 mov %r13,40(%rdx)
 mov %r14,48(%rdx)
 mov %r15,56(%rdx)
 movups %xmm6,64(%rdx)
 movups %xmm7,80(%rdx)
 movups %xmm8,96(%rdx)
 movups %xmm9,112(%rdx)
 movups %xmm10,128(%rdx)
 movups %xmm11,144(%rdx)
 movups %xmm12,160(%rdx)
 movups %xmm13,176(%rdx)
 movups %xmm14,192(%rdx)
 movups %xmm15,208(%rdx)
 add $40,%rsp
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %rbp
 pop %rbx
 ret
)");
bool Preserved(const std::array<std::uint8_t,224>& out){
    const std::uint64_t values[]={0x11223344,0x22334455,0x33445566,0x44556677,0x55667788,0x66778899,0x778899aa,0x8899aabb};
    for(unsigned n=0;n<8;++n)if(Get<std::uint64_t>(out.data(),n*8)!=values[n])return false;
    for(unsigned n=64;n<224;++n)if(out[n]!=0xff)return false;return true;
}
void Populate(unsigned count){
    using namespace mod::expansion;
    expected=count;scored=captured=0;selected=0;std::memset(image+Pool,0,kStride);std::memset(image+Sentients,0,kSentients*0x88);
    Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(image+Pool));Put(image,0xd96e98,reinterpret_cast<std::uintptr_t>(image+Sentients));
    Put(image,0xd9c398,reinterpret_cast<std::uintptr_t>(image+Dummy));Put(image,0xd9c178,reinterpret_cast<std::uintptr_t>(image+Dummy));Put(image,Dummy+0x10,int(0));
    Put(image,0xd96ec8,int(10000));Put(image+Pool,0,reinterpret_cast<std::uintptr_t>(image+Entities+0x2a0));Put(image+Pool,8,reinterpret_cast<std::uintptr_t>(image+Sentients));
    Put(image+Entities+0x2a0,0x118,reinterpret_cast<std::uintptr_t>(image+Sentients));Put(image+Sentients,8,int(2));
    const auto mask=Get<unsigned>(image,0x31e750+2*4);Check(mask,"Empty native opposing-team mask");unsigned team=0;while(!(mask&(1u<<team)))++team;
    for(unsigned n=1;n<=count;++n){auto* s=image+Sentients+n*0x88;auto* e=image+Entities+(n+1)*0x2a0;
        std::memset(e,0,0x2a0);Put(s,0,reinterpret_cast<std::uintptr_t>(e));Put(s,8,int(team));s[0x82]=1;
        Put(e,0x7e,std::uint16_t(n+1));Put(e,0x164,int(100));
        Put(image+Pool,kPerceptionStart+n*48+16,int(9000));Put(image+Pool,kPerceptionStart+n*48+28,float(100+n));
    }
}
unsigned EarlyExits(){
    // Three distinct native return paths must undo the larger frame.
    unsigned count=0;for(unsigned mode=0;mode<3;++mode){Populate(0);std::array<std::uint8_t,224> out{};
        if(mode==0){Put(image,Dummy+0x10,int(1));Put(image+Pool,0xd78,int(11000));}
        if(mode==1){Put(image+Sentients,0x48,std::uint16_t(2));Put(image+Sentients,0x4c,Get<float>(image,0x385a54));}
        if(mode==2){image[Sentients+0x15]=1;Put(image+Sentients,0x44,std::uint16_t(3));}
        std::array<unsigned,3> before{};std::memcpy(before.data(),image+Dummy+0x400,sizeof(before));
        GuardedThreat(image+0x156e00,image+Pool,out.data());Check(Preserved(out),"Native threat return damaged registers");
        for(unsigned n=0;n<3;++n)Check(Get<unsigned>(image,Dummy+0x400+n*4)==before[n]+(n==2-mode),"Expected native early exit not visited");++count;
    }return count;
}
void InstrumentExits(){
    const unsigned exits[]={0x156f70,0x156fbd,0x1573c3};
    for(unsigned n=0;n<3;++n){const auto at=exits[n];const auto relay=Dummy+0x500+n*32;
        std::vector<std::uint8_t> code{0xff,0x05};auto append=[&](std::int32_t value){for(unsigned b=0;b<4;++b)code.push_back((std::uint32_t(value)>>(b*8))&255);};
        append(Dummy+0x400+n*4-(relay+6));code.insert(code.end(),image+at,image+at+7);code.push_back(0xe9);append(at+7-(relay+code.size()+4));
        std::memcpy(image+relay,code.data(),code.size());image[at]=0xe9;Put(image,at+1,std::int32_t(relay-at-5));image[at+5]=image[at+6]=0x90;
    }
}
unsigned Searches(){
    using Search=void*(ABI*)(void*,void*,const float*,float);auto search=reinterpret_cast<Search>(image+0x128290);
    unsigned cases=0;for(unsigned n=1;n<274;++n){Populate(273);
        for(unsigned s=1;s<274;++s)image[Sentients+s*0x88+0x82]=s==n;
        std::array<float,3> point{};
        Check(search(image+Pool,nullptr,point.data(),1000.f)==image+Entities+(n+1)*0x2a0,"Recent-threat search misses relocated/high sentient");++cases;
    }return cases;
}
unsigned NativeHandles(){
    const unsigned sites[]={0x128083,0x157289,0x1572b9,0x19b314,0x1b6631,0x1b693d};unsigned cases=0;
    for(unsigned slot=0;slot<274;++slot)Put(image,Sentients+slot*0x88,reinterpret_cast<std::uintptr_t>(image+Entities+slot*0x2a0));
    for(unsigned at:sites){std::vector<std::uint8_t> code{0x41,0x54,0x41,0x56,0x41,0x57,0x49,0x89,0xd3,0x4c,0x69,0xd1,0x88,0,0,0};
        const auto setup=at==0x19b314?std::vector<std::uint8_t>{0x4c,0x89,0xd0,0x4c,0x89,0xda}:
            std::vector<std::uint8_t>{0x4c,0x89,0xd1,0x4c,0x89,0xd8,0x4d,0x89,0xdf,0x4d,0x89,0xde};
        code.insert(code.end(),setup.begin(),setup.end());
        // Copy only the real patched instruction and its following ADD for
        // LEA-based resolutions. Both load-based resolutions are eight bytes.
        unsigned bytes=(at==0x128083 || at==0x19b314)?8:10;
        if(at==0x1b693d){const std::uint8_t setupRdx[]={0x4c,0x89,0xd2};code.insert(code.end(),setupRdx,setupRdx+3);}
        code.insert(code.end(),image+at,image+at+bytes);
        if(at==0x128083){const std::uint8_t result[]={0x48,0x89,0xc8};code.insert(code.end(),result,result+3);}
        if(at==0x1b6631){const std::uint8_t result[]={0x4c,0x89,0xe0};code.insert(code.end(),result,result+3);}
        const std::uint8_t end[]={0x41,0x5f,0x41,0x5e,0x41,0x5c,0xc3};code.insert(code.end(),end,end+7);std::memcpy(image+Dummy,code.data(),code.size());
        using Resolve=std::uintptr_t(ABI*)(unsigned,void*);auto resolve=reinterpret_cast<Resolve>(image+Dummy);
        for(unsigned n=1;n<=274;++n){auto result=resolve(n,image);auto expectedPointer=image+((at==0x128083 || at==0x19b314)?Entities+(n-1)*0x2a0:Sentients+(n-1)*0x88);
            Check(result==reinterpret_cast<std::uintptr_t>(expectedPointer),"Native handle resolves original/wrong pool");++cases;}
    }return cases;
}
unsigned EntityReferences(){
    using namespace mod::expansion;
    image[0x19fb37]=0xc3;
    const std::uint8_t stub[]={0x53,0x55,0x48,0x89,0xcb,0x31,0xed,0x48,0xb8};std::memcpy(image+Dummy,stub,sizeof(stub));
    Put(image,Dummy+sizeof(stub),reinterpret_cast<std::uintptr_t>(image+0x19fb06));
    const std::uint8_t tail[]={0xff,0xd0,0x5d,0x5b,0xc3};std::memcpy(image+Dummy+sizeof(stub)+8,tail,sizeof(tail));
    const auto doomed=reinterpret_cast<std::uintptr_t>(image+Entities+0x2a0),other=doomed+0x2a0;
    for(unsigned n=0;n<kSentients;++n)Put(image,Sentients+n*0x88+0x38,n%2?other:doomed);
    std::array<std::uint8_t,50*0x88+0xc0> old{};std::memcpy(old.data(),image+0xd9c580,old.size());
    reinterpret_cast<void(ABI*)(std::uintptr_t)>(image+Dummy)(doomed);
    for(unsigned n=0;n<kSentients;++n)Check(Get<std::uintptr_t>(image,Sentients+n*0x88+0x38)==(n%2?other:0),"High sentient retains freed entity reference");
    Check(!std::memcmp(old.data(),image+0xd9c580,old.size()),"Original entity/global memory changed");return kSentients;
}
}
int main(int argc,char** argv){if(argc!=3)return 2;rlimit zero{0,0};setrlimit(RLIMIT_CORE,&zero);try{
    std::ifstream file(argv[1],std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(file)),{});
    image=static_cast<std::uint8_t*>(mmap(nullptr,0x5800000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED && data.size()==0x5800000,"Invalid private fixture");std::memcpy(image,data.data(),data.size());data.clear();data.shrink_to_fit();
    struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
    Detour(0x156890,reinterpret_cast<std::uintptr_t>(&Score));Detour(0x1b1cb0,reinterpret_cast<std::uintptr_t>(&Commit));Detour(0x156560,reinterpret_cast<std::uintptr_t>(&Secondary));
    if(std::string(argv[2])=="old"){phase="old candidate 51";Populate(51);std::array<std::uint8_t,224> out{};GuardedThreat(image+0x156e00,image+Pool,out.data());
        Check(!Preserved(out),"Old threat overwrite failed to reproduce");std::cerr<<"CANDIDATE_51_CORRUPTS_NATIVE_NONVOLATILE_REGISTERS\n";return 1;}
    InstrumentExits();unsigned passes=0;phase="expanded threat candidates";
    for(unsigned n:{0u,1u,32u,49u,50u,51u,59u,60u,90u,97u,128u,255u,256u,273u})for(unsigned repeat=0;repeat<8;++repeat){
        Populate(n);std::array<std::uint8_t,224> out{};GuardedThreat(image+0x156e00,image+Pool,out.data());
        Check(Preserved(out),"Expanded threat overwrites native saved registers");Check(scored==n && captured==n,"Expanded threat misses candidates");
        Check(selected==(n?reinterpret_cast<std::uintptr_t>(image+Entities+(n+1)*0x2a0):0),"High-index best threat was not selected");++passes;
    }
    phase="native early exits";auto exits=EarlyExits();phase="recent threat searches";auto searches=Searches();phase="native handle resolution";auto handles=NativeHandles();phase="native entity reference cleanup";auto references=EntityReferences();
    std::cout<<"{\"native_threat_cases\":"<<passes<<",\"maximum_hostile_candidates\":273,\"native_saved_gprs_per_case\":8,\"native_saved_xmm_registers_per_case\":10,\"native_early_exit_cases\":"<<exits<<",\"native_recent_threat_searches\":"<<searches<<",\"native_biased_handle_resolutions\":"<<handles<<",\"native_sentient_entity_reference_cleanup\":"<<references<<",\"best_target_past_old_limit\":true,\"full_secondary_candidate_list\":true,\"game_world_predicates_and_commit\":\"isolated\",\"windows_gameplay_tested\":false}\n";
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 2;}}
