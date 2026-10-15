// Execute the exact native script-variable code: InitVariables (0x221600),
// AllocObject (0x21b640 -> entry allocator 0x21b7e0),
// GetNewVariable (0x21d1e0), FindArrayVariable
// (0x21c410), RemoveVariable (0x21dde0) and RemoveRefToObject (0x21db10,
// which clears children through ClearObject 0x21bb60 and frees the parent).
// Only the two native error reporters are substituted (recorded + longjmp).
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <climits>
#ifdef SCRIPT_VM_HEADER
#include "script_vm_patches.hpp"
#endif
#define ABI __attribute__((ms_abi))
namespace {
std::uint8_t* image=nullptr;std::uint8_t* region=nullptr;std::size_t imageBytes=0;const char* phase="init";
// Synthetic owned RVAs (>= 0x6000000) live in the separately placed region.
std::uint8_t* At(std::uint64_t rva){return rva>=0x6000000?region+(rva-0x6000000):image+rva;}
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[256];int n=std::snprintf(b,sizeof(b),"SCRIPT_VM_FAULT phase=%s rva=0x%llx address=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,255));(void)ignored;}_exit(71);}
void Hook(unsigned at,std::uintptr_t target){std::uint8_t j[14]={0xff,0x25,0,0,0,0};std::memcpy(j+6,&target,8);std::memcpy(image+at,j,14);}
std::uint8_t* Load(const char* name){
 std::ifstream f(name,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()>0x5000000,"Invalid native image");
 imageBytes=bytes.size();
 auto* data=static_cast<std::uint8_t*>(mmap(nullptr,bytes.size(),7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Image mapping failed");std::memcpy(data,bytes.data(),bytes.size());
 const auto optional=Get<unsigned>(data,0x3c)+24;const auto preferred=Get<std::uint64_t>(data,optional+24);
 auto at=Get<unsigned>(data,optional+112+5*8);const auto end=at+Get<unsigned>(data,optional+112+5*8+4);const auto delta=std::uintptr_t(data)-preferred;
 while(at<end){const auto page=Get<unsigned>(data,at),length=Get<unsigned>(data,at+4);Check(length>=8 && at+length<=end,"Bad relocation block");for(unsigned n=8;n<length;n+=2){auto entry=Get<std::uint16_t>(data,at+n);if(!entry)continue;Check(entry>>12==10,"Unexpected relocation kind");const auto target=page+(entry&4095);Put(data,target,Get<std::uint64_t>(data,target)+delta);}at+=length;}
 return data;
}
std::jmp_buf errorJump;std::string error;
[[noreturn]] void ABI ScriptError(const char* text){error=text?text:"";std::longjmp(errorJump,1);}
[[noreturn]] void ABI EngineError(int,const char* text){error=text?text:"";if(!error.empty() && error[0]<32)error.erase(0,1);std::longjmp(errorJump,1);}
using Alloc=unsigned short(ABI*)();
using NewVar=unsigned(ABI*)(unsigned,unsigned);
using FindArray=unsigned(ABI*)(unsigned,unsigned);
using RemoveVar=void(ABI*)(unsigned,unsigned);
using FreeObj=void(ABI*)(unsigned);
struct Layout {std::uintptr_t list,counts;unsigned pools;};
// Stand-in for the owned MT pointer heap the DLL publishes through cells.
alignas(16) std::uint8_t heap[1<<16];std::size_t heapNext=0;unsigned resets=0,frees=0;void* lastFree=nullptr;int lastFreeSize=0;
void* ABI HeapAlloc(int size,int){const std::size_t bytes=(std::size_t(size)+15)&~std::size_t(15);void* p=heap+heapNext;heapNext+=bytes;return p;}
void ABI HeapFree(void* p,int size){++frees;lastFree=p;lastFreeSize=size;}
void ABI HeapReset(){++resets;heapNext=0;}
constexpr std::uintptr_t kSlots[]={0x7000000000,0x7100000000};
constexpr unsigned PerParent=16;
struct Fill {std::vector<unsigned> parents;unsigned children=0;std::string error;};
Fill Populate(unsigned limit){
 Fill f;error.clear();
 volatile unsigned children=0;std::vector<unsigned>* parents=&f.parents;
 if(setjmp(errorJump)==0){
  while(children<limit){
   const unsigned p=reinterpret_cast<Alloc>(image+0x21b640)();parents->push_back(p);
   for(unsigned n=0;n<PerParent && children<limit;++n){reinterpret_cast<NewVar>(image+0x21d1e0)(p,0x10000+n);children=children+1;}
  }
 }
 f.children=children;f.error=error;return f;
}
unsigned Sum(const Layout& l){unsigned s=0;for(unsigned k=0;k<l.pools;++k)s+=Get<unsigned>(At(l.counts),k*4);return s;}
#ifdef SCRIPT_VM_HEADER
// Apply include/script_vm_patches.hpp exactly as src/script_vm.cpp does, with
// the region at a different address than its synthetic RVA.
std::vector<std::uint8_t> Hex(const char* t){std::vector<std::uint8_t> o;auto d=[](char c)->unsigned{return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};for(;t[0]&&t[1];t+=2)o.push_back(std::uint8_t(d(t[0])*16+d(t[1])));return o;}
unsigned ApplyHeader(){
 using namespace mod::script_vm;
 auto* want=image+0x6020000;
 void* p=mmap(want,kRegionBytes,7,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);Check(p==want,"Region placement failed");region=static_cast<std::uint8_t*>(p);
 const auto host=std::uintptr_t(image);
 auto address=[&](std::uint32_t rva){return std::uintptr_t(At(rva));};
 unsigned native=0;
 for(const auto& patch:kPatches){
  auto before=Hex(patch.before),after=Hex(patch.after);
  if(patch.rva<kRegionRva){Check(before.size()==after.size() && std::memcmp(image+patch.rva,before.data(),before.size())==0,"Header original bytes differ");++native;}
  for(const auto& f:patch.fixups){
   std::int64_t value=std::int64_t(address(f.target))-(f.relative?std::int64_t(address(f.end)):std::int64_t(host+f.end));
   Check(f.width==4 && value>=INT32_MIN && value<=INT32_MAX,"Header fixup range");const auto v=std::int32_t(value);std::memcpy(after.data()+f.offset,&v,4);
  }
  std::memcpy(At(patch.rva),after.data(),after.size());
 }
 return native;
}
#endif
}
int main(int argc,char** argv){if(argc<3)return 2;const unsigned long listGuard=argc>3?std::stoul(argv[3],nullptr,16):0,dobjGuard=argc>4?std::stoul(argv[4],nullptr,16):0;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 image=Load(argv[1]);const std::string mode=argv[2];
 Hook(0x2906b0,std::uintptr_t(&ScriptError));Hook(0x1fb5f0,std::uintptr_t(&EngineError));
 Layout l=mode=="stock"?Layout{0x15c4400,0x15a03b4,2}:Layout{0x6002000,0x6001000,8};
 if(mode=="header"){
#ifdef SCRIPT_VM_HEADER
  const unsigned native=ApplyHeader();std::cerr<<"header native patches="<<native<<" region_delta=0x"<<std::hex<<(region-image)<<std::dec<<"\n";
#else
  throw std::runtime_error("Header mode not compiled");
#endif
 } else if(mode!="stock")region=image+0x6000000;
 if(mode!="stock"){
  // Inaccessible page after the relocated list: any write past it faults.
  Check(listGuard && dobjGuard && (mode=="header" || dobjGuard+0x1000==imageBytes),"Guard layout differs");
  Check(mprotect(At(listGuard),0x1000,PROT_NONE)==0 && mprotect(At(dobjGuard),0x1000,PROT_NONE)==0,"Guard page failed");
  const std::uintptr_t cells[]={std::uintptr_t(&HeapAlloc),std::uintptr_t(&HeapFree),std::uintptr_t(&HeapReset),std::uintptr_t(heap),sizeof(heap)};
  std::memcpy(At(0x6001200),cells,sizeof(cells));
  std::memcpy(At(0x6001100),kSlots,sizeof(kSlots));
 }
 phase="native InitVariables";reinterpret_cast<void(ABI*)()>(image+0x221600)();
 Check(Sum(l)==0,"Counters not cleared");
 phase="fill child pools";
 const auto first=Populate(1u<<22);
 Check(!first.error.empty(),"Child pools never reported exhaustion");
 const unsigned total=Sum(l);
 std::vector<unsigned> perPool(l.pools);for(unsigned k=0;k<l.pools;++k)perPool[k]=Get<unsigned>(At(l.counts),k*4);
 phase="native lookup of every child";
 unsigned found=0,mismatch=0;
 {
  unsigned remaining=first.children;
  for(unsigned p:first.parents){
   for(unsigned n=0;n<PerParent && remaining;++n,--remaining){
    const unsigned id=reinterpret_cast<FindArray>(image+0x21c410)(p,n);
    if(id)++found;else ++mismatch;
   }
  }
 }
 std::cout<<"{\"mode\":\""<<mode<<"\",\"children_before_error\":"<<first.children<<",\"parents\":"<<first.parents.size()
          <<",\"error\":\""<<first.error<<"\",\"counted_children\":"<<total<<",\"found_by_native_lookup\":"<<found<<",\"lookup_misses\":"<<mismatch<<",\"per_pool\":[";
 for(unsigned k=0;k<l.pools;++k)std::cout<<(k?",":"")<<perPool[k];
 std::cout<<"]";
 if(mode!="stock"){
  phase="native RemoveVariable on half of each parent";
  unsigned removed=0;
  for(unsigned p:first.parents)for(unsigned n=0;n<PerParent/2;++n){
   if(reinterpret_cast<FindArray>(image+0x21c410)(p,n)){reinterpret_cast<RemoveVar>(image+0x21dde0)(p,0x10000+n);++removed;}
  }
  const unsigned afterRemove=Sum(l);
  Check(afterRemove+removed==total,"Counters disagree after RemoveVariable");
  for(unsigned p:first.parents)for(unsigned n=0;n<PerParent/2;++n)Check(reinterpret_cast<FindArray>(image+0x21c410)(p,n)==0,"Removed child still found");
  phase="native RemoveRefToObject of every parent";
  for(unsigned p:first.parents)reinterpret_cast<FreeObj>(image+0x21db10)(p);
  Check(Sum(l)==0,"Counters not zero after release");
  // Every released parent must be back on free list (id & (pools-1)) with a cleared status.
  unsigned freedStatus=0;
  for(unsigned p:first.parents)if(Get<unsigned>(At(l.list),(p+1)*24+0x10)&0x60)++freedStatus;
  Check(freedStatus==0,"Released parent still marked in use");
  unsigned onLists=0;
  for(unsigned k=0;k<l.pools;++k){unsigned at=Get<std::uint16_t>(At(l.list),(k+1)*24+8),guard=0;
   while(at!=k && guard<0x10000){Check((at&(l.pools-1))==k,"Parent on the wrong free list");++onLists;at=Get<std::uint16_t>(At(l.list),(at+1)*24+8);++guard;}}
  phase="refill after full release";
  const auto second=Populate(1u<<22);
  std::cerr<<"refill children="<<second.children<<" parents="<<second.parents.size()<<" first="<<first.children<<" "<<first.parents.size()<<" err="<<second.error<<"\n";
  Check(second.children+PerParent>=first.children && second.children<=first.children+PerParent,"Capacity changed after release");
  Check(second.parents.size()+1>=first.parents.size(),"Parents not reusable after release");
  // Stock list storage must remain unused.
  std::vector<std::uint8_t> zero(0x18f4460-0x15c4400,0);
  const bool stockUntouched=std::memcmp(image+0x15c4400,zero.data(),zero.size())==0;
  Check(stockUntouched,"Stock variable list was written");
  phase="single high pool exhaustion";
  reinterpret_cast<void(ABI*)()>(image+0x221600)();Check(Sum(l)==0,"Counters not cleared on reinit");
  unsigned target=0;for(int n=0;n<64 && !target;++n){unsigned p=reinterpret_cast<Alloc>(image+0x21b640)();if((p&7)==5)target=p;else reinterpret_cast<NewVar>(image+0x21d1e0)(p,0x1ffff);}
  Check(target!=0,"No parent landed on list 5");
  error.clear();volatile unsigned single=0;
  if(setjmp(errorJump)==0){for(unsigned n=0;n<0x20000;++n){reinterpret_cast<NewVar>(image+0x21d1e0)(target,0x10000+n);single=single+1;}}
  Check(!error.empty(),"High pool never reported exhaustion");
  Check(Get<unsigned>(At(l.counts),5*4)==single && single>=0xc7f0,"High pool counter mismatch");
  for(unsigned n=0;n<single;n+=97)Check(reinterpret_cast<FindArray>(image+0x21c410)(target,n)!=0,"High pool child lost");
  std::cout<<",\"single_pool5_children\":"<<single<<",\"single_pool5_error\":\""<<error<<"\"";
  phase="native MT_Init through relay";
  using MtInit=void(ABI*)();using MtAlloc=void*(ABI*)(int,int);using MtIndex=unsigned(ABI*)(int,int);using MtFree=void(ABI*)(void*,int);
  reinterpret_cast<MtInit>(image+0x217780)();
  Check(resets==1 && Get<unsigned>(image,0x1566080)==0x7fffffff && Get<std::uint64_t>(image,0x1466008)==std::uint64_t(image+0x1466080),"MT_Init relay did not reset owned heap and native tree");
  phase="native MT_Alloc/MT_Free routing";
  void* owned=reinterpret_cast<MtAlloc>(image+0x216ee0)(100,2);
  Check(owned>=heap && owned<heap+sizeof(heap),"MT_Alloc did not use the owned heap");
  reinterpret_cast<MtFree>(image+0x217500)(owned,100);
  Check(frees==1 && lastFree==owned && lastFreeSize==100,"Owned MT block not routed to owned free");
  std::vector<std::uint8_t> before(0x2000);std::memcpy(before.data(),image+0x1566080,before.size());
  const unsigned index=reinterpret_cast<MtIndex>(image+0x216f00)(100,2);
  Check(index>0 && index<0x10000 && std::memcmp(before.data(),image+0x1566080,before.size())!=0,"Native MT index allocation failed");
  reinterpret_cast<MtFree>(image+0x217500)(image+0x1466080+std::size_t(index)*16,100);
  Check(frees==1 && std::memcmp(before.data(),image+0x1566080,before.size())==0,"Native MT block not returned to the native tree");
  phase="native checkpoint slot init";
  reinterpret_cast<void(ABI*)()>(image+0x1aee70)();
  Check(Get<std::uint64_t>(image,0x1157300)==kSlots[0] && Get<unsigned>(image,0x1157308)==0x2000000 &&
        Get<std::uint64_t>(image,0x1157800)==kSlots[1] && Get<unsigned>(image,0x1157808)==0x2000000,"Checkpoint slots not published from owned cells");
  std::cout<<",\"mt_routing\":true,\"checkpoint_slot_bytes\":"<<Get<unsigned>(image,0x1157308);
  std::cout<<",\"removed_children\":"<<removed<<",\"free_parents_after_release\":"<<onLists<<",\"refill_children\":"<<second.children<<",\"stock_list_untouched\":true";
 }
 std::cout<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"SCRIPT_VM_CHECK phase="<<phase<<" "<<e.what()<<"\n";return 1;}}
