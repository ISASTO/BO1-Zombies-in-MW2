// Exercise the exact native scratch-tree allocator/release, corpse searches,
// and actor-to-corpse ownership exchange. No booted renderer is claimed.
#include "expanded_actor_layout.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include <stdexcept>
#include <cstdio>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";
constexpr std::size_t Actors=0x5560000,Temp=0x55a0000,Flags=0x55a0a80,Cursor=0x55a0bd0,Corpses=0xf50140,Root=0xf4e1a0,Fake=0x5700000;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[200];int n=std::snprintf(b,sizeof(b),"ANIMATION_FAULT phase=%s rva=0x%llx fault=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,199));(void)ignored;}_exit(71);}
void Jump(std::size_t at,std::size_t to){image[at]=0xe9;Put(image,at+1,std::int32_t(to-at-5));}
using Lease=std::uintptr_t(ABI*)();using Release=void(ABI*)(std::uintptr_t);
using Lookup=std::uintptr_t(ABI*)(void*);using Remove=void(ABI*)(void*);using Swap=void(ABI*)(unsigned,unsigned,void*);
void Populate(){std::memset(image+Corpses,0,64*40);for(unsigned n=0;n<64;++n){Put(image,Corpses+n*40,std::uintptr_t(image+Fake+n*128));Put(image,Corpses+n*40+8,int(600+n));}for(unsigned n=0;n<256;++n)Put(image,Actors+n*8,std::uintptr_t(image+Fake+0x20000+n*128));}
Swap SwapShim(){
 // Jump into the original native swap with its actual registers. Only its
 // following script notifications are replaced by a jump to this epilogue.
 constexpr std::size_t at=0x57e0000;
 std::vector<std::uint8_t> b{0x56,0x57,0x53,0x41,0x56,0x41,0x57,0x41,0x89,0xcf,0x41,0x89,0xd6,0x4c,0x89,0xc7,0xe9};
 const auto offset=std::int32_t(0x131ff9-(at+b.size()+4));const auto* r=reinterpret_cast<const std::uint8_t*>(&offset);b.insert(b.end(),r,r+4);const auto end=at+b.size();
 for(auto c:{0x41,0x5f,0x41,0x5e,0x5b,0x5f,0x5e,0xc3})b.push_back(static_cast<std::uint8_t>(c));std::memcpy(image+at,b.data(),b.size());Jump(0x13203c,end);return reinterpret_cast<Swap>(image+at);
}
Lookup SearchState(){
 // Execute the third real corpse search through selection of its goal state,
 // then return RBX before its booted-world geometry and animation callbacks.
 const std::uint8_t epilogue[]={0x48,0x89,0xd8,0x48,0x81,0xc4,0x10,0x01,0,0,0x5b,0xc3};std::memcpy(image+0x1322e7,epilogue,sizeof(epilogue));return reinterpret_cast<Lookup>(image+0x132280);
}
}
int main(int argc,char**argv){if(argc!=3)return 2;const rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Invalid fixture");image=static_cast<std::uint8_t*>(mmap(nullptr,0x6000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED,"mmap failed");std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 auto lease=reinterpret_cast<Lease>(image+0x12c6d0);auto release=reinterpret_cast<Release>(image+0x12c730);image[0x29b7d0]=0xc3; // tree-node reset requires real assets; pool release runs intact
 for(unsigned n=0;n<336;++n)Put(image,Temp+n*8,std::uintptr_t(image+Fake+0x40000+n*128));Put(image,Cursor,int(0));Put(image,0xf50f30,int(0));std::memset(image+Flags,0,336);
 if(std::strcmp(argv[2],"old-leases")==0){phase="old scratch-tree alias";const auto a=lease(),b=lease();Check(a!=b,"TWO_LIVE_LEASES_SHARE_ONE_TREE");return 0;}
 Populate();std::array<std::uint8_t,0x2a0> entity{};entity[0]=15;auto lookup=reinterpret_cast<Lookup>(image+0x132dd0);auto remove=reinterpret_cast<Remove>(image+0x131ea0);auto state=SearchState();auto swap=SwapShim();
 if(std::strcmp(argv[2],"old-end")==0){phase="old corpse search";Put(image,0xf50b48,int(1000));Put(image,0xf50b40,std::uintptr_t(0x10001000005d7ull));Put(entity.data(),0x7e,std::uint16_t(1000));Check(lookup(entity.data())==Get<std::uintptr_t>(image,Corpses),"CORPSE_SEARCH_ACCEPTED_ONE_PAST_END");return 0;}
 if(std::strcmp(argv[2],"old-swap")==0){phase="old corpse tree swap";Put(image,0xf50040+59*8,std::uintptr_t(0x10001000005d7ull));const auto expected=Get<std::uintptr_t>(image,Actors+59*8);swap(59,0,entity.data());Check(Get<std::uintptr_t>(image,Corpses)==expected,"ACTOR_59_REUSED_PACKED_CORPSE_DATA_AS_TREE");return 0;}
 phase="unique native scratch leases";std::set<std::uintptr_t> live;
 for(unsigned n=0;n<336;++n){const auto tree=lease();Check(tree==Get<std::uintptr_t>(image,Temp+((n+1)%336)*8),"Wrong final cursor read");Check(live.insert(tree).second,"Mutable scratch trees alias");}Check(lease()==0,"Pool exhausted beyond bound");
 phase="native scratch release and recycle";
 unsigned recycles=0;for(unsigned n=0;n<1000;++n){const auto slot=(n*131)%336;const auto tree=Get<std::uintptr_t>(image,Temp+slot*8);Check(live.erase(tree)==1,"Leased tree missing");release(tree);Check(image[Flags+slot]==0,"Wrong occupancy flag released");const auto again=lease();Check(again==tree && live.insert(again).second && image[Flags+slot]==1,"Recycled tree not uniquely owned");Check(lease()==0,"Occupied tree re-leased");++recycles;}
 phase="bounded native corpse searches";unsigned searchCases=0;
 Put(image,0xf50b48,int(1000));Put(image,0xf50b40,std::uintptr_t(0x10001000005d7ull));
 for(unsigned n=0;n<65;++n){const auto index=n==64?0:n;Put(entity.data(),0x7e,std::uint16_t(n==64?1000:600+n));
  Check(lookup(entity.data())==Get<std::uintptr_t>(image,Corpses+index*40),"Corpse tree lookup crossed array");
  Check(state(entity.data())==(std::uintptr_t)(image+Corpses+index*40+12),"Corpse state lookup crossed array");
  remove(entity.data());Check(Get<int>(image,Corpses+index*40+8)==-1,"Corpse removal selected wrong entry");Put(image,Corpses+index*40+8,int(600+index));Check(Get<int>(image,0xf50b48)==1000,"Corpse removal wrote past end");searchCases+=3;
 }
 phase="native 256-slot actor/corpse tree ownership";unsigned swaps=0;
 for(unsigned n=0;n<256;++n){Populate();const auto c=n%64;const auto actorTree=Get<std::uintptr_t>(image,Actors+n*8),corpseTree=Get<std::uintptr_t>(image,Corpses+c*40);std::array<std::uint8_t,64*40> before{};std::memcpy(before.data(),image+Corpses,before.size());swap(n,c,entity.data());Check(Get<std::uintptr_t>(image,Actors+n*8)==corpseTree && Get<std::uintptr_t>(image,Corpses+c*40)==actorTree,"Wrong actor/corpse tree ownership");Put(before.data(),c*40,actorTree);Put(before.data(),c*40+8,int(Get<std::uint16_t>(entity.data(),0x7e)));Check(!std::memcmp(before.data(),image+Corpses,before.size()),"Swap overwrote unrelated corpse metadata");for(unsigned other=0;other<256;++other)if(other!=n)Check(Get<std::uintptr_t>(image,Actors+other*8)==(std::uintptr_t)(image+Fake+0x20000+other*128),"Swap overwrote another actor tree");++swaps;}
 std::cout<<"{\"distinct_simultaneous_scratch_trees\":336,\"native_tree_release_recycles\":"<<recycles<<",\"bounded_corpse_search_cases\":"<<searchCases<<",\"native_actor_corpse_tree_swaps\":"<<swaps<<",\"packed_metadata_not_used_as_tree\":true,\"unrelated_corpse_metadata_preserved\":true,\"live_actor_capacity\":256,\"corpse_capacity\":64,\"booted_renderer_tested\":false}\n";munmap(image,0x6000000);
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 1;}}
