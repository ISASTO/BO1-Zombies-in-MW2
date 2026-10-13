// Exact native actor/animation/snapshot and checkpoint code, MS-ABI on Linux.
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <stdexcept>
#include <cstdio>
#include <cstdarg>
namespace {
using namespace mod::expansion;
std::uint8_t* image=nullptr;const char* phase="init";bool legacy=false,legacyWrite=false;
std::vector<SaveField> fields;unsigned created=0,cleared=0;
unsigned fxWrite16=0,fxWrite32=0,fxRead16=0,fxRead32=0;
constexpr std::size_t Pool=0x4000000,Sentients=0x4500000,Frame=0x4510000,ClientFrame=0x4580000,Nodes=0x45c0000;
#define ABI __attribute__((ms_abi))
template<class T>void Put(void* p,std::size_t at,T value){std::memcpy(static_cast<char*>(p)+at,&value,sizeof(value));}
template<class T>T Get(const void* p,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Fault(int,siginfo_t* info,void* c) {
 auto* u=static_cast<ucontext_t*>(c);char b[300];int n=std::snprintf(b,sizeof(b),"Fault phase=%s rva=%llx addr=%p rcx=%llx rdx=%llx r8=%llx rbx=%llx\n",phase,
 (unsigned long long)(u->uc_mcontext.gregs[REG_RIP]-(std::uintptr_t)image),info->si_addr,(unsigned long long)u->uc_mcontext.gregs[REG_RCX],(unsigned long long)u->uc_mcontext.gregs[REG_RDX],(unsigned long long)u->uc_mcontext.gregs[REG_R8],(unsigned long long)u->uc_mcontext.gregs[REG_RBX]);if(n>0){auto r=write(2,b,std::min(n,299));(void)r;}
 auto* stack=reinterpret_cast<std::uintptr_t*>(u->uc_mcontext.gregs[REG_RSP]);
 for(unsigned x=0;x<12;++x){std::fprintf(stderr,"stack[%u]=%llx delta=%llx\n",x,(unsigned long long)stack[x],(unsigned long long)(stack[x]-(std::uintptr_t)image));}
 _exit(71);
}
void Detour(std::size_t rva,std::uintptr_t to){auto j=mod::AbsoluteJump(to);std::memcpy(image+rva,j.data(),j.size());}
using Void=void(ABI*)();using One=void(ABI*)(void*);using Allocate=void*(ABI*)();using SAllocate=void*(ABI*)(int);
using Write=void(ABI*)(const SaveField*,const void*,void*,int,void*);using Read=void(ABI*)(const SaveField*,void*,int,void*);
using Select=void(ABI*)(void*,int);using Memory=void(ABI*)(void*,int,const void*);
void ABI Error(int,const char* fmt,...) {std::fprintf(stderr,"Native error %s: %s\n",phase,fmt);_exit(72);}
const char* ABI Empty(unsigned){return "";}
std::uintptr_t ABI Tree(void*,int){return reinterpret_cast<std::uintptr_t>(image+0x4700000+(created++)*128);}
std::uintptr_t ABI Dict(unsigned){return reinterpret_cast<std::uintptr_t>(image+0x45b0000);}
void ABI Clear(std::uintptr_t,int){++cleared;}
void ABI HeaderWrite(void* mem,int s){reinterpret_cast<Select>(image+0x270080)(mem,s);if(!legacyWrite){PopulationChunkHeader h;reinterpret_cast<Memory>(image+0x270c90)(mem,sizeof(h),&h);}}
void ABI HeaderRead(void* save,int s){auto select=reinterpret_cast<Select>(image+0x1af040);select(save,s);PopulationChunkHeader h;reinterpret_cast<Memory>(image+0x26feb0)(static_cast<char*>(save)+0x480,sizeof(h),&h);PopulationChunkHeader expected;legacy=h.magic!=expected.magic;if(!legacy)Check(SupportedPopulationChunk(h),"Bad expanded header");else select(save,s);Put(image,0x4590000,legacy?32:256);Put(image,0x4590004,legacy?50:274);}
void ABI WriteActor(const SaveField* original,const void* source,void* temp,int size,void* mem){if(legacyWrite){reinterpret_cast<Write>(image+0x1adfb0)(original,source,temp,size,mem);return;}std::array<char,kStride> scratch;std::memcpy(scratch.data(),source,kStride);reinterpret_cast<Write>(image+0x1adfb0)(fields.data(),source,scratch.data(),kStride,mem);}
void ABI ReadActor(const SaveField* original,void* dest,int size,void* save){auto read=reinterpret_cast<Read>(image+0x1adf50);if(!legacy){read(fields.data(),dest,kStride,save);return;}read(original,dest,size,save);std::array<char,kOldStride> stock{};std::memcpy(stock.data(),dest,0x1558);Check(MigrateLegacyActor(stock.data(),stock.size(),dest,kStride),"Legacy migration failed");}
void ABI WriteFx(const SaveField* table,const void* source,void* temp,int bytes,void* mem){if(bytes==16)++fxWrite16;else if(bytes==32)++fxWrite32;else Check(false,"Changed FX record size");reinterpret_cast<Write>(image+0x1adfb0)(table,source,temp,bytes,mem);}
void ABI ReadFx(const SaveField* table,void* dest,int bytes,void* save){if(bytes==16)++fxRead16;else if(bytes==32)++fxRead32;else Check(false,"Changed FX record size");reinterpret_cast<Read>(image+0x1adf50)(table,dest,bytes,save);}
std::vector<SaveField> Fields(std::size_t at){std::vector<SaveField> out;for(unsigned n=0;n<400;++n){out.push_back(Get<SaveField>(image,at+n*16));if(!out.back().kind)return out;}throw std::runtime_error("Invalid descriptor");}
std::size_t SaveLoad(bool stock) {
 legacyWrite=stock;fxWrite16=fxWrite32=fxRead16=fxRead32=0;
 // Change only the two writer relays in the fixture to produce stock streams.
 const auto actorRelay=0x1813dc+5+Get<std::int32_t>(image,0x1813dd);
 const auto sentientRelay=0x1814ae + 5+Get<std::int32_t>(image,0x1814af);
 Put(image,actorRelay+1,stock?32:256);Put(image,sentientRelay+1,stock?50:274);
 std::vector<char> data(0x1000000);std::array<char,128> mem{};
 Put(mem.data(),0,reinterpret_cast<std::uintptr_t>(data.data()));Put(mem.data(),8,int(data.size()));Put(mem.data(),0x14,int(-1));
 phase="native MemFile segment initialization";auto select=reinterpret_cast<Select>(image+0x270080);select(mem.data(),0);select(mem.data(),1);
 phase=stock?"stock G_Save":"expanded G_Save";reinterpret_cast<void(ABI*)(bool,void*)>(image+0x180cf0)(false,mem.data());select(mem.data(),-1);
 const auto bytes=Get<int>(mem.data(),8);Check(bytes>0 && bytes<(int)data.size(),"Save did not finish stream");
 std::memset(image+Pool,0,kActors*kStride);std::memset(image+Sentients,0,kSentients*kSentientStride);
 std::array<char,0x600> save{};std::memcpy(save.data()+0x480,mem.data(),mem.size());Put(save.data(),0x480+0x14,int(-1));Put(save.data(),0x480+0x10,int(0));
 phase=stock?"stock G_Load":"expanded G_Load";reinterpret_cast<One>(image+0x17f8e0)(save.data());
 Check(fxWrite16==32 && fxWrite32==32 && fxRead16==32 && fxRead32==32,"Unrelated FX population/record sizes changed");
 const auto actors=stock?32:kActors,sentients=stock?50:kSentients;
 for(unsigned n=0;n<actors;++n){auto* a=image+Pool+n*kStride;Check(a[0x14ff]==1 && Get<int>(a,0x1a0)==int(10000+n),"Actor restored incorrectly");for(unsigned s=0;s<sentients;++s)Check(Get<std::uintptr_t>(a,kPerceptionStart+s*48+40)==reinterpret_cast<std::uintptr_t>(image+Nodes+((n+s)%274)*0xa8),"Perception node lost");}
 for(unsigned n=0;n<sentients;++n)Check(image[Sentients+n*0x88+0x82]==1,"Sentient flag lost");
 if(stock)for(unsigned n=32;n<256;++n)Check(!image[Pool+n*kStride+0x14ff],"Legacy read retained high actors");
 return bytes;
}
}
int main(int argc,char**argv){if(argc!=2)return 2;rlimit zero{0,0};setrlimit(RLIMIT_CORE,&zero);try{
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});Check(bytes.size()>0x4800000,"Invalid image");
 image=static_cast<std::uint8_t*>(mmap(nullptr,0x5000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));Check(image!=MAP_FAILED,"mmap failed");std::memcpy(image,bytes.data(),bytes.size());bytes.clear();bytes.shrink_to_fit();
 struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
 const std::uint8_t ret=0xc3;image[0x2b9300]=ret;
 const auto copy=std::vector<std::uint8_t>{0x57,0x56,0x48,0x89,0xcf,0x48,0x89,0xd6,0x4c,0x89,0xc1,0xfc,0xf3,0xa4,0x5e,0x5f,0xc3};std::memcpy(image+0x2e3b40,copy.data(),copy.size());
 image[0x203980]=0xb8;Put(image,0x203981,int(1));image[0x203985]=0xc3;
 for(auto rva:{0x2039d0,0x204530,0x21ab50}){image[rva]=0x31;image[rva+1]=0xc0;image[rva+2]=0xc3;}
 Detour(0x1fb5f0,reinterpret_cast<std::uintptr_t>(&Error));Detour(0x21a370,reinterpret_cast<std::uintptr_t>(&Empty));
 Detour(0x20afa0,reinterpret_cast<std::uintptr_t>(&Dict));Detour(0x29bd20,reinterpret_cast<std::uintptr_t>(&Tree));Detour(0x29c440,reinterpret_cast<std::uintptr_t>(&Clear));
 const std::uintptr_t callbacks[]={reinterpret_cast<std::uintptr_t>(&HeaderWrite),reinterpret_cast<std::uintptr_t>(&HeaderRead),reinterpret_cast<std::uintptr_t>(&WriteActor),reinterpret_cast<std::uintptr_t>(&ReadActor)};for(unsigned n=0;n<4;++n)Detour(0x47f0000+n*16,callbacks[n]);
 Detour(0x47f0040,reinterpret_cast<std::uintptr_t>(&WriteFx));Detour(0x47f0050,reinterpret_cast<std::uintptr_t>(&ReadFx));
 for(auto at:{0x1811d3,0x181208}){image[at]=0xe8;Put(image,at+1,int(0x47f0040-at-5));}
 for(auto at:{0x17fe33,0x17fe50}){image[at]=0xe8;Put(image,at+1,int(0x47f0050-at-5));}
 Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(image+Pool));Put(image,0xd96e98,reinterpret_cast<std::uintptr_t>(image+Sentients));Put(image,0xd96e78,reinterpret_cast<std::uintptr_t>(image+0xd9e070));Put(image,0xd96e70,reinterpret_cast<std::uintptr_t>(image+0x45d0000));Put(image,0xd96eb0,reinterpret_cast<std::uintptr_t>(image+0x45e0000));Put(image,0xd9c4a8,reinterpret_cast<std::uintptr_t>(image+0x4550200));Put(image,0xd9c0e0,reinterpret_cast<std::uintptr_t>(image+0x4550000));Put(image,0x4550010,int(256));Put(image,0xcf7090,reinterpret_cast<std::uintptr_t>(image+Nodes));Put(image,0xf56484,int(274));Put(image,0x19ada88,reinterpret_cast<std::uintptr_t>(image+0x45b0000));
 phase="full native animation init";reinterpret_cast<Void>(image+0x170b20)();Check(created==658,"Animation init did not create 256+64+336+2 trees");
 phase="temporary tree leases";using Lease=std::uintptr_t(ABI*)();auto lease=reinterpret_cast<Lease>(image+0x12c6d0);for(unsigned n=0;n<336;++n)Check(lease()!=0,"Temporary lease ended early");Check(lease()==0,"Temporary lease crossed capacity");
 Put(image,0xd97be8,int(0));phase="animation cleanup";reinterpret_cast<Void>(image+0x1710c0)();Check(cleared==658,"Animation cleanup omitted expanded trees");
 std::memset(image+Sentients,0xa5,274*0x88);phase="sentient startup reset";reinterpret_cast<Void>(image+0x1b0900)();for(unsigned n=0;n<274;++n)Check(!image[Sentients+n*0x88+0x82],"Startup missed high sentient");std::memset(image+Sentients,0,274*0x88);
 auto sa=reinterpret_cast<SAllocate>(image+0x1b0be0);auto aa=reinterpret_cast<Allocate>(image+0x123950);
 phase="allocation";
 for(unsigned n=0;n<274;++n){auto* s=static_cast<std::uint8_t*>(sa(0));Check(s==image+Sentients+n*0x88,"Sentient alloc wrong");auto* entity=image+0xd9e070+(n+2)*0x2a0;Put(entity,0x7e,static_cast<std::uint16_t>(n+2));Put(entity,0x118,reinterpret_cast<std::uintptr_t>(s));Put(s,0,reinterpret_cast<std::uintptr_t>(entity));Put(s,8,int(2));}
 for(unsigned n=0;n<256;++n){auto* a=static_cast<std::uint8_t*>(aa());Check(a==image+Pool+n*kStride,"Actor alloc wrong");auto* e=image+0xd9e070+(n+2)*0x2a0;Put(a,0,reinterpret_cast<std::uintptr_t>(e));Put(a,8,reinterpret_cast<std::uintptr_t>(image+Sentients+n*0x88));Put(e,0x110,reinterpret_cast<std::uintptr_t>(a));Put(a,0x1a0,int(10000+n));Put(a,0x19c,std::uint8_t(1));Put(a,0x14f0,int(1));Put(a,0x13c8,reinterpret_cast<std::uintptr_t>(image+Pool+((n+1)%256)*kStride));Put(a,0xcb0,reinterpret_cast<std::uintptr_t>(image+Sentients+273*0x88));for(unsigned s=0;s<274;++s)Put(a,kPerceptionStart+s*48+40,reinterpret_cast<std::uintptr_t>(image+Nodes+((n+s)%274)*0xa8));Put(image,0x4560000+n*8,reinterpret_cast<std::uintptr_t>(image+0x4700000+n*128));}
 Check(aa()==nullptr,"257th actor must be refused");Check(sa(0)==nullptr,"275th sentient must be refused");
 phase="coherent snapshot";std::memset(image+Frame+4,0xff,2048*2);for(unsigned n=0;n<64;++n){Put(image,0xf50148+n*40,int(600+n));Put(image,0xf5014c+n*40,std::uint8_t(1));Put(image,0xf50150+n*40,int(55500+n));}Put(image,0xd97bec,int(1));Put(image,0xd96ec8,int(987654));reinterpret_cast<Void>(image+0x174e50)();reinterpret_cast<Void>(image+0x170af0)();Check(!std::memcmp(image+Frame,image+ClientFrame,0x4404),"Snapshot copy incomplete");
 using Team=void(ABI*)(int,int*,int*);auto team=reinterpret_cast<Team>(image+0x12c6a0);for(unsigned n=0;n<256;++n){int index=-1,t=-1;team(n+2,&index,&t);Check(index==int(n) && t==2,"Client team lookup truncated index");}
 using Goal=void*(ABI*)(int);auto goal=reinterpret_cast<Goal>(image+0x15cec0);auto state=reinterpret_cast<Goal>(image+0x15cee0);for(unsigned n=0;n<320;++n){int entity=n<256?n+2:600+n-256;Check(goal(entity)==image+ClientFrame+0x2e04+n*16 && state(entity)==image+ClientFrame+0x1004+n*24,"Client goal lookup truncated index");}
 phase="linked sentient handle roundtrip";
 reinterpret_cast<Void>(image+0x15b000)();reinterpret_cast<Void>(image+0x15b050)();
 using HandleSet=void(ABI*)(void*,void*);using FieldWrite=void(ABI*)(const SaveField*,void*,const void*);using FieldRead=void(ABI*)(const SaveField*,void*,void*);
 auto setHandle=reinterpret_cast<HandleSet>(image+0x15b270);auto fieldWrite=reinterpret_cast<FieldWrite>(image+0x1ae680);auto fieldRead=reinterpret_cast<FieldRead>(image+0x1ae0d0);SaveField handleField{0,8,0};
 for(unsigned n=0;n<274;++n){std::uint32_t handle=0,encoded=0;auto* sentient=image+Sentients+n*0x88;setHandle(&handle,sentient);encoded=handle;fieldWrite(&handleField,&encoded,&handle);Check(encoded==n+1,"Linked handle encoder truncated sentient");setHandle(&handle,nullptr);fieldRead(&handleField,&encoded,nullptr);Check(static_cast<std::uint16_t>(encoded)==n+1,"Linked handle decoder truncated sentient");reinterpret_cast<One>(image+0x15b100)(sentient);Check(encoded==0,"High sentient head release did not clear registered handle");}
 PopulationChunkHeader wrong;wrong.version=2;Check(!SupportedPopulationChunk(wrong),"Future checkpoint layout accepted");wrong.version=0;Check(!SupportedPopulationChunk(wrong),"Invalid checkpoint layout accepted");
 fields=ExpandedSaveFields(Fields(0x340bb0));const auto expandedBytes=SaveLoad(false);
 // Convert low perception records to the stock inline representation before
 // asking the actual G_Save loops to generate the untagged legacy stream.
 for(unsigned n=0;n<32;++n){auto* a=image+Pool+n*kStride;std::memcpy(a+0x348,a+kPerceptionStart,50*48);Put(a,0xcb0,reinterpret_cast<std::uintptr_t>(image+Sentients+49*0x88));Put(a,0x13c8,reinterpret_cast<std::uintptr_t>(image+Pool+((n+1)%32)*kStride));}
 const auto stockBytes=SaveLoad(true);
 std::cout<<"{\"unrelated_fx_records_per_array\":32,\"native_actor_capacity\":256,\"native_sentient_capacity\":274,\"native_animation_trees_created_and_cleared\":658,\"native_temporary_tree_leases\":336,\"native_client_team_cases\":256,\"native_linked_sentient_handle_roundtrips\":274,\"native_client_goal_pairs\":320,\"snapshot_bytes\":17412,\"native_g_save_g_load_actor_records\":256,\"perception_node_pointers\":70144,\"legacy_actor_records_migrated\":32,\"expanded_stream_bytes\":"<<expandedBytes<<",\"legacy_stream_bytes\":"<<stockBytes<<",\"transport\":\"native uncompressed MemFile with RLE\",\"other_engine_checkpoint_domains\":\"isolated\",\"booted_game_tested\":false}\n";
 }catch(const std::exception& e){std::cerr<<phase<<": "<<e.what()<<'\n';return 1;}}
