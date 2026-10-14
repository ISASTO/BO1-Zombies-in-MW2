// Exact native skeleton allocators and glass allocation/free. Real assets,
// visibility, physics teardown and rendering are not part of this fixture.
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
#include <random>
#include <stdexcept>
#include <cstdio>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="initialization";
constexpr std::size_t Cells=0x57e0000;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[256];int n=std::snprintf(b,sizeof(b),"RESOURCE_FAULT phase=%s rva=0x%llx address=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,255));(void)ignored;}_exit(71);}
using Reset=void(ABI*)();using SAllocate=void*(ABI*)(unsigned);using CAllocate=void*(ABI*)(void*,unsigned);
using GAllocate=unsigned(ABI*)(void*,unsigned,unsigned,unsigned,unsigned);using GFree=void(ABI*)(void*,unsigned);
struct Guarded {
 std::uint8_t* data=nullptr;std::size_t bytes;
 explicit Guarded(std::size_t n):bytes(n){data=static_cast<std::uint8_t*>(mmap(nullptr,n+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Skeleton mapping failed");Check(mprotect(data+n,4096,PROT_NONE)==0,"Skeleton guard failed");}
 ~Guarded(){if(data && data!=MAP_FAILED)munmap(data,bytes+4096);}
};
struct Glass {
 std::array<std::uint8_t,0xa8> pool{};
 std::vector<std::uint8_t> places,states,dynamics;
 std::vector<std::uint32_t> bits;
 unsigned count;
 explicit Glass(unsigned n):places(n*32),states(n*32),dynamics(n*48),bits((n+31)/32),count(n) {
  Put(pool.data(),0xc,n);Put(pool.data(),0x10,unsigned(bits.size()));Put(pool.data(),0x20,0u);Put(pool.data(),0x24,65535u);
  Put(pool.data(),0x38,std::uintptr_t(places.data()));Put(pool.data(),0x40,std::uintptr_t(states.data()));Put(pool.data(),0x48,std::uintptr_t(dynamics.data()));Put(pool.data(),0x58,std::uintptr_t(bits.data()));
  for(unsigned i=0;i<n;++i)Put(places.data(),i*32,i+1<n?i+1:0xffffu);
 }
 bool Active(unsigned n)const{return bits[n/32]&(0x80000000u>>(n%32));}
 unsigned Allocate(){return reinterpret_cast<GAllocate>(image+0x100410)(pool.data(),0,0,0,0);}
 void Free(unsigned n){reinterpret_cast<GFree>(image+0x100ed0)(pool.data(),n);}
 void Transform(unsigned n){for(unsigned j=0;j<8;++j)Put(places.data(),n*32+j*4,0x3f33787fu+n*17+j);}
 void Invariant(const std::set<unsigned>& live)const {
  Check(Get<unsigned>(pool.data(),0x1c)==live.size(),"Glass active census differs");
  for(unsigned n=0;n<count;++n)Check(Active(n)==(live.count(n)!=0),"Glass occupancy differs from live ownership");
  std::set<unsigned> free;auto at=Get<unsigned>(pool.data(),0x20);
  while(at!=0xffff){Check(at<count && !Active(at) && free.insert(at).second,"Glass free chain crosses bounds, overlaps live data or cycles");at=Get<unsigned>(places.data(),at*32);}
  Check(free.size()+live.size()==count,"Glass free chain loses a slot");
 }
};
}
int main(int argc,char** argv){if(argc!=3)return 2;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Invalid private image");image=static_cast<std::uint8_t*>(mmap(nullptr,0x5800000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED,"Image mapping failed");std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 image[0x10b4f0]=0xc3; // Released-piece renderer/physics broadphase callback.
 const auto mode=std::string(argv[2]);
 if(mode=="old-head"){phase="returned corrupt glass free head";Glass g(128);Put(g.pool.data(),0x20,0x3f33787fu);g.Allocate();throw std::runtime_error("Old corrupt head unexpectedly returned");}
 if(mode=="old-double-free"){phase="old duplicate glass free";Glass g(128);auto a=g.Allocate();g.Free(a);g.Free(a);auto x=g.Allocate(),y=g.Allocate();Check(x!=y,"DOUBLE_FREE_REISSUES_ONE_LIVE_PIECE");return 0;}
 auto clientAllocate=reinterpret_cast<CAllocate>(image+0xd56d0);auto serverAllocate=reinterpret_cast<SAllocate>(image+0x230ed0);auto clientReset=reinterpret_cast<Reset>(image+0xd4a10);auto serverReset=reinterpret_cast<Reset>(image+0x2318f0);
 constexpr unsigned BoneBytes=255*32;
 if(mode=="old-client"){phase="stock client skeleton exhaustion";clientReset();unsigned count=0;while(clientAllocate(nullptr,BoneBytes))++count;Check(count==72,"Unexpected stock client exhaustion");std::cout<<"{\"client_skeletons_before_exhaustion\":72}\n";return 0;}
 if(mode=="old-server"){phase="stock server skeleton wrap";serverReset();auto* first=serverAllocate(BoneBytes);for(unsigned n=1;n<64;++n)Check(serverAllocate(BoneBytes)!=first,"Stock skeleton wrapped early");Check(serverAllocate(BoneBytes)==first,"Stock server did not wrap at allocation 65");std::cout<<"{\"server_wrap_allocation\":65}\n";return 0;}
 for(const auto& guard:kArenaGuards)Check(mprotect(image+guard.rva+0x1000000,guard.bytes,PROT_NONE)==0,"Near-arena guard failed");
 Guarded client(kClientSkeletonBytes),server(kServerSkeletonBytes);Put(image,Cells,std::uintptr_t(client.data));Put(image,Cells+8,std::uintptr_t(server.data));
 phase="expanded native client/server skeleton frames";unsigned allocations=0;
 for(unsigned frame=0;frame<16;++frame){clientReset();serverReset();auto epoch=Get<unsigned>(image,0x19c386c);std::set<void*> c,s;
  // Three maximum-size passes over all 256 actors plus 64 corpses. Unique
  // ranges and unchanged generation prove no within-frame wrap or alias.
  for(unsigned n=0;n<3*(256+64);++n){auto* a=static_cast<std::uint8_t*>(clientAllocate(nullptr,BoneBytes));auto* b=static_cast<std::uint8_t*>(serverAllocate(BoneBytes));Check(a==client.data+n*BoneBytes && b==server.data+n*BoneBytes,"Skeleton block lost or aliased");Check(c.insert(a).second && s.insert(b).second,"Two live skeleton allocations overlap");Check(Get<unsigned>(image,0x19c386c)==epoch,"Server recycled a live frame");a[0]=std::uint8_t(n);a[BoneBytes-1]=std::uint8_t(n+1);b[0]=std::uint8_t(n);b[BoneBytes-1]=std::uint8_t(n+1);allocations+=2;}
  for(unsigned n=0;n<960;++n)Check(client.data[n*BoneBytes]==std::uint8_t(n) && server.data[n*BoneBytes+BoneBytes-1]==std::uint8_t(n+1),"Later skeleton evaluation damaged earlier live skeletons");
 }
 phase="native DObj skeleton creation, reuse and frame invalidation";
 using Bone=unsigned(ABI*)(void*,unsigned);using Parts=unsigned(ABI*)(void*,const void*);using ClientParts=unsigned(ABI*)(void*,const void*,void**);
 const auto cbone=reinterpret_cast<Bone>(image+0xc9a60),sbone=reinterpret_cast<Bone>(image+0x231050);
 const auto spart=reinterpret_cast<Parts>(image+0x2310b0);const auto cpart=reinterpret_cast<ClientParts>(image+0xd5700);
 std::array<std::array<std::uint8_t,0xb0>,320> clientObjects{},serverObjects{};
 const std::array<std::uint32_t,6> requested{0x80000000,0,0,0,0,0};
 unsigned dobjCases=0;
 for(unsigned frame=0;frame<8;++frame){clientReset();serverReset();
  for(unsigned n=0;n<320;++n){auto& c=clientObjects[n];auto& s=serverObjects[n];c[0xe]=s[0xe]=255;
   Check(cbone(c.data(),0)==0 && sbone(s.data(),0)==0,"New-frame DObj should require bone evaluation");
   auto* cp=client.data+n*BoneBytes;auto* sp=server.data+n*BoneBytes;
   Check(Get<std::uintptr_t>(c.data(),0x80)==std::uintptr_t(cp) && Get<std::uintptr_t>(s.data(),0x80)==std::uintptr_t(sp),"Native DObj selected the wrong owned skeleton");
   Put(c.data(),0x60,0x80000000u);Put(s.data(),0x60,0x80000000u);
   Check(cbone(c.data(),0)==1 && sbone(s.data(),0)==1,"Already-calculated bone cache was lost");
   void* cached=nullptr;Check(cpart(c.data(),requested.data(),&cached)==1 && cached==cp && spart(s.data(),requested.data())==1,"Native part-bit reuse lost cached skeleton");dobjCases+=6;
  }
  Check(Get<unsigned>(image,0x548cf4)==320*BoneBytes && Get<unsigned>(image,0x19c3870)==320*BoneBytes,"Cached DObj reuse allocated duplicate live skeletons");
 }
 // Exercise the client's separate inlined allocation path as well.
 clientReset();for(unsigned n=0;n<320;++n){auto& c=clientObjects[n];void* out=nullptr;Check(cpart(c.data(),requested.data(),&out)==0 && out==client.data+n*BoneBytes,"Inlined client allocation uses a stale bound or pointer");++dobjCases;}
 phase="native skeleton exhaustion boundaries";clientReset();Put(image,0x548cf4,unsigned(kClientSkeletonBytes-32));Check(clientAllocate(nullptr,16)==client.data+kClientSkeletonBytes-32,"Last safe client block refused");Check(!clientAllocate(nullptr,16),"Client crossed end guard");
 serverReset();Put(image,0x19c3870,unsigned(kServerSkeletonBytes-32));auto epoch=Get<unsigned>(image,0x19c386c);Check(serverAllocate(16)==server.data+kServerSkeletonBytes-32,"Last safe server block refused");Check(serverAllocate(16)==server.data && Get<unsigned>(image,0x19c386c)!=epoch,"Server wrap did not reset storage/generation coherently");
 phase="returned glass free-head bits";unsigned corruptCases=0,duplicateCases=0;
 for(unsigned limit:{1u,31u,32u,33u,127u,128u,2048u})for(unsigned style=0;style<6;++style){
  Glass g(limit);std::set<unsigned> live;
  if(limit>1){auto a=g.Allocate();live.insert(a);g.Transform(a);}
  std::vector<std::uint8_t> before=g.places;
  const auto free=Get<unsigned>(g.pool.data(),0x20);
  if(style==0)Put(g.pool.data(),0x20,0x3f33787fu);
  if(style==1)Put(g.pool.data(),0x20,0x3f161262u);
  if(style==2 && !live.empty())Put(g.pool.data(),0x20,*live.begin());
  if(style==3)Put(g.places.data(),free*32,free);
  if(style==4)Put(g.places.data(),free*32,limit+9);
  if(style==5 && !live.empty())Put(g.places.data(),free*32,*live.begin());
  const auto a=g.Allocate();Check(a<limit && live.insert(a).second,"Corrupt chain reissued an active glass transform");
  for(auto n:live)if(n!=a)Check(!std::memcmp(g.places.data()+n*32,before.data()+n*32,32),"Repair modified a live glass transform");
  g.Transform(a);g.Invariant(live);++corruptCases;
 }
 phase="native glass full pool and duplicate release";
 for(unsigned limit:{1u,33u,128u,2048u}){
  Glass g(limit);std::set<unsigned> live;
  for(unsigned n=0;n<limit;++n){auto a=g.Allocate();Check(a<limit && live.insert(a).second,"Glass exhausted early or duplicated");g.Transform(a);}
  Check(g.Allocate()==0xffff,"Full glass pool exceeded its capacity");auto before=g.places;Put(g.pool.data(),0x20,0x3f33787fu);Check(g.Allocate()==0xffff && g.places==before,"Full-pool repair altered live glass");g.Invariant(live);
  for(unsigned n=0;n<limit;++n){g.Free(n);live.erase(n);g.Free(n);g.Free(0xffff);g.Free(0xffffffffu);g.Invariant(live);duplicateCases+=3;}
 }
 phase="deterministic native glass lifetime stress";Glass g(2048);std::set<unsigned> live;std::mt19937 rng(0x28a1);
 for(unsigned n=0;n<100000;++n){if(!live.empty() && ((rng()%100)<49 || live.size()==2048)){auto it=live.begin();std::advance(it,rng()%live.size());auto a=*it;g.Free(a);live.erase(it);if(n%31==0){g.Free(a);++duplicateCases;}}
  else {auto a=g.Allocate();Check(a<2048 && live.insert(a).second,"Stress allocation duplicated or escaped");g.Transform(a);}
  if(n%997==0 && live.size()<2048){Put(g.pool.data(),0x20,0x3f33787fu);auto a=g.Allocate();Check(a<2048 && live.insert(a).second,"Stress repair lost a slot");g.Transform(a);++corruptCases;}
  if(n%97==0)g.Invariant(live);
 }
 g.Invariant(live);phase="native glass geometry exhaustion";auto snapshot=g.pool;Put(g.pool.data(),0x28,65535u);Check(reinterpret_cast<GAllocate>(image+0x100410)(g.pool.data(),1,0,0,0)==0xffff,"Glass geometry budget exceeded");Put(g.pool.data(),0x28,Get<unsigned>(snapshot.data(),0x28));g.Invariant(live);
 std::cout<<"{\"guarded_client_skeleton_bytes\":"<<kClientSkeletonBytes<<",\"guarded_server_skeleton_bytes\":"<<kServerSkeletonBytes<<",\"native_skeleton_frames\":16,\"native_skeleton_allocations\":"<<allocations<<",\"native_dobj_create_cache_and_invalidation_cases\":"<<dobjCases<<",\"maximum_size_skeletons_per_frame\":960,\"actors_and_corpses_per_pass\":320,\"client_exhaustion_and_server_generation_wrap_checked\":true,\"glass_lifetime_operations\":100000,\"glass_corrupt_chain_cases\":"<<corruptCases<<",\"glass_invalid_or_duplicate_frees\":"<<duplicateCases<<",\"glass_rebuild_counter\":"<<Get<unsigned>(image,Cells+16)<<",\"live_glass_transforms_preserved\":true,\"guard_pages\":11,\"booted_game_tested\":false}\n";
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 1;}}
