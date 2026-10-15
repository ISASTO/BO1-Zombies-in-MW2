// Execute the exact native glass shatter lookup (0x100870), server glass
// damage entry (0x16a060) and guarded release (0x100ed0) on Linux.
// Fracture, effects, state publication and release tail calls are recorded
// by MS-ABI substitutes; no world, physics or renderer state is involved.
#include "crowd_resources.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";
constexpr std::uintptr_t Cells=0x57e0000,Glass=0xcdb118,ServerGlass=0xcf75a8,Weaken=0xcf75b0,Destroy=0xcf75b8;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c){auto* u=static_cast<ucontext_t*>(c);char b[256];int n=std::snprintf(b,sizeof(b),"GLASS_STATE_FAULT phase=%s rva=0x%llx address=%p\n",phase,(unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr);if(n>0){auto ignored=write(2,b,std::min(n,255));(void)ignored;}_exit(71);}
void Hook(unsigned at,std::uintptr_t target){auto jump=mod::AbsoluteJump(target);std::memcpy(image+at,jump.data(),jump.size());}
std::uint8_t* Load(const char* name){
 std::ifstream f(name,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Invalid native image");
 auto* data=static_cast<std::uint8_t*>(mmap(nullptr,bytes.size(),7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Image mapping failed");std::memcpy(data,bytes.data(),bytes.size());
 const auto optional=Get<unsigned>(data,0x3c)+24;const auto preferred=Get<std::uint64_t>(data,optional+24);
 auto at=Get<unsigned>(data,optional+112+5*8);const auto end=at+Get<unsigned>(data,optional+112+5*8+4);const auto delta=std::uintptr_t(data)-preferred;
 while(at<end){const auto page=Get<unsigned>(data,at),length=Get<unsigned>(data,at+4);Check(length>=8 && at+length<=end,"Bad relocation block");for(unsigned n=8;n<length;n+=2){auto entry=Get<std::uint16_t>(data,at+n);if(!entry)continue;Check(entry>>12==10,"Unexpected relocation kind");const auto target=page+(entry&4095);Put(data,target,Get<std::uint64_t>(data,target)+delta);}at+=length;}
 return data;
}
// Recorded native substitutes.
unsigned fractures=0,lastFracturePiece=0,stateChanges=0,lastChangedPlace=0,releases=0,lastReleasedPiece=0;
void ABI Effects(void*){}
std::uint32_t ABI Fracture(void* glass,unsigned piece,void*,void*){Check(glass==image+Glass,"Fracture received another glass system");++fractures;lastFracturePiece=piece;return 0;}
void ABI StateChange(unsigned place,void*,void*){++stateChanges;lastChangedPlace=place;}
void ABI ReleaseTail(void* glass,unsigned piece){Check(glass==image+Glass,"Release tail received another glass system");++releases;lastReleasedPiece=piece;}
void Install(){
 Hook(0x1013c0,std::uintptr_t(&Effects));Hook(0x103a80,std::uintptr_t(&Fracture));
 Hook(0x16a380,std::uintptr_t(&StateChange));Hook(0x10b4f0,std::uintptr_t(&ReleaseTail));
}
// Client glass: piece states (32 bytes, place id at +0xc), occupancy bitmap.
struct ClientGlass {
 std::vector<std::uint8_t> states,places,pieceGeometry;std::vector<std::uint32_t> bitmap;unsigned pieces;
 explicit ClientGlass(unsigned count):states(count*32),places(count*32),pieceGeometry(count*48),bitmap((count+31)/32),pieces(count){
  std::memset(image+Glass,0,0xa8);Put(image+Glass,0xc,count);Put(image+Glass,0x10,unsigned(bitmap.size()));
  Put(image+Glass,0x20,0xffffu);Put(image+Glass,0x38,std::uintptr_t(places.data()));Put(image+Glass,0x40,std::uintptr_t(states.data()));
  Put(image+Glass,0x48,std::uintptr_t(pieceGeometry.data()));Put(image+Glass,0x58,std::uintptr_t(bitmap.data()));
 }
 void Activate(unsigned piece,unsigned place){bitmap[piece/32]|=0x80000000u>>(piece%32);Put(states.data(),piece*32+0xc,std::uint16_t(place));Put(image+Glass,0x1c,Get<unsigned>(image+Glass,0x1c)+1);}
};
// Leave a recognizable value where 0x100870 keeps its never-initialized index.
__attribute__((noinline)) void Dirty(){volatile std::uint16_t junk[4096];for(auto& v:junk)v=0x7ff7;}
__attribute__((noinline)) void Shatter(unsigned place){
 std::array<std::uint8_t,64> a{},b{},c{};
 Dirty();reinterpret_cast<void(ABI*)(unsigned,void*,void*,void*)>(image+0x100870)(place,a.data(),b.data(),c.data());
}
struct ServerPlaces {
 std::uint8_t* mapping;std::uint8_t* records;std::array<std::uint8_t,0x40> header{};std::array<std::uint8_t,0x20> weaken{},destroy{};unsigned count;
 explicit ServerPlaces(unsigned places):count(places){
  mapping=static_cast<std::uint8_t*>(mmap(nullptr,8192,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(mapping!=MAP_FAILED,"Record mapping failed");
  Check(mprotect(mapping+4096,4096,PROT_NONE)==0,"Record guard failed");records=mapping+4096-places*12;
  Put(header.data(),0,std::uintptr_t(records));Put(header.data(),8,places);
  Put(weaken.data(),0x10,std::uint16_t(50));Put(destroy.data(),0x10,std::uint16_t(100));
  Put(image+ServerGlass,0,std::uintptr_t(header.data()));Put(image+Weaken,0,std::uintptr_t(weaken.data()));Put(image+Destroy,0,std::uintptr_t(destroy.data()));
 }
 ~ServerPlaces(){munmap(mapping,8192);}
};
__attribute__((noinline)) void Release(unsigned piece){reinterpret_cast<void(ABI*)(void*,unsigned)>(image+0x100ed0)(image+Glass,piece);}
__attribute__((noinline)) void Damage(unsigned place,unsigned amount){std::array<std::uint8_t,64> a{},b{};reinterpret_cast<void(ABI*)(unsigned,unsigned,void*,void*)>(image+0x16a060)(place,amount,a.data(),b.data());}
CrowdResourceState& State(){return *reinterpret_cast<CrowdResourceState*>(image+Cells);}
}
int main(int argc,char** argv){if(argc<3)return 2;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 image=Load(argv[1]);Install();const std::string mode=argv[2];
 if(mode=="old-missing"){
  // Returned Test31 path: shatter a place that owns no active piece.
  phase="stock shatter with no owning piece";ClientGlass glass(64);glass.Activate(0,5);Shatter(9);
  std::cout<<"{\"fracture_calls\":"<<fractures<<",\"fractured_piece\":"<<lastFracturePiece<<"}\n";
  return fractures==1?0:1;
 }
 if(mode=="old-damage"){phase="stock out-of-range server place damage";ServerPlaces server(27);Damage(27,10);throw std::runtime_error("Out-of-range damage unexpectedly bounded");}
 if(mode!="fixed")return 2;
 unsigned compared=0;
 phase="owned shatter compatibility";
 for(unsigned pieces:{1u,32u,33u,64u,1051u})for(unsigned owner:{0u,pieces/2,pieces-1}){
  ClientGlass glass(pieces);glass.Activate(owner,7);fractures=0;Shatter(7);
  Check(fractures==1 && lastFracturePiece==owner,"Owned piece was not fractured");++compared;
 }
 phase="missing-piece shatter";
 const auto missingBefore=State().glassMissingPlaces;
 for(unsigned place:{9u,10u,27u,1023u}){ClientGlass glass(64);glass.Activate(0,5);fractures=0;Shatter(place);Check(fractures==0,"Missing piece still reached native fracture");Check(State().glassLastMissingPlace==place,"Missing place not recorded");}
 {ClientGlass glass(64);fractures=0;Shatter(3);Check(fractures==0,"Empty glass reached native fracture");}
 Check(State().glassMissingPlaces-missingBefore==5,"Missing-place counter differs");
 phase="server damage compatibility";
 unsigned changes=0;
 {
  ServerPlaces server(27);
  for(unsigned place=0;place<27;++place){
   stateChanges=0;Damage(place,40);Check(stateChanges==0,"Sub-threshold damage changed state");
   Damage(place,20);Check(stateChanges==1 && lastChangedPlace==place,"Weaken threshold not published");
   Damage(place,60);Check(stateChanges==2,"Destroy threshold not published");
   Check(Get<std::uint16_t>(server.records,place*12)==120,"Native place damage accumulation differs");changes+=stateChanges;
  }
  phase="out-of-range server damage";
  const auto rejectedBefore=State().glassRejectedDamage;
  for(unsigned place:{27u,28u,1050u,0xffffu,0xffffffffu}){stateChanges=0;Damage(place,500);Check(stateChanges==0,"Rejected damage changed state");Check(State().glassLastRejectedPlace==place,"Rejected place not recorded");}
  Check(State().glassRejectedDamage-rejectedBefore==5,"Rejected-damage counter differs");
  const auto caller=State().glassLastRejectedCaller;
  Check(caller>=std::uintptr_t(&Damage) && caller<std::uintptr_t(&Damage)+0x400,"Rejected-damage caller is not the native call site");
 }
 phase="ignored glass release ring";
 {
  ClientGlass glass(64);glass.Activate(4,4);const auto before=State().glassFreeRingNext;releases=0;
  for(unsigned piece:{11u,12u,64u,4000u}){Release(piece);Check(releases && lastReleasedPiece==piece,"Native release tail lost its piece");}
  Check(State().glassFreeRingNext-before==4,"Ignored-release ring count differs");
  for(unsigned n=0;n<4;++n){const auto& r=State().glassFreeRing[(before+n)%kGlassFreeRingEntries];Check(r.piece==(n==0?11u:n==1?12u:n==2?64u:4000u),"Ignored-release piece differs");Check(r.caller>=std::uintptr_t(&Release) && r.caller<std::uintptr_t(&Release)+0x100,"Ignored-release caller is not the native call site");}
  releases=0;Release(4);
  Check(releases==1 && !(glass.bitmap[0]&(0x80000000u>>4)),"Active piece release changed");Check(State().glassFreeRingNext-before==4,"Valid release was recorded as ignored");
 }
 std::cout<<"{\"owned_shatter_cases\":"<<compared<<",\"missing_piece_cases\":5,\"server_damage_places\":27,\"published_state_changes\":"<<changes
          <<",\"rejected_damage_cases\":5,\"ignored_release_records\":4,\"missing_place_counter\":"<<State().glassMissingPlaces
          <<",\"rejected_damage_counter\":"<<State().glassRejectedDamage<<"}\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"GLASS_STATE_CHECK phase="<<phase<<" "<<e.what()<<"\n";return 1;}}
