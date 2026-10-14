// Execute the exact native allocation blocks and collision routines using the
// checked runtime plan. GPU/VM OS services are recording substitutes, not D3D.
#include "crowd_resources.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#define ABI __attribute__((ms_abi))
struct NativeRegisters {
 std::array<std::uint64_t,16> gpr{};
 std::array<std::array<std::uint8_t,16>,16> xmm{};
 std::uint64_t flags=0x202;
};
static_assert(offsetof(NativeRegisters,xmm)==128 && offsetof(NativeRegisters,flags)==384);
extern "C" {
 std::uintptr_t crowd_saved_stack=0,crowd_entry=0,crowd_publish_entry=0;
 NativeRegisters crowd_result{};
 void crowd_execute(void*,const NativeRegisters*);
 void crowd_snapshot();
 unsigned ABI crowd_publish(void*,unsigned,unsigned,void*);
 void crowd_publish_success();void crowd_publish_failure();
}
#include "crowd_probe_cases.hpp"
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="load";
constexpr std::size_t Cells=0x57e0000;
constexpr std::size_t ProbeCode=0x57c0000;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[256];int n=std::snprintf(b,sizeof(b),"CROWD_FAULT phase=%s rva=0x%llx address=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,255));(void)ignored;}_exit(71);}
void Bytes(std::size_t at,std::initializer_list<unsigned char> code){std::memcpy(image+at,code.begin(),code.size());}
void Jump(std::size_t at,std::uintptr_t target){Bytes(at,{0x48,0xb8});Put(image,at+2,target);Bytes(at+10,{0xff,0xe0});}
void RelativeJump(std::size_t at,std::size_t target){image[at]=0xe9;Put(image,at+1,std::int32_t(target-at-5));}
void IndirectJump(std::uint8_t* memory,std::size_t at,std::uintptr_t target){
 memory[at]=0xff;memory[at+1]=0x25;Put(memory,at+2,0u);Put(memory,at+6,target);
}
struct Guarded {
 std::uint8_t* data;std::size_t size;
 explicit Guarded(std::size_t n):size(n){data=static_cast<std::uint8_t*>(mmap(nullptr,n+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Guarded allocation failed");Check(mprotect(data+n,4096,PROT_NONE)==0,"Guard page failed");}
 ~Guarded(){munmap(data,size+4096);}
};
std::vector<unsigned> gpuLengths,cpuLengths;
std::vector<std::uintptr_t> reservedCpu;
unsigned fakeBuffer=0;
long ABI CreateVertexBuffer(void*,unsigned bytes,unsigned usage,unsigned fvf,unsigned pool,void** out,void* shared){
 Check(usage==0x208 && fvf==0 && pool==0 && !shared,"Native GPU creation flags changed");gpuLengths.push_back(bytes);*out=&fakeBuffer;return 0;
}
void ABI IgnoreDynamicBuffer(void*,unsigned){}
void* ABI ReserveCpu(unsigned bytes){
 cpuLengths.push_back(bytes);auto* p=mmap(nullptr,std::size_t(bytes)+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 Check(p!=MAP_FAILED && !mprotect(static_cast<char*>(p)+bytes,4096,PROT_NONE),"Native CPU reservation substitute failed");reservedCpu.push_back(std::uintptr_t(p));return p;
}
void TestNativeCreation(unsigned expected){
 std::array<std::uintptr_t,40> vtable{};vtable[26]=std::uintptr_t(&CreateVertexBuffer);void* device=vtable.data();
 std::array<std::uint8_t,32> cvar{};cvar[16]=1;
 Put(image,0x41161a0,std::uintptr_t(cvar.data()));Put(image,0x410fd20,std::uintptr_t(&device));
 // The first dynamic vertex buffer and later index buffers are independent.
 Jump(0x24f00,std::uintptr_t(&IgnoreDynamicBuffer));Jump(0x269b60,std::uintptr_t(&ReserveCpu));RelativeJump(0x25072,0x25256);
 reinterpret_cast<void(ABI*)()>(image+0x24fa0)();
 Check(gpuLengths==std::vector<unsigned>({expected,expected}),"GPU API did not receive the matching capacity");
 Check(cpuLengths==gpuLengths,"CPU virtual reservations differ from GPU capacity");
 for(unsigned n=0;n<2;++n)Check(Get<unsigned>(image,0x4116588+n*24+4)==expected,"Native vertex metadata capacity differs");
}
using Reserve=unsigned(ABI*)(unsigned);
using Surface=void*(ABI*)(unsigned);
using Reset=void(ABI*)(void*);
Reserve GpuWrapper(){
 Bytes(ProbeCode,{0x57,0x89,0xca});Jump(ProbeCode+3,std::uintptr_t(image+0x530a7));
 Bytes(0x530cc,{0x89,0xf8,0x5f,0xc3});Bytes(0x531ce,{0xb8,0xff,0xff,0xff,0xff,0x5f,0xc3});
 return reinterpret_cast<Reserve>(image+ProbeCode);
}
Reserve CpuWrapper(){
 Bytes(ProbeCode+0x40,{0x89,0xca});Jump(ProbeCode+0x42,std::uintptr_t(image+0x531ea));
 Bytes(0x53224,{0x89,0xc8,0xc3});Bytes(0x53208,{0xb8,0xff,0xff,0xff,0xff,0xc3});
 return reinterpret_cast<Reserve>(image+ProbeCode+0x40);
}
Surface SurfaceWrapper(){
 // Simulate a nonzero native stack-buffer base, not just a byte count.
 Bytes(ProbeCode+0x80,{0x53,0x57,0x55,0xbd,0x00,0x00,0x10,0x00,0x8d,0x99,0x00,0x00,0x10,0x00});
 Jump(ProbeCode+0x8e,std::uintptr_t(image+0x5318c));
 Bytes(0x532cc,{0x48,0x01,0xd7,0x48,0x89,0xf8,0x5d,0x5f,0x5b,0xc3});
 Bytes(0x531b2,{0x31,0xc0,0x5d,0x5f,0x5b,0xc3});
 Bytes(0x531bc,{0x31,0xc0,0x5d,0x5f,0x5b,0xc3});
 return reinterpret_cast<Surface>(image+ProbeCode+0x80);
}
Reset ResetWrapper(){
 Bytes(ProbeCode+0xc0,{0x56,0x31,0xf6});Jump(ProbeCode+0xc3,std::uintptr_t(image+0xcc18));Bytes(0xcc1e,{0x5e,0xc3});
 return reinterpret_cast<Reset>(image+ProbeCode+0xc0);
}
void SetPublicationContinuations(){
 crowd_publish_entry=std::uintptr_t(image+0x532e2);
 IndirectJump(image,ProbeCode+0x300,std::uintptr_t(&crowd_publish_success));
 IndirectJump(image,ProbeCode+0x320,std::uintptr_t(&crowd_publish_failure));
 RelativeJump(0x5330f,ProbeCode+0x300);RelativeJump(0x532e9,ProbeCode+0x300);RelativeJump(0x531bc,ProbeCode+0x320);
}
NativeRegisters ExecuteFragment(std::uint8_t* memory,unsigned at,unsigned end,const NativeRegisters& input){
 std::array<std::uint8_t,5> saved{};std::memcpy(saved.data(),memory+end,5);
 memory[end]=0xe9;Put(memory,end+1,std::int32_t(ProbeCode+0x800-end-5));
 IndirectJump(memory,ProbeCode+0x800,std::uintptr_t(&crowd_snapshot));
 crowd_execute(memory+at,&input);auto result=crowd_result;std::memcpy(memory+end,saved.data(),5);return result;
}
unsigned BridgeEquivalence(std::uint8_t* original,CrowdResourceState& state){
 std::array<std::uint8_t,0x9000> stack{};unsigned cases=0;
 const auto make=[&](unsigned seed){NativeRegisters r;
  for(unsigned n=0;n<16;++n){r.gpr[n]=0x1234567800100000ull+seed*0x10001ull+n*0x22334455ull;for(unsigned j=0;j<16;++j)r.xmm[n][j]=std::uint8_t(n*31+j*7+seed);}
  r.gpr[4]=std::uintptr_t(stack.data()+0x1000);r.gpr[5]=std::uintptr_t(stack.data()+0x5000);r.flags=0x202|((seed*97)&0x8d5);return r;};
 for(unsigned frameNumber=0;frameNumber<2;++frameNumber){
  auto* frame=image+kRenderFrameBaseRva+frameNumber*kRenderFrameStride;Put(image,0x410fd00,std::uintptr_t(frame));Put(original,0x410fd00,std::uintptr_t(frame));
  auto& f=state.frames[frameNumber];auto* table=reinterpret_cast<std::uintptr_t*>(f.table);auto* reverse=reinterpret_cast<std::uint16_t*>(f.reverse);
  for(unsigned id:{0u,31u,0x7fe0u,0x8000u,0x8001u,0x9234u,0xffffu})for(unsigned seed=0;seed<4;++seed){
   auto* record=id<0x8000?frame+0x42980+id*4:reinterpret_cast<std::uint8_t*>(f.surfaces)+0x1000+(id&0x7fff)*48;
   Check(record+128<=reinterpret_cast<std::uint8_t*>(f.surfaces)+kCrowdSurfaceBytes || id<0x8000,"Bridge test record escapes its guarded pool");
   for(unsigned n=0;n<128;++n)record[n]=std::uint8_t(n*13+id+seed*53);
   if(id>=0x8000){table[id]=std::uintptr_t(record);reverse[(std::uintptr_t(record)-f.surfaces)/4]=std::uint16_t(id&0x7fff);}
   for(const auto& c:bridgeCases){
    auto updated=make(seed);auto oracle=updated;
    if(c.form>=3){
     updated.gpr[7]=std::uintptr_t(record);oracle.gpr[7]=std::uintptr_t(frame+0x42980+id*4);
     if(c.form==3){updated.gpr[2]=oracle.gpr[2]=0x7e00000000000000ull;updated.gpr[8]=oracle.gpr[8]=0x0120000000000000ull;}
     else {updated.gpr[6]=oracle.gpr[6]=std::uintptr_t(frame);updated.gpr[9]=oracle.gpr[9]=0x0120000000000000ull;}
    }else{
     updated.gpr[c.base]=std::uintptr_t(frame);updated.gpr[c.index]=id;oracle=updated;
     if(c.form==2){updated.gpr[7]=std::uintptr_t(frame+0x42980);oracle.gpr[7]=std::uintptr_t(record)-id*4;}
     else oracle.gpr[c.base]=std::uintptr_t(record)-id*4-0x42980;
    }
    std::fill(stack.begin(),stack.end(),0x6b);auto expected=ExecuteFragment(original,c.at,c.end,oracle);
    auto expectedByte=Get<unsigned>(stack.data()+0x1000,0x70),expectedMatrix=Get<unsigned>(stack.data()+0x5000,0xa0);
    std::fill(stack.begin(),stack.end(),0x6b);auto actual=ExecuteFragment(image,c.at,c.end,updated);
    if(c.form>=3)expected.gpr[7]=std::uintptr_t(record);
    else if(c.form!=2 && c.base!=c.destination)expected.gpr[c.base]=std::uintptr_t(frame);
    if(expected.gpr!=actual.gpr || expected.xmm!=actual.xmm || ((expected.flags^actual.flags)&c.flags)){
     std::cerr<<"bridge mismatch site="<<std::hex<<c.at<<" id="<<id<<" frame="<<frameNumber<<" form="<<c.form<<std::dec<<'\n';
     for(unsigned r=0;r<16;++r)if(expected.gpr[r]!=actual.gpr[r])std::cerr<<" r"<<r<<" expected="<<std::hex<<expected.gpr[r]<<" actual="<<actual.gpr[r]<<std::dec<<'\n';
     throw std::runtime_error("Native packed surface register/flags/SIMD equivalence failed");
    }
    Check(Get<unsigned>(stack.data()+0x1000,0x70)==expectedByte && Get<unsigned>(stack.data()+0x5000,0xa0)==expectedMatrix,"Surface bridge changed native local writes");++cases;
   }
  }
 }
 return cases;
}
using StartupRead=std::uintptr_t(ABI*)(void*,unsigned);
StartupRead StartupReadWrapper(){
 // Isolate the exact quarter-unit addressing block, then execute the same
 // mesh-field read that faults in both returned mission-start dumps. Asset,
 // shader and D3D work between those blocks is excluded from this fixture.
 Bytes(ProbeCode+0xa00,{0x53,0x56,0x48,0x83,0xec,0x28,0x49,0x89,0xc8,0x89,0xd6});
 Jump(ProbeCode+0xa0b,std::uintptr_t(image+0x643d3));
 RelativeJump(0x643e4,0x645b9);
 Bytes(0x645c1,{0x48,0x83,0xc4,0x28,0x5e,0x5b,0xc3});
 return reinterpret_cast<StartupRead>(image+ProbeCode+0xa00);
}
unsigned StartupReadCases(CrowdResourceState& state){
 const auto read=StartupReadWrapper();std::array<std::uint8_t,0x40> mesh{};unsigned cases=0;
 for(unsigned n=0;n<2;++n){auto* frame=image+kRenderFrameBaseRva+n*kRenderFrameStride;auto& f=state.frames[n];auto* table=reinterpret_cast<std::uintptr_t*>(f.table);
  for(unsigned id:{0u,31u,0x7fe0u,0x8000u,0x8001u,0x9234u,0xffffu}){
   auto* record=id<0x8000?frame+0x42980+id*4:reinterpret_cast<std::uint8_t*>(f.surfaces)+0x100+(id&0x7fff)*48;
   const auto expected=0x1234567800000000ull+n*0x10000ull+id;
   std::memset(record,0,48);Put(record,0,std::int32_t(-4));Put(record,0x20,std::uintptr_t(mesh.data()));Put(mesh.data(),0x10,expected);table[id]=std::uintptr_t(record);
   Check(read(frame,id)==expected,"Quarter-unit CPU renderer read a mesh from the old pool or another frame");++cases;
  }
 }
 return cases;
}
using Glass=void(ABI*)(const void*,const void*,unsigned,const void*,unsigned,void*);
unsigned selectedFeature=0;
bool ABI SelectFeature(const void*,const void*,const void*,void* output){
 Put(output,0,1.f);Put(output,4,selectedFeature);Put(output,8,0.f);Put(output,12,0.f);Put(output,16,1.f);return true;
}
using EarlyPose=unsigned(ABI*)(const void*,const void*);
EarlyPose EarlyPoseWrapper(){
 // Preserve the native locals' actual offsets while isolating only the
 // broadphase entry gate from geometry-method/world callbacks.
 Bytes(ProbeCode+0x100,{0x56,0x57,0x53,0x48,0x81,0xec,0x10,0x01,0x00,0x00,
   0x48,0x89,0xd3,0x48,0x89,0xce,0x48,0x8d,0x7c,0x24,0x50,
   0xb9,0x17,0x00,0x00,0x00,0xf3,0x48,0xa5,0x48,0x89,0xde,
   0x48,0x8d,0x7c,0x24,0x20,0xb9,0x03,0x00,0x00,0x00,0xf3,0x48,0xa5,
   0xbf,0x34,0x12,0x00,0x00});
 Jump(ProbeCode+0x132,std::uintptr_t(image+0x1ecfeb));
 // The two native destinations overlap. Route each to its own fixture stub.
 RelativeJump(0x1ecffa,ProbeCode+0x180);RelativeJump(0x1ed005,ProbeCode+0x1a0);
 Bytes(ProbeCode+0x180,{0xb8,0x01,0x00,0x00,0x00,0x48,0x81,0xc4,0x10,0x01,0x00,0x00,0x5b,0x5f,0x5e,0xc3});
 Bytes(ProbeCode+0x1a0,{0x31,0xc0,0x48,0x81,0xc4,0x10,0x01,0x00,0x00,0x5b,0x5f,0x5e,0xc3});
 return reinterpret_cast<EarlyPose>(image+ProbeCode+0x100);
}
struct GlassInput {
 std::array<std::uint8_t,0xb8> query{};
 std::array<std::uint8_t,0xa8> pool{};
 std::array<std::uint8_t,32> piece{};
 std::array<std::uint8_t,16> reference{};
 std::array<unsigned short,3> triangle{0,1,2};
 std::array<float,9> vertices{-4,-4,0,4,-4,0,0,4,0};
 std::array<std::uint8_t,256> output{};
 GlassInput(){
  Put(query.data(),0,0x0280e491u);Put(query.data(),0x34,7u);
  Put(query.data(),0x24,4.f);Put(query.data(),0x28,4.f);Put(query.data(),0x2c,4.f);Put(query.data(),0x30,7.f);
  Put(query.data(),0x44,1.f);Put(query.data(),0x54,1.f);Put(query.data(),0x64,1.f);
  Put(piece.data(),0x1c,4.47835493f);Put(pool.data(),0x38,std::uintptr_t(piece.data()));Put(reference.data(),0,std::uintptr_t(pool.data()));
  Put(query.data(),0x90,std::uintptr_t(reference.data()));Put(query.data(),0xa8,1.f);Put(query.data(),0xb0,1u);
 }
 void Nan(){for(unsigned at=0x18;at<=0x88;at+=4)if(at!=0x34)Put(query.data(),at,0x7fc00000u);for(unsigned at=0xa0;at<=0xa8;at+=4)Put(query.data(),at,0x7fc00000u);}
 void Run(){reinterpret_cast<Glass>(image+0x1dfa90)(triangle.data(),vertices.data(),1,query.data(),0x1100000,output.data());}
};
}
int main(int argc,char** argv){if(argc<3 || argc>4)return 2;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try {
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});Check(bytes.size()==0x5800000,"Invalid image size");
 image=static_cast<std::uint8_t*>(mmap(nullptr,bytes.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED,"Image mapping failed");std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 const auto mode=std::string(argv[2]);
 if(mode=="old-nan"){phase="returned non-finite glass narrowphase";GlassInput g;g.Nan();g.Run();throw std::runtime_error("Old NaN query unexpectedly returned");}
 if(mode=="old-startup"){
  phase="returned Test29 quarter-unit CPU renderer startup";
  StartupReadWrapper()(image+kRenderFrameBaseRva,0x8000);
  throw std::runtime_error("Old quarter-unit renderer unexpectedly returned");
 }
 const bool fixed=mode=="fixed";
 phase="matching native GPU and CPU allocation requests";TestNativeCreation(fixed?kSkinCacheBytes:0x480000);
 Guarded scene0(kCrowdSurfaceBytes),scene1(kCrowdSurfaceBytes),reverse0(kSurfaceReverseBytes),reverse1(kSurfaceReverseBytes),table0(kSurfaceTableBytes),table1(kSurfaceTableBytes);
 auto* state=reinterpret_cast<CrowdResourceState*>(image+Cells);state->frames[0].surfaces=std::uintptr_t(scene0.data);state->frames[1].surfaces=std::uintptr_t(scene1.data);
 const auto gpu=GpuWrapper(),cpu=CpuWrapper();const auto surface=SurfaceWrapper();const auto reset=ResetWrapper();
 auto* frame0=image+kRenderFrameBaseRva;auto* frame1=frame0+kRenderFrameStride;
 std::array<std::array<std::uint32_t,8>,2> gpuMeta{};
 state->secondFrameLow[0]=std::uint32_t(std::uintptr_t(frame1));
 for(unsigned n=0;n<2;++n){auto* frame=n?frame1:frame0;Put(frame,0x419c0,std::uintptr_t(gpuMeta[n].data()));Put(frame,0x419d0,reservedCpu[n]);
  state->frames[n].reverse=std::uintptr_t(n?reverse1.data:reverse0.data);state->frames[n].table=std::uintptr_t(n?table1.data:table0.data);state->framePointers[n]=std::uintptr_t(&state->frames[n]);
  auto* table=reinterpret_cast<std::uintptr_t*>(state->frames[n].table);for(unsigned id=0;id<kSurfaceHandles;++id)table[id]=std::uintptr_t(frame+0x42980+id*4);
 }
 auto choose=[&](unsigned n){Put(image,0x410fd00,std::uintptr_t(n?frame1:frame0));};
 if(!fixed){
  phase="stock skin-cache and scene-record exhaustion";choose(0);unsigned skinCount=0,surfaceCount=0;
  while(gpu(4800)!=0xffffffffu)++skinCount;
  while(surface(64*48))++surfaceCount;
  Check(skinCount==30 && surfaceCount==42,"Stock renderer limits did not reproduce");
  Check(gpuMeta[0][0]>0x480000,"Stock failed allocation should expose counter overshoot");
  std::cout<<"{\"stock_skin_models_at_4800_vertices\":"<<skinCount<<",\"stock_scene_models_at_64_surfaces\":"<<surfaceCount<<",\"failed_native_reservation_overshoots\":true}\n";return 0;
 }
 phase="256 actors plus 64 corpses across two native frame slots";unsigned modelCases=0;SetPublicationContinuations();
 for(unsigned tick=0;tick<12;++tick){unsigned n=tick&1;choose(n);auto* frame=n?frame1:frame0;
  // Resets occur only at native frame reuse, after worker synchronization.
  reset(frame);gpuMeta[n][0]=0;Put(frame,0x419d8,0u);Check(state->handleCounts[n]==0,"Frame reuse retains a stale handle generation");
  for(unsigned m=0;m<320;++m){auto* s=static_cast<std::uint8_t*>(surface(64*48));Check(s==(n?scene1.data:scene0.data)+m*64*48,"Animated scene record aliases or skips storage");std::memset(s,0,64*48);s[4]=std::uint8_t(m);s[64*48-1]=std::uint8_t(m+1);
   std::array<std::uint8_t,0x100> object{};Put(object.data(),0x48,std::uintptr_t(s));Check(crowd_publish(s,64,64*48,object.data())==1,"Valid animated records were not mapped before publication");
   Check(gpu(4800)==m*4800*32 && cpu(4800)==m*4800*32,"Skin offsets overlap or differ between CPU and GPU");++modelCases;}
  for(unsigned m=0;m<320;++m)Check((n?scene1.data:scene0.data)[m*64*48+4]==std::uint8_t(m),"A later character damaged an earlier record");
  Check(state->handleCounts[n]==320*64,"Frame handles were lost or reused during publication");
  Check(Get<unsigned>(frame,0x41994)==0,"Animated crowd consumed the rigid packed-offset pool");
 }
 phase="bounded exhaustion and arithmetic-overflow requests";choose(0);SurfaceWrapper();
 auto bounded=[&](auto reserve,unsigned* used,unsigned cap,unsigned unit){
  *used=cap-32;Check(reserve(32/unit)!=0xffffffffu && *used==cap,"Last safe skin allocation rejected");auto before=*used;Check(reserve(1)==0xffffffffu && *used==before,"Rejected skin reservation advanced the counter");
  *used=0xfffffff0u;Check(reserve(16)==0xffffffffu && *used==0xfffffff0u,"Skin integer overflow was accepted");
  *used=0;for(unsigned vertices:{0x08000000u,0x80000000u,0xffffffffu})Check(reserve(vertices)==0xffffffffu && *used==0,"Vertex-to-byte multiplication wrapped before the capacity check");
 };
 bounded(gpu,&gpuMeta[0][0],kSkinCacheBytes,32);auto* cpuUsed=reinterpret_cast<unsigned*>(frame0+0x419d8);bounded(cpu,cpuUsed,kSkinCacheBytes,32);
 state->frames[0].used=kCrowdSurfaceBytes-48;Check(surface(48)==scene0.data+kCrowdSurfaceBytes-48,"Last safe surface record rejected");auto before=state->frames[0].used;Check(!surface(4) && state->frames[0].used==before,"Surface exhaustion advances count or crosses guard");
 state->frames[0].used=0xfffffff0u;Check(!surface(32) && state->frames[0].used==0xfffffff0u,"Surface arithmetic overflow accepted");
 choose(0);Put(image,0x410fd00,std::uintptr_t(frame0+4));Check(!surface(48) && state->badFrames==1,"Unknown frame used an arbitrary surface slot");
 phase="concurrent native reservations";choose(0);gpuMeta[0][0]=0;*cpuUsed=0;state->frames[0].used=0;
 std::mutex mutex;std::set<unsigned> offsets;std::atomic<bool> okay{true};std::vector<std::thread> workers;
 for(unsigned t=0;t<8;++t)workers.emplace_back([&]{for(unsigned n=0;n<1000;++n){auto a=gpu(4),b=cpu(4);auto* s=surface(128);if(a==0xffffffffu || b==0xffffffffu || !s){okay=false;continue;}std::lock_guard<std::mutex> lock(mutex);if(!offsets.insert(a).second)okay=false;}});
 for(auto& t:workers)t.join();Check(okay && offsets.size()==8000 && gpuMeta[0][0]==8000*128 && *cpuUsed==8000*128 && state->frames[0].used==8000*128,"Concurrent reservations overlap or lose a slot");
 phase="concurrent record handle publication";state->frames[0].used=0;state->handleCounts[0]=0;SetPublicationContinuations();offsets.clear();workers.clear();okay=true;
 for(unsigned t=0;t<8;++t)workers.emplace_back([&]{std::array<std::uint8_t,0x100> object{};for(unsigned n=0;n<1000;++n){auto* s=static_cast<std::uint8_t*>(surface(48));if(!s){okay=false;continue;}std::memset(s,0,48);Put(object.data(),0x48,std::uintptr_t(s));if(!crowd_publish(s,1,48,object.data())){okay=false;continue;}
   auto id=Get<std::uint16_t>(reinterpret_cast<void*>(state->frames[0].reverse),(std::uintptr_t(s)-state->frames[0].surfaces)/2);auto* table=reinterpret_cast<std::uintptr_t*>(state->frames[0].table);if(table[0x8000+id]!=std::uintptr_t(s))okay=false;std::lock_guard<std::mutex> lock(mutex);if(!offsets.insert(id).second)okay=false;}});
 for(auto& t:workers)t.join();Check(okay && offsets.size()==8000 && state->handleCounts[0]==8000,"Concurrent animated handles overlap, disappear or alias another record");
 phase="handle and record-format boundaries";std::array<std::uint8_t,0x100> object{};auto publish=[&](void* record,unsigned count,unsigned size){Put(object.data(),0x48,std::uintptr_t(record));auto okay=crowd_publish(record,count,size,object.data());if(!okay)Check(!Get<std::uintptr_t>(object.data(),0x48),"Failed record mapping left a drawable pointer");return okay;};
 auto* last=surface(48);std::memset(last,0,48);state->handleCounts[0]=0x7fff;Check(publish(last,1,48) && state->handleCounts[0]==0x8000,"Last 16-bit animated handle was rejected");
 Check(!publish(last,1,48) && state->handleCounts[0]==0x8000,"Handle exhaustion crossed its pointer table");
 state->handleCounts[0]=0xffffffffu;Check(!publish(last,1,48) && state->handleCounts[0]==0xffffffffu,"Handle count arithmetic overflow accepted");state->handleCounts[0]=0;
 Check(!publish(last,1,4),"Truncated positive record was mapped");Check(!publish(last,2,48),"Record count mismatch was mapped");
 Put(last,0,std::int32_t(-259));Check(!publish(last,1,48),"Oversized bone record was mapped");Put(last,0,std::int32_t(0x80000000u));Check(!publish(last,1,48),"Overflowing record marker was mapped");
 Check(!publish(scene0.data+kCrowdSurfaceBytes+4,1,4),"Outside-pool record was read before range validation");
 auto* mixed=static_cast<std::uint8_t*>(surface(236));std::memset(mixed,0,236);Put(mixed,0,std::int32_t(-3));Put(mixed,4,0u);Put(mixed,52,std::int32_t(-4));Put(mixed,128,std::int32_t(-5));state->handleCounts[0]=0;
 Check(publish(mixed,4,236),"Valid empty/GPU/CPU mixed record sequence was rejected");
 auto* table=reinterpret_cast<std::uintptr_t*>(state->frames[0].table);for(unsigned n=0;n<4;++n)Check(table[0x8000+n]==std::uintptr_t(mixed+std::array<unsigned,4>{0,4,52,128}[n]),"Variable-width record lost its start address");
 phase="native packed producer and every field reader equivalence";Check(argc==4,"Original native image is required for semantic comparison");
 std::ifstream referenceFile(argv[3],std::ios::binary);std::vector<char> referenceBytes((std::istreambuf_iterator<char>(referenceFile)),{});Check(referenceBytes.size()==0x5800000,"Invalid original oracle image");
 auto* reference=static_cast<std::uint8_t*>(mmap(nullptr,referenceBytes.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(reference!=MAP_FAILED,"Oracle mapping failed");std::memcpy(reference,referenceBytes.data(),referenceBytes.size());referenceBytes.clear();referenceBytes.shrink_to_fit();
 const auto bridgeCasesPassed=BridgeEquivalence(reference,*state);munmap(reference,0x5800000);
 phase="first and subsequent animated CPU renderer record reads";
 const auto startupCasesPassed=StartupReadCases(*state);
 phase="all warning categories counted without native throttle";Bytes(0x2acf9,{0xc3});auto warning=reinterpret_cast<void(ABI*)(unsigned)>(image+0x2acf0);
 for(unsigned n=0;n<64;++n){warning(n);warning(n);}warning(64);warning(0xffffffffu);for(auto count:state->warnings)Check(count==2,"Warning counter dropped, overflowed or aliased a category");
 // Restore continuations altered by the allocation wrappers before physics.
 phase="returned non-finite glass query guarded before triangle selection";GlassInput g;g.Nan();g.Run();Check(state->invalidGlassQueries==1 && state->queryClaim==2 && state->queryStage==2,"Returned invalid descriptor was not captured and skipped");
 auto snapshot=state->query;
 unsigned invalidCases=0;
 std::vector<unsigned> numeric;for(unsigned at=0x18;at<=0x88;at+=4)if(at!=0x34)numeric.push_back(at);for(unsigned at=0xa0;at<=0xa8;at+=4)numeric.push_back(at);
 for(auto at:numeric)for(unsigned bits:{0x7fc00000u,0xffc00000u,0x7f800000u,0xff800000u}){GlassInput input;Put(input.query.data(),at,bits);input.Run();++invalidCases;}
 Check(state->invalidGlassQueries==invalidCases+1 && state->query==snapshot,"Guard lost a non-finite component or overwrote the first failure");
 phase="feature-selector underflow and upper bound";
 Jump(0x1dd870,std::uintptr_t(&SelectFeature));RelativeJump(0x1dcf66,0x1dd183);
 std::array<float,4> plane{0,0,1,0};std::array<float,9> triangle{-4,-4,0,4,-4,0,0,4,0};
 std::array<std::uint8_t,16> polygon{};Put(polygon.data(),0,std::uintptr_t(triangle.data()));Put(polygon.data(),8,3u);
 GlassInput finite;Put(finite.query.data(),4,3u);Put(finite.query.data(),0x40,1.f);
 using Collision=void(ABI*)(const void*,const void*,const void*,unsigned,void*);
 auto collision=reinterpret_cast<Collision>(image+0x1dcea0);
 for(unsigned n=1;n<=14;++n){selectedFeature=n;collision(plane.data(),polygon.data(),finite.query.data(),0,finite.output.data());}
 Check(state->invalidFeatures==0,"A valid polygon feature was rejected");
 for(unsigned n:{0u,15u,0x7fffffffu,0xffffffffu}){selectedFeature=n;collision(plane.data(),polygon.data(),finite.query.data(),0,finite.output.data());}
 Check(state->invalidFeatures==4,"A zero or out-of-range polygon feature reached vertex arithmetic");
 phase="invalid glass pose rejected before world traversal";const auto early=EarlyPoseWrapper();
 std::array<float,6> bounds{0,0,0,4,4,4};GlassInput pose;
 Check(early(pose.query.data(),bounds.data())==1,"Finite glass pose did not reach native world entry");
 unsigned poseCases=0;
 for(unsigned at=0x38;at<=0x64;at+=4)for(unsigned bits:{0x7fc00000u,0xffc00000u,0x7f800000u,0xff800000u}){
  GlassInput bad;Put(bad.query.data(),at,bits);Check(early(bad.query.data(),bounds.data())==0,"Bad native glass pose reached map traversal");++poseCases;
 }
 for(unsigned at=0;at<6;++at)for(unsigned bits:{0x7fc00000u,0xffc00000u,0x7f800000u,0xff800000u}){
  auto bad=bounds;Put(bad.data(),at*4,bits);Check(early(pose.query.data(),bad.data())==0,"Non-finite bounds reached map traversal");++poseCases;
 }
 GlassInput other;Put(other.query.data(),0x34,5u);Put(other.query.data(),0x38,0x7fc00000u);
 Check(early(other.query.data(),bounds.data())==1,"Glass-only early gate changed another geometry type");
 state->queryClaim=0;GlassInput first;Put(first.query.data(),0x38,0x7fc00000u);Check(!early(first.query.data(),bounds.data()) && state->queryClaim==2 && state->queryStage==1 && state->firstQuery==0x1234,"First bad native geometry was not retained at the early stage");++poseCases;
 // A finite descriptor must continue at the original instruction with its
 // input arguments intact. Avoid world contacts for this gate-only check.
 phase="finite collision descriptors pass the same native continuation";
 Bytes(0x1dfaa4,{0x48,0x81,0xc4,0xf0,0x00,0x00,0x00,0x5b,0xc3});
 for(unsigned n=0;n<1000;++n){GlassInput input;Put(input.query.data(),0x38,float(n));input.Run();}
 Check(state->invalidGlassQueries==invalidCases+1+poseCases,"Finite geometry was rejected");
 std::cout<<"{\"native_gpu_create_requests\":2,\"native_cpu_reservations\":2,\"skin_cache_bytes\":"<<kSkinCacheBytes<<",\"skinned_surface_bytes_per_frame\":"<<kCrowdSurfaceBytes<<",\"native_frame_slots\":2,\"actor_and_corpse_models_per_frame\":320,\"model_allocation_cases\":"<<modelCases<<",\"concurrent_reservations\":24000,\"concurrent_handle_publications\":8000,\"native_packed_record_equivalence_cases\":"<<bridgeCasesPassed<<",\"native_gpr_xmm_flags_compared\":true,\"quarter_unit_renderer_read_cases\":"<<startupCasesPassed<<",\"failure_counters_remain_bounded\":true,\"warning_categories\":64,\"nonfinite_query_cases\":"<<invalidCases+1<<",\"early_pose_cases\":"<<poseCases<<",\"valid_collision_features\":14,\"invalid_collision_features_rejected\":4,\"finite_query_pass_cases\":1000,\"first_invalid_descriptor_retained\":true,\"rigid_packed_pool_untouched\":true,\"gpu_peak_bytes\":"<<state->gpuPeak<<",\"cpu_peak_bytes\":"<<state->cpuPeak<<",\"windows_gameplay_tested\":false}\n";
}catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 1;}}
