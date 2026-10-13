// Actual native cleanup and record serializers in the disposable expanded image.
#include "expanded_actor_layout.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/resource.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <csignal>
#include <cstdio>
#include <ucontext.h>
#include <unistd.h>
namespace {
using namespace mod::expansion;
constexpr std::size_t kImageSize=0x5000000,kPool=0x4000000,kSentientPool=0x4500000,kAnimations=0x4560000;
constexpr std::size_t kHeads=0x4570000,kNodes=0x4580000,kLimit=0x4550000;
std::uintptr_t fixtureBase=0;
const char* phase="initialize";
void Fault(int, siginfo_t* info, void* context) {
    const auto ip=static_cast<std::uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
    char message[160];const auto n=std::snprintf(message,sizeof(message),"Native fixture fault phase=%s rva=0x%llx address=%p\n",phase,
        static_cast<unsigned long long>(ip-fixtureBase),info->si_addr);
    if(n>0){const auto written=write(STDERR_FILENO,message,std::min(n,159));(void)written;}
    _exit(71);
}
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>void Put(void* base,std::size_t at,T v){std::memcpy(static_cast<char*>(base)+at,&v,sizeof(v));}
template<class T>T Get(const void* base,std::size_t at){T v;std::memcpy(&v,static_cast<const char*>(base)+at,sizeof(v));return v;}
using Allocate=void*(__attribute__((ms_abi))*)();
using SentientAllocate=void*(__attribute__((ms_abi))*)(int);
using One=void(__attribute__((ms_abi))*)(void*);
using Void=void(__attribute__((ms_abi))*)();
using Perception=void*(__attribute__((ms_abi))*)(void*,void*);
using Write=void(__attribute__((ms_abi))*)(const SaveField*,const void*,void*,int,void*);
using Read=void(__attribute__((ms_abi))*)(const SaveField*,void*,int,void*);
struct Stream {std::vector<char> bytes;std::size_t cursor=0;bool valid=true;};
void __attribute__((ms_abi)) MemoryWrite(Stream* stream,int size,const void* data) {
    if(!stream || size<0 || size>0x100000 || !data){if(stream)stream->valid=false;return;}
    const auto* begin=static_cast<const char*>(data);stream->bytes.insert(stream->bytes.end(),begin,begin+size);
}
void __attribute__((ms_abi)) MemoryRead(void* data,int size,Stream* stream) {
    if(!stream || size<0 || !data || static_cast<std::size_t>(size)>stream->bytes.size()-stream->cursor){if(stream)stream->valid=false;return;}
    std::memcpy(data,stream->bytes.data()+stream->cursor,size);stream->cursor+=size;
}
std::vector<std::uintptr_t> clearedTrees;
void __attribute__((ms_abi)) ClearTree(std::uintptr_t tree,int) {clearedTrees.push_back(tree);}
void Detour(std::uint8_t* image,std::size_t rva,std::uintptr_t target) {
    const auto code=mod::AbsoluteJump(target);std::memcpy(image+rva,code.data(),code.size());
}
std::vector<SaveField> Fields(std::uint8_t* image,std::size_t at) {
    std::vector<SaveField> fields;
    for(unsigned n=0;n<400;++n){const auto f=Get<SaveField>(image,at+n*16);fields.push_back(f);if(!f.kind)return fields;}
    throw std::runtime_error("Unterminated save descriptor table");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const rlimit noCore{0,0};setrlimit(RLIMIT_CORE,&noCore);
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4700000 && bytes.size()<kImageSize,"Invalid expanded lifecycle image");
        auto* image=static_cast<std::uint8_t*>(mmap(nullptr,kImageSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        fixtureBase=reinterpret_cast<std::uintptr_t>(image);
        struct sigaction action{};action.sa_sigaction=Fault;action.sa_flags=SA_SIGINFO;
        sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
        struct Release {void* image;~Release(){munmap(image,kImageSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(image+kPool));
        Put(image,0xd96e98,reinterpret_cast<std::uintptr_t>(image+kSentientPool));
        Put(image,0xd96e78,reinterpret_cast<std::uintptr_t>(image+0xd9e070));
        Put(image,0xd9c0e0,reinterpret_cast<std::uintptr_t>(image+kLimit));
        Put(image,kLimit+0x10,int(kActors));Put(image,0xd96ee0,0);
        Put(image,0xcf7090,reinterpret_cast<std::uintptr_t>(image+kNodes));
        Put(image,0xf56484,int(kSentients));
        std::memset(image+kPool,0,kActors*kStride);std::memset(image+kSentientPool,0,kSentients*kSentientStride);
        std::memset(image+kHeads-16,0xa5,16);std::memset(image+kHeads+kSentients*2,0xa5,16);
        Detour(image,0x270c90,reinterpret_cast<std::uintptr_t>(&MemoryWrite));
        Detour(image,0x1af020,reinterpret_cast<std::uintptr_t>(&MemoryRead));
        Detour(image,0x29c440,reinterpret_cast<std::uintptr_t>(&ClearTree));
        const auto allocate=reinterpret_cast<Allocate>(image+0x123950);
        const auto allocateSentient=reinterpret_cast<SentientAllocate>(image+0x1b0be0);
        const auto accessor=reinterpret_cast<Perception>(image+0x12d410);
        phase="sentient allocations";
        for(std::size_t n=0;n<kSentients;++n) {
            auto* sentient=static_cast<std::uint8_t*>(allocateSentient(0));
            Check(sentient==image+kSentientPool+n*kSentientStride,"Sentient allocation changed");
            const auto entity=0xd9e070+(n+2)*0x2a0;
            Put(image,entity+0x7e,static_cast<std::uint16_t>(n+2));
            Put(image,entity+0x118,reinterpret_cast<std::uintptr_t>(sentient));
            Put(sentient,0,reinterpret_cast<std::uintptr_t>(image+entity));Put(sentient,8,int(2));
        }
        phase="actor allocations and accessors";
        for(std::size_t n=0;n<kActors;++n) {
            auto* actor=static_cast<std::uint8_t*>(allocate());
            Check(actor==image+kPool+n*kStride,"Actor allocation changed");
            const auto entity=image+0xd9e070+(n+2)*0x2a0;
            Put(actor,0,reinterpret_cast<std::uintptr_t>(entity));
            Put(actor,8,reinterpret_cast<std::uintptr_t>(image+kSentientPool+n*kSentientStride));
            Put(entity,0x110,reinterpret_cast<std::uintptr_t>(actor));
            Put(actor,0x13c8,reinterpret_cast<std::uintptr_t>(image+kPool+((n+1)%kActors)*kStride));
            Put(actor,0xcb0,reinterpret_cast<std::uintptr_t>(image+kSentientPool+273*kSentientStride));
            Put(actor,0x1a0,int(10000+n));
            for(std::size_t s=0;s<kSentients;++s) {
                auto* record=actor+*PerceptionOffset(s);
                Check(accessor(actor,image+kSentientPool+s*kSentientStride)==record,"Common native perception accessor missed an appended record");
                Put(record,16,int(20000+n+s));Put(record,28,float(n+s));
                Put(record,40,reinterpret_cast<std::uintptr_t>(image+kNodes+((n+s)%kSentients)*0xa8));
            }
        }
        const auto originalActorFields=Fields(image,0x340bb0);
        const auto actorFields=ExpandedSaveFields(originalActorFields);
        const auto sentientFields=Fields(image,0x341120);
        const auto write=reinterpret_cast<Write>(image+0x1adfb0);
        const auto read=reinterpret_cast<Read>(image+0x1adf50);
        const std::vector<char> actorBaseline(reinterpret_cast<char*>(image+kPool),reinterpret_cast<char*>(image+kPool+kActors*kStride));
        const std::vector<char> sentientBaseline(reinterpret_cast<char*>(image+kSentientPool),reinterpret_cast<char*>(image+kSentientPool+kSentients*kSentientStride));
        Stream stream;const PopulationChunkHeader header;
        MemoryWrite(&stream,sizeof(header),&header);
        std::vector<char> temporary(kStride);
        phase="actor record write";
        for(std::size_t n=0;n<kActors;++n) {
            const std::uint8_t active=1;MemoryWrite(&stream,1,&active);
            auto* actor=image+kPool+n*kStride;std::memcpy(temporary.data(),actor,kStride);
            write(actorFields.data(),actor,temporary.data(),kStride,&stream);
        }
        phase="sentient record write";
        for(std::size_t n=0;n<kSentients;++n) {
            const std::uint8_t active=1;MemoryWrite(&stream,1,&active);
            auto* sentient=image+kSentientPool+n*kSentientStride;std::memcpy(temporary.data(),sentient,kSentientStride);
            write(sentientFields.data(),sentient,temporary.data(),kSentientStride,&stream);
        }
        Check(stream.valid,"Native population writer crossed the memory stream");
        const auto expectedBytes=sizeof(header)+kActors*(1+kStride)+kSentients*(1+kSentientStride);
        Check(stream.bytes.size()==expectedBytes,"Native population chunk has the wrong serialized size");
        std::memset(image+kPool,0,kActors*kStride);std::memset(image+kSentientPool,0,kSentients*kSentientStride);
        PopulationChunkHeader restored;MemoryRead(&restored,sizeof(restored),&stream);
        Check(SupportedPopulationChunk(restored),"Expanded population discriminator was lost");
        auto wrong=restored;++wrong.version;Check(!SupportedPopulationChunk(wrong),"Unknown population version accepted");
        wrong=restored;wrong.actors=32;Check(!SupportedPopulationChunk(wrong),"Stock count accepted as an expanded chunk");
        phase="actor record read";
        for(std::size_t n=0;n<kActors;++n) {
            std::uint8_t active=0;MemoryRead(&active,1,&stream);Check(active==1,"Actor flag truncated");
            read(actorFields.data(),image+kPool+n*kStride,kStride,&stream);
        }
        phase="sentient record read";
        for(std::size_t n=0;n<kSentients;++n) {
            std::uint8_t active=0;MemoryRead(&active,1,&stream);Check(active==1,"Sentient flag truncated");
            read(sentientFields.data(),image+kSentientPool+n*kSentientStride,kSentientStride,&stream);
        }
        Check(stream.valid && stream.cursor==stream.bytes.size(),"Native population reader did not consume exactly its chunk");
        Check(!std::memcmp(image+kPool,actorBaseline.data(),actorBaseline.size()),"Native actor record round-trip changed a pointer/field");
        Check(!std::memcmp(image+kSentientPool,sentientBaseline.data(),sentientBaseline.size()),"Native sentient record round-trip changed a pointer/field");
        // Stock record decoding uses the original descriptors before moving its
        // fifty inline perception records into the appended expanded layout.
        phase="legacy record migration";
        std::vector<char> legacy(kOldStride);std::memcpy(legacy.data(),image+kPool,0x1558);
        Put(legacy.data(),0xcb0,reinterpret_cast<std::uintptr_t>(image+kSentientPool+49*kSentientStride));
        for(std::size_t n=0;n<50;++n) {
            std::memcpy(legacy.data()+0x348+n*kRecordBytes,image+kPool+kPerceptionStart+n*kRecordBytes,kRecordBytes);
            if(n==49)Put(legacy.data(),0x348+n*kRecordBytes+40,std::uintptr_t(0));
        }
        Stream legacyStream;std::memcpy(temporary.data(),legacy.data(),0x1558);
        write(originalActorFields.data(),legacy.data(),temporary.data(),0x1558,&legacyStream);
        std::vector<char> decoded(kOldStride),migrated(kStride);
        read(originalActorFields.data(),decoded.data(),0x1558,&legacyStream);
        Check(legacyStream.valid && legacyStream.cursor==legacyStream.bytes.size() && !std::memcmp(decoded.data(),legacy.data(),0x1558),"Native stock record decoding failed");
        Check(MigrateLegacyActor(decoded.data(),decoded.size(),migrated.data(),migrated.size()),"Stock actor migration failed");
        Check(!std::memcmp(migrated.data()+kPerceptionStart,legacy.data()+0x348,50*kRecordBytes),"Legacy perception migration changed a record");
        for(std::size_t n=kPerceptionStart+50*kRecordBytes;n<kStride;++n)Check(!migrated[n],"Legacy migration retained stale high perception slots");
        Check(!MigrateLegacyActor(decoded.data(),0x1557,migrated.data(),migrated.size()),"Truncated stock actor accepted");
        const auto untouched=migrated;
        Check(!MigrateLegacyActor(migrated.data(),0x1558,migrated.data(),migrated.size()) && migrated==untouched,"Overlapping legacy migration damaged its input");
        // Retain sixteen non-actor sentients, all beyond the old index-49 bound.
        // A new special allocation must recycle its marked victim at index 270.
        phase="special sentient victim cleanup";
        for(std::size_t n=256;n<272;++n)Put(image,kSentientPool+n*kSentientStride+0x83,std::uint8_t(n==270));
        const auto victim=image+kSentientPool+270*kSentientStride;
        const auto victimEntity=Get<std::uintptr_t>(victim,0);
        for(std::size_t n=0;n<kActors;++n)Put(image,kPool+n*kStride+0x13d0,victimEntity);
        Check(allocateSentient(2)==victim,"Special allocator missed the disposable sentient past slot 49");
        unsigned clearedPerception=0;
        for(std::size_t n=0;n<kActors;++n) {
            auto* actor=image+kPool+n*kStride;
            Check(!Get<std::uintptr_t>(actor,0x13d0),"Sentient free missed a high actor's entity reference");
            for(unsigned b=0;b<kRecordBytes;++b)Check(!actor[*PerceptionOffset(270)+b],"Sentient free missed appended perception");
            ++clearedPerception;
        }
        phase="high sentient free and reuse";
        Put(victim,0,victimEntity);Put(victim,8,int(2));Put(reinterpret_cast<void*>(victimEntity),0x118,reinterpret_cast<std::uintptr_t>(victim));
        reinterpret_cast<One>(image+0x1b14a0)(victim);
        Check(!victim[0x82] && !Get<std::uintptr_t>(reinterpret_cast<void*>(victimEntity),0x118),"Native sentient free did not release the high slot/entity link");
        Check(allocateSentient(0)==victim,"Ordinary allocator did not reuse a released high sentient");
        // Execute the native reference-head and full animation-clear lifecycles.
        phase="reference head reset";
        std::memset(image+kHeads,0x5a,kSentients*2);reinterpret_cast<Void>(image+0x15b050)();
        for(std::size_t n=0;n<kSentients*2;++n)Check(!image[kHeads+n],"Reference-head reset missed a high sentient");
        for(unsigned n=0;n<16;++n)Check(image[kHeads-16+n]==0xa5 && image[kHeads+kSentients*2+n]==0xa5,"Reference-head reset crossed its bounds");
        for(std::size_t n=0;n<kActors;++n)Put(image,kAnimations+n*8,std::uintptr_t(0x10000+n));
        phase="animation cleanup";
        Put(image,0xd97be8,int(0));reinterpret_cast<Void>(image+0x1710c0)();
        Check(clearedTrees.size()==256,"Native animation cleanup did not visit all 256 trees");
        for(std::size_t n=0;n<kActors;++n)Check(clearedTrees[n]==0x10000+n && !Get<std::uintptr_t>(image,kAnimations+n*8),"Native animation cleanup lost a high entry");
        std::cout<<"{\"probe\":\"native expanded lifecycle and population records\","
            "\"native_perception_accessor_cases\":"<<kActors*kSentients<<","
            "\"native_actor_records_roundtripped\":256,\"native_sentient_records_roundtripped\":274,"
            "\"native_perception_node_pointers_roundtripped\":"<<kActors*kSentients<<","
            "\"population_chunk_bytes\":"<<stream.bytes.size()<<","
            "\"expanded_header_rejects_wrong_layout\":true,\"native_stock_record_migration\":true,"
            "\"native_special_victim_index\":270,\"native_cleanup_actor_references\":256,"
            "\"native_cleanup_perception_records\":"<<clearedPerception<<",\"native_high_sentient_free_reuse\":true,"
            "\"converted_reference_heads_reset\":274,\"native_animation_trees_cleared\":256,"
            "\"substituted_dependencies\":[\"memory stream byte transport\",\"animation tree release recorder\",\"following global engine teardown omitted\",\"Test19 perception notification/profiler isolation\"],"
            "\"full_engine_checkpoint_tested\":false,\"client_snapshot_consumers_tested\":false,\"runtime_expansion_enabled\":false}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
