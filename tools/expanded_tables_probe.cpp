// Execute the exact native compass entity update (0x92c00), compass writer
// (0x92580), anim-info pool init (0x29ce80) and allocator (0x29a790).
// Engine callbacks outside these routines are MS-ABI substitutes.
#include "crowd_resources.hpp"
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <csetjmp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#define ABI __attribute__((ms_abi))
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";
// Since Test21 the client entity->actor map is 16-bit in the arena (0x4580000,
// fixture +0x1000000); kind bytes follow at +0x4304, indexed by slot.
constexpr std::uintptr_t Cells=0x57e0000,Table=0x3fdf50,TableEnd=Table+42*0x2c,Entities=0x441070,SlotMap=0x5580000,Clock=0x51dca8;
constexpr std::uintptr_t OldPool=0x1bc0a30,NewPool=kAnimInfoRva+0x1000000;
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Hook(unsigned at,std::uintptr_t target){auto jump=mod::AbsoluteJump(target);std::memcpy(image+at,jump.data(),jump.size());}
std::uint8_t* Load(const char* name){
 std::ifstream f(name,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==0x5800000,"Invalid native image");
 auto* data=static_cast<std::uint8_t*>(mmap(nullptr,bytes.size(),7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(data!=MAP_FAILED,"Image mapping failed");std::memcpy(data,bytes.data(),bytes.size());
 const auto optional=Get<unsigned>(data,0x3c)+24;const auto preferred=Get<std::uint64_t>(data,optional+24);
 auto at=Get<unsigned>(data,optional+112+5*8);const auto end=at+Get<unsigned>(data,optional+112+5*8+4);const auto delta=std::uintptr_t(data)-preferred;
 while(at<end){const auto page=Get<unsigned>(data,at),length=Get<unsigned>(data,at+4);Check(length>=8 && at+length<=end,"Bad relocation block");for(unsigned n=8;n<length;n+=2){auto entry=Get<std::uint16_t>(data,at+n);if(!entry)continue;Check(entry>>12==10,"Unexpected relocation kind");const auto target=page+(entry&4095);Put(data,target,Get<std::uint64_t>(data,target)+delta);}at+=length;}
 return data;
}
bool ABI NotVisible(int,int){return false;}
void ABI Nothing(){}
std::jmp_buf errorJump;const char* engineError=nullptr;
[[noreturn]] void ABI EngineError(int,const char* text){engineError=text;std::longjmp(errorJump,1);}
void Install(){Hook(0xa9740,std::uintptr_t(&NotVisible));Hook(0x21b2d0,std::uintptr_t(&Nothing));Hook(0x1fb5f0,std::uintptr_t(&EngineError));}
CrowdResourceState& State(){return *reinterpret_cast<CrowdResourceState*>(image+Cells);}
// One client actor entity with a chosen client actor slot.
void Actor(unsigned entity,unsigned slot){
 auto* cent=image+Entities+entity*0x1a8;std::memset(cent,0,0x1a8);cent[0xe0]=0xd;Put(cent,0x15e,std::uint16_t(entity));
 Put(cent,0x18,1.5f);Put(cent,0x1c,2.5f);Put(cent,0x20,3.5f);Put(cent,0x28,7u);
 Put(image+SlotMap,4+entity*2,std::uint16_t(slot));image[SlotMap+0x4304+slot]=0;
}
void Update(unsigned entity){reinterpret_cast<void(ABI*)(int)>(image+0x92c00)(int(entity));}
void Write(unsigned entity){float origin[3]{4,5,6};reinterpret_cast<void(ABI*)(void*,void*,int)>(image+0x92580)(image+Entities+entity*0x1a8,origin,100);}
std::vector<std::uint8_t> Region(std::uintptr_t a,std::uintptr_t z){return {image+a,image+z};}
// Anim-info allocation: tree header {anims*, root WORD}; anims[1] read by native code.
struct Tree {void* anims;std::uint16_t root,pad[3];};
unsigned Allocate(unsigned limit){
 std::vector<std::uint64_t> anims(8,0x1234);Tree tree{anims.data(),0,{}};unsigned made=0;
 engineError=nullptr;
 if(setjmp(errorJump)==0){for(;made<limit;++made)reinterpret_cast<unsigned(ABI*)(void*,unsigned,unsigned,unsigned,int)>(image+0x29a790)(&tree,0,0,0,0);}
 return made;
}
// Queue leaf anim-info record 1 for server->client transfer until the native
// per-snapshot budget raises its engine error.
unsigned Transfer(unsigned limit){
 reinterpret_cast<void(ABI*)()>(image+0x29a110)();unsigned queued=0;engineError=nullptr;std::uint64_t tree=0;
 if(setjmp(errorJump)==0){for(;queued<limit;++queued)reinterpret_cast<void(ABI*)(void*,unsigned)>(image+0x29ebb0)(&tree,1);}
 return queued;
}
}
int main(int argc,char** argv){if(argc<3)return 2;rlimit none{0,0};setrlimit(RLIMIT_CORE,&none);try{
 image=Load(argv[1]);Install();const std::string mode=argv[2];
 *reinterpret_cast<int*>(image+Clock)=5000;
 if(mode=="old"){
  phase="stock compass actor slots";
  const auto before=Region(TableEnd,0x400400);
  Actor(1200,62);Update(1200);Actor(1201,100);Update(1201);Write(1201);
  const auto after=Region(TableEnd,0x400400);unsigned changed=0;for(std::size_t i=0;i<before.size();++i)changed+=before[i]!=after[i];
  const bool command=Get<unsigned>(image,0x3fea00)==1200u || Get<std::uint32_t>(image,0x3fea00)!=0;
  const bool glass=Get<unsigned>(image,Table+100*0x2c)==1201u && Table+100*0x2c>=0x3fef50;
  phase="stock anim-info capacity";
  reinterpret_cast<void(ABI*)()>(image+0x29ce80)();const auto made=Allocate(20000);const auto madeError=engineError;
  reinterpret_cast<void(ABI*)()>(image+0x29ce80)();const auto queued=Transfer(40000);
  std::cout<<"{\"anim_transfers_before_error\":"<<queued<<",\"transfer_error\":\""<<(engineError?engineError+1:"")<<"\","<<"\"bytes_written_past_compass_table\":"<<changed<<",\"actor_62_hits_command_entries\":"<<(command?"true":"false")
           <<",\"actor_100_hits_glass_place_states\":"<<(glass?"true":"false")<<",\"anim_infos_before_error\":"<<made
           <<",\"anim_info_error\":\""<<(madeError?madeError+1:"")<<"\"}\n";
  return changed && glass && made==4095 && madeError && queued==2048 && engineError?0:1;
 }
 if(mode!="fixed")return 2;
 phase="compass actor slots below 32";
 unsigned owned=0;
 for(unsigned slot:{0u,5u,31u}){Actor(1100+slot,slot);Update(1100+slot);
  auto* r=image+Table+slot*0x2c;Check(Get<unsigned>(r,0)==1100+slot && Get<int>(r,4)==5000 && Get<float>(r,8)==1.5f && Get<float>(r,0x10)==3.5f,"Native compass record differs");
  Write(1100+slot);Check(Get<int>(r,0x24)==5100,"Native compass writer differs");++owned;}
 phase="compass expanded actors";
 const auto before=Region(TableEnd,0x400400);const auto rejectedBefore=State().compassRejected;
 for(unsigned slot:{32u,33u,51u,62u,93u,100u,209u,255u}){Actor(1300+slot,slot);Update(1300+slot);Write(1300+slot);}
 Check(Region(TableEnd,0x400400)==before,"Expanded actor reached memory past the compass table");
 Check(State().compassRejected-rejectedBefore==16,"Compass rejection counter differs");Check(State().compassWriterSkips==8,"Compass writer skip counter differs");
 phase="expanded anim-info pool";
 reinterpret_cast<void(ABI*)()>(image+0x29ce80)();
 unsigned linked=0;for(std::uint16_t n=Get<std::uint16_t>(image+NewPool,0xa);n;n=Get<std::uint16_t>(image+NewPool+n*64,0xa)){
  Check(n<kAnimInfoRecords,"Free list escaped the pool");Check(Get<std::uint16_t>(image+NewPool+Get<std::uint16_t>(image+NewPool+n*64,0xa)*64,8)==n,"Free list back link differs");++linked;}
 Check(linked==kAnimInfoRecords-1,"Free list does not cover the expanded pool");
 std::vector<std::uint8_t> zero(0x40000,0);Check(std::memcmp(image+OldPool,zero.data(),zero.size())==0,"Old static pool was used");
 const auto made=Allocate(20000);
 Check(made==kAnimInfoRecords-1 && engineError,"Expanded anim-info capacity differs");
 phase="expanded anim-transfer budget";
 reinterpret_cast<void(ABI*)()>(image+0x29ce80)();const auto queued=Transfer(40000);
 Check(queued==kAnimTransferRecords && engineError,"Expanded anim-transfer capacity differs");
 std::vector<std::uint8_t> zeroTransfer(0x800*0x1c,0);Check(std::memcmp(image+0x1c01230,zeroTransfer.data(),zeroTransfer.size())==0,"Old static transfer array was used");
 std::cout<<"{\"compass_owned_slot_cases\":"<<owned<<",\"compass_expanded_actor_cases\":8,\"compass_rejections\":"<<State().compassRejected
          <<",\"compass_unfound_writes_skipped\":"<<State().compassWriterSkips<<",\"anim_info_free_list_records\":"<<linked
          <<",\"anim_infos_before_error\":"<<made<<",\"anim_transfers_before_error\":"<<queued<<",\"old_static_pool_untouched\":true,\"old_static_transfer_array_untouched\":true}\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"TABLES_CHECK phase="<<phase<<" "<<e.what()<<"\n";return 1;}}
