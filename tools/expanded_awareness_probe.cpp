// Native nearby-sentient production, sort, refresh and register preservation.
// World visibility callbacks are isolated; construction/iteration/perception
// addresses, distance math, qsort and final record refresh execute natively.
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
#include <algorithm>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";
constexpr std::size_t Pool=0x5000000,Sentients=0x5500000,Entities=0xd9e070,Jit=0x56f0000;
unsigned expected=0,updates=0,refreshes=0,sorts=0,breakAfter=0;bool validateRecords=true;
std::set<std::uintptr_t> visited;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Detour(std::size_t at,std::uintptr_t fn){auto jump=mod::AbsoluteJump(fn);std::memcpy(image+at,jump.data(),jump.size());}
void LoaderRelocations(){
 // Apply the owned PE's DIR64 relocations as the Windows loader does. In
 // particular qsort dispatches its comparator through a relocated CFG thunk;
 // leaving that pointer at the preferred 0x140000000 base is not a valid test.
 const auto optional=Get<std::uint32_t>(image,0x3c)+24;
 Check(Get<std::uint16_t>(image,optional)==0x20b,"Expected PE32+");
 const auto preferred=Get<std::uint64_t>(image,optional+24);
 auto at=Get<std::uint32_t>(image,optional+112+5*8);
 const auto end=at+Get<std::uint32_t>(image,optional+112+5*8+4);
 const auto delta=std::uintptr_t(image)-preferred;
 while(at<end){const auto page=Get<std::uint32_t>(image,at),bytes=Get<std::uint32_t>(image,at+4);Check(bytes>=8 && at+bytes<=end,"Bad relocation block");
  for(unsigned n=8;n<bytes;n+=2){auto entry=Get<std::uint16_t>(image,at+n);if(!entry)continue;Check((entry>>12)==10,"Unexpected native relocation kind");const auto target=page+(entry&4095);Put(image,target,Get<std::uint64_t>(image,target)+delta);}
  at+=bytes;
 }
}
void Fault(int,siginfo_t* info,void* context){auto* u=static_cast<ucontext_t*>(context);char b[200];int n=std::snprintf(b,sizeof(b),"AWARENESS_FAULT phase=%s rva=%llx addr=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,199));(void)ignored;}_exit(71);}
using Compare=int(ABI*)(const void*,const void*);
using Sort=void(ABI*)(void*,std::size_t,std::size_t,Compare);
void ABI Sorted(void* rows,std::size_t count,std::size_t bytes,Compare compare){
 ++sorts;
 if(validateRecords){
  Check(count==expected && bytes==16,"Candidate count/stride truncated");
  std::set<std::uintptr_t> pointers;
  for(unsigned n=0;n<count;++n){auto* row=static_cast<char*>(rows)+n*16;
   const auto sentient=Get<std::uintptr_t>(row,0);
   Check(sentient>=(std::uintptr_t)(image+Sentients+0x88) && sentient<=(std::uintptr_t)(image+Sentients+expected*0x88),"Native candidate pointer damaged");
   Check(pointers.insert(sentient).second,"Native candidate duplicated");
   Check(std::isfinite(Get<float>(row,8)) && Get<float>(row,8)>0 && std::isfinite(Get<float>(row,12)) && Get<float>(row,12)>0,"Native candidate metrics damaged");
  }
 }
 reinterpret_cast<Sort>(image+0x2f6ab0)(rows,count,bytes,compare);
 if(validateRecords)for(unsigned n=1;n<count;++n)Check(compare(static_cast<char*>(rows)+(n-1)*bytes,static_cast<char*>(rows)+n*bytes)<=0,"Native qsort order incorrect");
}
void ABI Visibility(void* actor,void* entity){
 ++updates;Check(visited.insert((std::uintptr_t)entity).second,"Visibility candidate visited twice");
 if(breakAfter && updates==breakAfter)Put(actor,0x14f4,Get<int>(actor,0x14f4)+1);
}
void ABI Refresh(void*,void*){++refreshes;}
bool countRegistered=false;
std::uintptr_t ABI RegisterCount(const char* name,int value,int minimum,int maximum,unsigned flags,const char* description){
 Check(!std::strcmp(name,"ai_count") && value==256 && minimum==0 && maximum==256 && flags==0xc4 &&
       !std::strcmp(description,"Maximum number of AI"),"Actor dvar registration retains stock domain or changes other arguments");
 countRegistered=true;return (std::uintptr_t)(image+Jit+0x3000);
}
void CountDomain(){
 Detour(0x26cc10,(std::uintptr_t)&RegisterCount);
 // Enter the unchanged native registration argument sequence with its real
 // outgoing stack geometry, then stop before registration of the next dvar.
 const std::size_t at=Jit+0x400;
 const std::uint8_t setup[]={0x48,0x83,0xec,0x38,0xe9};std::memcpy(image+at,setup,sizeof(setup));
 Put(image,at+5,std::int32_t(0x172c98-(at+9)));
 const std::uint8_t tail[]={0x48,0x83,0xc4,0x38,0xc3};std::memcpy(image+0x172cc4,tail,sizeof(tail));
 reinterpret_cast<void(ABI*)()>(image+at)();Check(countRegistered,"Native actor dvar was not registered");
}
extern "C" void GuardedAwareness(void* fn,void* actor,void* out);
asm(R"(
.text
.global GuardedAwareness
GuardedAwareness:
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

void Populate(unsigned count,unsigned actorIndex,bool proximity){
 expected=count;updates=refreshes=sorts=breakAfter=0;visited.clear();
 std::memset(image+Pool,0,kActors*kStride);std::memset(image+Sentients,0,kSentients*0x88);
 Put(image,0xd96ea0,(std::uintptr_t)(image+Pool));Put(image,0xd96e98,(std::uintptr_t)(image+Sentients));Put(image,0xd96ec8,int(10000));
 auto* actor=image+Pool+actorIndex*kStride;auto* own=image+Entities+0x2a0;
 std::memset(own,0,0x2a0);Put(actor,0,(std::uintptr_t)own);Put(actor,8,(std::uintptr_t)(image+Sentients));Put(own,0x118,(std::uintptr_t)(image+Sentients));
 Put(image+Sentients,0,(std::uintptr_t)own);Put(image+Sentients,8,int(1));Put(actor,0x14ff,std::uint8_t(1));
 Put(actor,0xd98,proximity?100000.f:0.f);Put(actor,0x304,int(100));
 const auto mask=Get<unsigned>(image,0x31e750+4);Check(mask,"Native team mask empty");unsigned team=0;while(!(mask&(1u<<team)))++team;
 for(unsigned n=1;n<=count;++n){auto* s=image+Sentients+n*0x88;auto* e=image+Entities+(n+1)*0x2a0;
  std::memset(e,0,0x2a0);Put(s,0,(std::uintptr_t)e);Put(s,8,int(team));s[0x82]=1;Put(s,0x78,(std::uintptr_t)(image+Jit+0x1000+n*16));
  Put(e,0x118,(std::uintptr_t)s);Put(e,0xe8,float(100+n));Put(e,0xec,float(n));Put(e,0xf0,float(n%7));
 }
}
unsigned LatePerception(){
 struct Site{unsigned at,bytes;};const Site sites[]={{0x1b22ef,8},{0x1b4c70,8},{0x1b4cdf,10},{0x1b4cf7,9},{0x1b7799,7}};
 using Run=std::uintptr_t(ABI*)(void*,std::uint64_t,unsigned);
 unsigned cases=0;auto* actor=image+Pool+255*kStride;
 for(auto site:sites){
  for(unsigned n=0;n<274;++n){
   std::memset(actor,0xa5,kStride);
   const auto field=site.at==0x1b4cdf?28:site.at==0x1b4cf7?0:site.at==0x1b7799?20:16;
   if(site.at!=0x1b7799)Put(actor,kPerceptionStart+n*48+field,std::uint32_t(0));
   auto before=std::vector<std::uint8_t>(actor,actor+kStride);
   std::vector<std::uint8_t> code{0x53,0x56,0x41,0x56,0x48,0x89,0xcb,0x48,0x89,0xce,0x4c,0x6b,0xf2,0x30,0x48,0x6b,0xc2,0x06};
   if(site.at==0x1b22ef){const std::uint8_t b[]={0x48,0x89,0xc2};code.insert(code.end(),b,b+3);}
   if(site.at==0x1b4c70){const std::uint8_t b[]={0x44,0x89,0xc0};code.insert(code.end(),b,b+3);}
   if(site.at==0x1b7799){const std::uint8_t b[]={0x44,0x89,0xc1};code.insert(code.end(),b,b+3);}
   code.insert(code.end(),image+site.at,image+site.at+site.bytes);
   if(site.at==0x1b22ef || site.at==0x1b4cf7){const std::uint8_t b[]={0x0f,0x94,0xc0,0x0f,0xb6,0xc0};code.insert(code.end(),b,b+6);}
   if(site.at==0x1b4cdf){const std::uint8_t b[]={0x48,0x89,0xd0};code.insert(code.end(),b,b+3);}
   const std::uint8_t end[]={0x41,0x5e,0x5e,0x5b,0xc3};code.insert(code.end(),end,end+5);std::memcpy(image+Jit,code.data(),code.size());
   const auto result=reinterpret_cast<Run>(image+Jit)(actor,n,12345);
   if(site.at==0x1b7799)Put(before.data(),kPerceptionStart+n*48+20,std::uint32_t(12345));
   else Check(result==(site.at==0x1b4cdf?(std::uintptr_t)(actor+kPerceptionStart+n*48+28):site.at==0x1b4c70?12345:1),"Late native perception access uses old actor body");
   Check(!std::memcmp(before.data(),actor,kStride),"Late perception write changed unrelated actor bytes");++cases;
  }
 }
 return cases;
}
}
int main(int argc,char** argv){if(argc!=3)return 2;const rlimit zero{0,0};setrlimit(RLIMIT_CORE,&zero);try{
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(file)),{});
 image=static_cast<std::uint8_t*>(mmap(nullptr,0x5800000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED && data.size()==0x5800000,"Invalid fixture");std::memcpy(image,data.data(),data.size());data.clear();data.shrink_to_fit();
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 sigaction(SIGILL,&action,nullptr);sigaction(SIGBUS,&action,nullptr);
 LoaderRelocations();
 for(const auto& guard:kArenaGuards)Check(mprotect(image+guard.rva+0x1000000,guard.bytes,PROT_NONE)==0,"Native pool guard failed");
 Detour(0x150ae0,(std::uintptr_t)&Visibility);Detour(0x151420,(std::uintptr_t)&Refresh);Detour(Jit+0x800,(std::uintptr_t)&Sorted);
 image[0x1522e3]=0xe8;Put(image,0x1522e4,std::int32_t(Jit+0x800-0x1522e8));
 if(std::string(argv[2])=="old"){
  phase="old nearby candidate 51";Populate(51,0,false);validateRecords=false;std::array<std::uint8_t,224> out{};
  GuardedAwareness(image+0x152100,image+Pool,out.data());Check(!Preserved(out),"Old nearby overwrite did not reproduce");
  Check(Get<std::uintptr_t>(out.data(),96)==(std::uintptr_t)(image+Sentients+51*0x88),"Candidate pointer was not restored into XMM8");
  std::cerr<<"NEARBY_CANDIDATE_51_RESTORES_SENTIENT_POINTER_IN_XMM8\n";return 1;
 }
 unsigned cases=0;std::uint64_t candidates=0;
 phase="all population boundaries";
 for(unsigned actorIndex:{0u,31u,32u,50u,59u,90u,128u,255u})for(unsigned n:{0u,1u,31u,32u,49u,50u,51u,59u,60u,90u,97u,128u,255u,256u,273u})for(bool proximity:{false,true}){
  Populate(n,actorIndex,proximity);std::array<std::uint8_t,224> out{};auto* actor=image+Pool+actorIndex*kStride;
  GuardedAwareness(image+0x152100,actor,out.data());Check(Preserved(out),"Nearby frame damages nonvolatile registers");Check(updates==n && refreshes==(proximity?n:0) && sorts==(n>1),"Nearby traversal/refresh incomplete");
  for(unsigned s=1;s<=n;++s)if(proximity){Check(Get<int>(actor,kPerceptionStart+s*48+16)==10000,"High sentient sight time not refreshed");Check(Get<std::uintptr_t>(actor,kPerceptionStart+s*48+40)==Get<std::uintptr_t>(image,Sentients+s*0x88+0x78),"High perception node lost");}
  ++cases;candidates+=n;
 }
 phase="all actor slots under full load";
 for(unsigned cycle=0;cycle<4;++cycle)for(unsigned slot=0;slot<256;++slot){
  Populate(273,slot,true);std::array<std::uint8_t,224> out{};GuardedAwareness(image+0x152100,image+Pool+slot*kStride,out.data());Check(Preserved(out) && updates==273 && refreshes==273,"Full-load actor refresh failed");++cases;candidates+=273;
 }
 phase="no-team early exit";Populate(0,255,false);Put(image+Sentients,8,int(0));std::array<std::uint8_t,224> early{};GuardedAwareness(image+0x152100,image+Pool+255*kStride,early.data());Check(Preserved(early) && !updates,"No-team return damages frame");
 phase="visibility early exit";Populate(273,255,false);breakAfter=51;std::array<std::uint8_t,224> changed{};GuardedAwareness(image+0x152100,image+Pool+255*kStride,changed.data());Check(Preserved(changed) && updates==51,"Epoch break damages frame");
 phase="remaining perception consumers";const auto late=LatePerception();
 phase="native actor-count dvar domain";CountDomain();
 std::cout<<"{\"native_awareness_cases\":"<<cases<<",\"native_candidate_records_checked\":"<<candidates<<",\"maximum_candidates\":273,\"actor_slots_exercised\":256,\"native_qsort_and_comparator\":true,\"saved_gprs_per_case\":8,\"saved_xmm_registers_per_case\":10,\"early_exit_cases\":2,\"late_perception_cases\":"<<late<<",\"native_ai_count_default_and_maximum\":256,\"inaccessible_resource_guard_pages\":8,\"visibility_world_callbacks\":\"isolated\",\"windows_gameplay_tested\":false}\n";
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 2;}}
