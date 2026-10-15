// Execute exact native glass polygon construction with guarded piece output.
// The fixture replaces CRT memory calls and the producer's world/asset helpers.
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";
constexpr unsigned ContextBytes=0x7d0,Cells=0x57e0000;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[256];int n=std::snprintf(b,sizeof(b),"FRACTURE_FAULT phase=%s rva=0x%llx address=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,255));(void)ignored;}_exit(71);}
void Hook(unsigned at,std::uintptr_t target){auto jump=mod::AbsoluteJump(target);std::memcpy(image+at,jump.data(),jump.size());}
void ABI Fill(void* to,unsigned value,std::size_t count){std::memset(to,value,count);}
void ABI Copy(void* to,const void* from,std::size_t count){std::memmove(to,from,count);}
std::uint8_t* Load(const char* name){
 std::ifstream f(name,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Invalid native image");
 auto* data=static_cast<std::uint8_t*>(mmap(nullptr,bytes.size(),7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Image mapping failed");std::memcpy(data,bytes.data(),bytes.size());
 const auto optional=Get<unsigned>(data,0x3c)+24;const auto preferred=Get<std::uint64_t>(data,optional+24);
 auto at=Get<unsigned>(data,optional+112+5*8);const auto end=at+Get<unsigned>(data,optional+112+5*8+4);const auto delta=std::uintptr_t(data)-preferred;
 while(at<end){const auto page=Get<unsigned>(data,at),length=Get<unsigned>(data,at+4);Check(length>=8 && at+length<=end,"Bad relocation block");for(unsigned n=8;n<length;n+=2){auto entry=Get<std::uint16_t>(data,at+n);if(!entry)continue;Check(entry>>12==10,"Unexpected relocation kind");const auto target=page+(entry&4095);Put(data,target,Get<std::uint64_t>(data,target)+delta);}at+=length;}
 image=data;Hook(0x2e42d0,std::uintptr_t(&Fill));Hook(0x2e3b40,std::uintptr_t(&Copy));image[0x2b9300]=0xc3;return data;
}
struct Guarded {
 std::uint8_t* mapping;std::uint8_t* data;
 Guarded(){mapping=static_cast<std::uint8_t*>(mmap(nullptr,8192,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(mapping!=MAP_FAILED,"Output mapping failed");Check(mprotect(mapping+4096,4096,PROT_NONE)==0,"Output guard failed");data=mapping+4096-ContextBytes;}
 ~Guarded(){munmap(mapping,8192);}
};
struct Node {std::uint8_t a,b,flag,pad;std::array<std::uint8_t,20> unused{};Node* next=nullptr;};
static_assert(sizeof(Node)==32 && offsetof(Node,next)==24);
struct Geometry {
 std::array<std::uint8_t,0x4f70> work{};
 std::array<Node,1024> nodes{};
 unsigned vertices=0,edges=0,initial=0;
 void Point(unsigned i,int x,int y){Put(work.data(),i*4,static_cast<std::int16_t>(x));Put(work.data(),i*4+2,static_cast<std::int16_t>(y));Put(work.data(),0x3fc+i*8,float(x)/32);Put(work.data(),0x400+i*8,float(y)/32);vertices=std::max(vertices,i+1);}
 void Ring(unsigned n,unsigned start=0){edges=n;for(unsigned i=0;i<n;++i){nodes[i].a=start+i;nodes[i].b=start+(i+1)%n;nodes[i].next=&nodes[(i+1)%n];}}
 void Degenerate(){Point(3,-9,62);Point(8,-1013,363);vertices=25;edges=1;nodes[0].a=3;nodes[0].b=8;nodes[0].next=&nodes[0];initial=25;}
 void Convex(unsigned n){for(unsigned i=0;i<n;++i){double a=6.283185307179586*i/n;Point(i,int(std::lround(320*std::cos(a))),int(std::lround(320*std::sin(a))));}Ring(n);initial=n;}
 void Prepare(void* out){std::memset(out,0,ContextBytes);Put(work.data(),0xbf4,vertices);Put(out,0x14,1u);Put(out,0x18,std::uintptr_t(nodes.data()));Put(out,0x20,std::uint16_t(vertices));Put(out,0x320,std::uint8_t(vertices));Put(out,0x430,initial);std::memset(static_cast<char*>(out)+0x321,0xff,255);for(unsigned i=0;i<vertices;++i)Put(out,0x321+i,std::uint8_t(i));}
 void Run(void* out){reinterpret_cast<void(ABI*)(void*,void*)>(image+0x10a8b0)(work.data(),out);}
};
Geometry* producer=nullptr;unsigned published=0;
bool ABI Outer(void*,void* out){std::memset(out,0,ContextBytes);Put(out,0x14,1u);Put(out,0x18,std::uintptr_t(producer->nodes.data()));Put(out,0x20,std::uint16_t(producer->edges));std::memset(static_cast<char*>(out)+0x321,0xff,255);return true;}
void ABI Publish(void*,void* out){Check(Get<unsigned>(out,0x430)<=231,"Invalid piece reached native publication");++published;}
void Publisher(Geometry& g,bool expected){
 producer=&g;Put(g.work.data(),0xbf4,g.vertices);float thickness=1;Put(g.work.data(),0x4f28,std::uintptr_t(&thickness));
 Hook(0x10a3d0,std::uintptr_t(&Outer));Hook(0x104dc0,std::uintptr_t(&Publish));
 const auto before=published;reinterpret_cast<void(ABI*)(void*,void*,unsigned)>(image+0x109160)(g.work.data(),nullptr,0);
 Check(published-before==unsigned(expected),"Native producer publication gate failed");
}
}
int main(int argc,char** argv){if(argc<3)return 2;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 image=Load(argv[1]);const auto mode=std::string(argv[2]);
 if(mode=="old-degenerate"){phase="repeated two-vertex native output";Geometry g;g.Degenerate();Guarded out;g.Prepare(out.data);g.Run(out.data);throw std::runtime_error("Old degenerate output unexpectedly bounded");}
 if(mode=="old-full"){phase="full native piece output";Geometry g;g.Convex(3);g.initial=231;Guarded out;g.Prepare(out.data);g.Run(out.data);throw std::runtime_error("Old full output unexpectedly bounded");}
 Check(argc==4,"Baseline image required");auto* fixed=image;auto* old=Load(argv[3]);unsigned equivalent=0,rejected=0,inputs=0;
 phase="valid native geometry compatibility";
 for(unsigned count:{3u,4u,5u,8u,16u,32u,64u})for(unsigned initial:{count,200u,230u}){
  // Large polygons can exceed the four-byte remainder; only compare valid
  // complete outputs with sufficient room, plus an exact-full triangle.
  if(initial==230 && count!=3)continue;if(initial==200 && count>16)continue;
  Geometry g;g.Convex(count);g.initial=initial;Guarded out;
  image=old;g.Prepare(out.data);g.Run(out.data);std::array<std::uint8_t,ContextBytes> baseline{};std::memcpy(baseline.data(),out.data,ContextBytes);
  image=fixed;g.Prepare(out.data);g.Run(out.data);Check(std::memcmp(baseline.data(),out.data,ContextBytes)==0,"Valid native glass output changed");Check(Get<unsigned>(out.data,0x430)<=231,"Valid geometry count overflowed");++equivalent;
 }
 image=fixed;phase="bounded degenerate and full-output rejection";
 for(unsigned n=0;n<160;++n){Geometry g;g.Degenerate();Guarded out;g.Prepare(out.data);g.Run(out.data);Check(Get<unsigned>(out.data,0x430)==0xffffffffu,"Degenerate piece was not rejected");++rejected;}
 for(unsigned initial:{230u,231u})for(unsigned count:{4u,8u,32u,128u}){
  Geometry g;g.Convex(count);g.initial=initial;Guarded out;g.Prepare(out.data);g.Run(out.data);Check(Get<unsigned>(out.data,0x430)==0xffffffffu,"Overfull piece was not rejected");++rejected;
 }
 phase="invalid native input counts";
 for(unsigned kind=0;kind<4;++kind){Geometry g;g.Convex(3);Guarded out;g.Prepare(out.data);if(kind==0)Put(out.data,0x430,232u);if(kind==1)Put(g.work.data(),0xbf4,256u);if(kind==2)Put(out.data,0x14,33u);if(kind==3)Put(out.data,0x20,std::uint8_t(129));g.Run(out.data);Check(Get<unsigned>(out.data,0x430)==0xffffffffu,"Invalid input was not rejected");++inputs;}
 {
  Geometry g;g.Degenerate();g.edges=513;for(unsigned i=0;i<g.edges;++i){g.nodes[i].a=i%2?8:3;g.nodes[i].b=i%2?3:8;g.nodes[i].next=&g.nodes[(i+1)%g.edges];}
  Guarded out;g.Prepare(out.data);g.Run(out.data);Check(Get<unsigned>(out.data,0x430)==0xffffffffu,"Boundary-edge work array was not bounded");++inputs;
 }
 phase="native producer allocation/publication gate";
 Geometry bad;bad.Degenerate();Publisher(bad,false);Geometry good;good.Convex(3);Publisher(good,true);
 auto* state=reinterpret_cast<CrowdResourceState*>(image+Cells);
 Check(state->invalidFragments>=160 && state->fragmentOutputRejections>=8 && state->fragmentInputRejections==5,"Native rejection counters differ");
 std::cout<<"{\"valid_geometry_byte_comparisons\":"<<equivalent<<",\"degenerate_and_output_rejections\":"<<rejected<<",\"input_rejections\":"<<inputs<<",\"native_producer_publication_cases\":2,\"valid_piece_publications\":"<<published<<",\"output_guard_exact_bytes\":924,\"invalid_fragment_counter\":"<<state->invalidFragments<<",\"output_rejection_counter\":"<<state->fragmentOutputRejections<<",\"input_rejection_counter\":"<<state->fragmentInputRejections<<"}\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"FRACTURE_CHECK phase="<<phase<<" "<<e.what()<<"\n";return 1;}}
