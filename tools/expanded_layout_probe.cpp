// Isolated exact-native routines in an appended actor/perception layout.
// This fixture does not boot MW2 or enable a runtime expansion in the DLL.
#include "expanded_actor_layout.hpp"
#include <sys/mman.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace mod::expansion;
constexpr std::size_t kSize=0x5000000,kPool=0x4000000,kSentientsPool=0x4500000,kMap=0x4510000;
constexpr std::size_t kGoalIds=0x4520000,kGoalState=0x4530000,kPresence=0x4540000,kTeam=kPresence+256;
constexpr std::size_t kAnimations=0x4560000;
constexpr std::size_t kLimit=0x4550000,kVisits=kLimit+0x100,kLast=kVisits+8,kPredicate=0x46f0000;
void Check(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
template<class T> void Put(std::uint8_t* base,std::size_t at,T value) {std::memcpy(base+at,&value,sizeof(value));}
template<class T> T Get(const std::uint8_t* base,std::size_t at) {T out{};std::memcpy(&out,base+at,sizeof(out));return out;}
using Allocate=void*(__attribute__((ms_abi))*)();
using AllocateSentient=void*(__attribute__((ms_abi))*)(int);
using Iterate=void(__attribute__((ms_abi))*)();
using Clear=void(__attribute__((ms_abi))*)(void*,void*);
using Copy=void(__attribute__((ms_abi))*)(void*,void*,int);
using WriteField=void(__attribute__((ms_abi))*)(const SaveField*,void*,const void*);
using ReadField=void(__attribute__((ms_abi))*)(const SaveField*,void*,void*);
using Animation=std::uintptr_t(__attribute__((ms_abi))*)(void*);
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4700000 && bytes.size()<kSize,"Invalid private expanded fixture");
        auto* image=static_cast<std::uint8_t*>(mmap(nullptr,kSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        struct Release {void* address;~Release(){munmap(address,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        std::memset(image+kPool,0,kActors*kStride);std::memset(image+kSentientsPool,0,kSentients*kSentientStride);
        Put(image,0xd96ea0,reinterpret_cast<std::uintptr_t>(image+kPool));
        Put(image,0xd96e98,reinterpret_cast<std::uintptr_t>(image+kSentientsPool));
        Put(image,0xd9c0e0,reinterpret_cast<std::uintptr_t>(image+kLimit));
        Put(image,kLimit+0x10,int(kActors));Put(image,0xd96ee0,0);
        Put(image,kPredicate+2,reinterpret_cast<std::uintptr_t>(image+kVisits));
        Put(image,kPredicate+14,reinterpret_cast<std::uintptr_t>(image+kLast));
        std::memset(image+kPool-16,0xa5,16);std::memset(image+kPool+kActors*kStride,0xa5,16);
        const auto allocateSentient=reinterpret_cast<AllocateSentient>(image+0x1b0be0);
        for(std::size_t n=0;n<kSentients;++n)Check(allocateSentient(0)==image+kSentientsPool+n*kSentientStride,
            "Native ordinary sentient allocation did not reach all expanded slots");
        Check(allocateSentient(0)==nullptr,"Native ordinary sentient allocator crossed slot 273");
        const auto allocate=reinterpret_cast<Allocate>(image+0x123950);
        for(std::size_t n=0;n<kActors;++n) {
            auto* actor=static_cast<std::uint8_t*>(allocate());
            Check(actor==image+kPool+n*kStride && actor[0x14ff]==1,"Expanded native allocator returned an invalid slot/active flag");
            const auto entity=0xd9e070+(n+2)*0x2a0;
            Put(image,entity+0x7e,static_cast<std::uint16_t>(n+2));
            Put(actor,0,reinterpret_cast<std::uintptr_t>(image+entity));
            Put(actor,8,reinterpret_cast<std::uintptr_t>(image+kSentientsPool+n*kSentientStride));
            Put(image,entity+0x110,reinterpret_cast<std::uintptr_t>(actor));
            Put(image,entity+0x118,reinterpret_cast<std::uintptr_t>(image+kSentientsPool+n*kSentientStride));
            Put(image,kSentientsPool+n*kSentientStride+8,int(n==255?2:1));
            Put(actor,0x19c,std::uint8_t(1));Put(actor,0x1a0,int(10000+n));
            Put(actor,0x1a8,float(500+n));Put(actor,0x1ac,float(600+n));Put(actor,0x1b0,float(700+n));
            Put(actor,0x14f0,int(1));
            Put(image,kAnimations+n*8,reinterpret_cast<std::uintptr_t>(image+kGoalState+n*24));
        }
        Check(Get<int>(image,0xd96ee0)==256 && allocate()==nullptr,"Native allocation did not stop after 256 actors");
        const auto iterate=reinterpret_cast<Iterate>(image+0x123bc0);iterate();
        Check(Get<unsigned>(image,kVisits)==256 && Get<std::uintptr_t>(image,kLast)==reinterpret_cast<std::uintptr_t>(image+kPool+255*kStride+0xfa4),
            "Expanded native iterator did not reach slot 255 with the new division/stride");
        const auto clear=reinterpret_cast<Clear>(image+0x1247e0);
        const auto copy=reinterpret_cast<Copy>(image+0x12d450);
        auto* destination=image+kPool;auto* source=image+kPool+kStride;
        std::memset(destination+0xca8,0x5a,48);std::memset(source+0xca8,0x5a,48);
        unsigned cleared=0,copied=0;
        for(std::size_t n=0;n<kSentients;++n) {
            const auto offset=*PerceptionOffset(n);
            std::memset(destination+offset,0xa5,48);
            clear(destination,image+kSentientsPool+n*kSentientStride);
            for(unsigned byte=0;byte<48;++byte)Check(destination[offset+byte]==0,"Native perception clear missed an expanded record");
            ++cleared;
            Put(source,offset+16,int(20000+n));
            Put(source,offset+28,float(10+n));Put(source,offset+32,float(20+n));Put(source,offset+36,float(30+n));
            Put(source,offset+40,std::uintptr_t(0x1122334455667788));
            copy(destination,source,static_cast<int>(n));
            Check(Get<int>(destination,offset+16)==20000+static_cast<int>(n)
                && Get<float>(destination,offset+28)==10+n && Get<std::uintptr_t>(destination,offset+40)==0x1122334455667788,
                "Native perception copy lost an expanded slot's sight position/node data");
            ++copied;
        }
        for(unsigned byte=0;byte<48;++byte)Check(destination[0xca8+byte]==0x5a && source[0xca8+byte]==0x5a,
            "Expanded perception overwrote the original actor tail");
        for(unsigned byte=0;byte<16;++byte)Check(image[kPool-16+byte]==0xa5 && image[kPool+kActors*kStride+byte]==0xa5,"Expanded actor pool guard overwritten");
        const auto animation=reinterpret_cast<Animation>(image+0x12c770);
        for(std::size_t n=0;n<256;++n)Check(animation(image+kPool+n*kStride)==reinterpret_cast<std::uintptr_t>(image+kGoalState+n*24),
            "Expanded native animation-pointer lookup lost an actor index");
        // Run the native snapshot body and native corpse snapshot copy with
        // widened producer stores, relocated arrays and real actor index lookup.
        std::memset(image+kMap,0xff,2048*2);
        const auto corpseRecords=0x174f85+7+Get<std::int32_t>(image,0x174f85+3)-4;
        for(unsigned n=0;n<64;++n)Put(image,corpseRecords+n*40,int(-1));
        Put(image,corpseRecords+63*40,int(511));
        Put(image,corpseRecords+63*40+4,std::uint8_t(1));Put(image,corpseRecords+63*40+8,int(55555));
        Put(image,corpseRecords+63*40+16,float(155.5));
        Put(image,0xd97bec,int(1));
        reinterpret_cast<Iterate>(image+0x174e50)();
        for(unsigned n=0;n<256;++n)Check(Get<std::uint16_t>(image,kMap+(n+2)*2)==n,"Snapshot producer truncated an actor index");
        Check(Get<std::uint16_t>(image,kMap+511*2)==319,"Snapshot producer aliased the last corpse with an actor");
        Check(Get<std::uint8_t>(image,kTeam+255)==2 && Get<int>(image,kGoalIds+255*16)==10255
            && Get<float>(image,kGoalIds+255*16+4)==755,"Snapshot producer lost slot-255 team/goal state");
        Check(Get<std::uint8_t>(image,kPresence+255)==1 && Get<int>(image,kGoalIds+319*16)==55555
            && Get<float>(image,kGoalIds+319*16+4)==155.5,"Native snapshot callbacks lost expanded animation/corpse state");
        // Execute actual native save-field pointer conversion and restoration,
        // including index 256 and sentient 274. No stream writer/import is used.
        const auto write=reinterpret_cast<WriteField>(image+0x1ae680);
        const auto read=reinterpret_cast<ReadField>(image+0x1ae0d0);
        unsigned actorReferences=0,sentientReferences=0;
        for(unsigned kind:{6u,7u}) {
            const auto count=kind==6?kActors:kSentients;
            const auto pool=kind==6?kPool:kSentientsPool;
            const auto stride=kind==6?kStride:kSentientStride;
            const SaveField field{0,kind,0};
            for(std::size_t n=0;n<count;++n) {
                const auto original=reinterpret_cast<std::uintptr_t>(image+pool+n*stride);auto encoded=original;
                write(&field,&encoded,&original);
                Check(static_cast<std::uint32_t>(encoded)==n+1,"Native save-field encoder truncated an expanded pointer index");
                read(&field,&encoded,nullptr);
                Check(encoded==original,"Native save-field decoder did not restore an expanded pool pointer");
                kind==6?++actorReferences:++sentientReferences;
            }
        }
        std::vector<SaveField> originalFields;
        for(unsigned n=0;n<256;++n) {
            const auto field=Get<SaveField>(image,0x340bb0+n*16);originalFields.push_back(field);if(!field.kind)break;
        }
        const auto fields=ExpandedSaveFields(originalFields);unsigned perceptionFields=0;
        for(const auto& field:fields)if(field.kind==14 && field.offset>=kPerceptionStart) {
            Check(field.offset+8<=kStride,"Expanded perception save field crosses actor boundary");++perceptionFields;
        }
        Check(perceptionFields==274,"Expanded save descriptor omits perception node references");
        destination[0x14ff]=0;Put(image,0xd96ee0,255);
        Check(allocate()==destination,"Native allocator did not reuse the freed expanded slot");
        for(std::size_t n=kPerceptionStart;n<kStride;++n)Check(destination[n]==0,"Native allocator retained stale appended perception on reuse");
        reinterpret_cast<Iterate>(image+0x12c840)();
        Check(Get<int>(image,0xd96ee0)==0,"Converted actor reset did not clear the native count");
        for(std::size_t n=0;n<kActors;++n)Check(image[kPool+n*kStride+0x14ff]==0,"Converted reset missed an expanded active flag");
        std::cout<<"{\n  \"probe\":\"isolated appended 256-actor native layout\",\n"
            "  \"native_actor_allocations\":256,\n  \"native_257th_allocation_refused\":true,\n"
            "  \"native_ordinary_sentient_allocations\":274,\n  \"native_275th_sentient_allocation_refused\":true,\n"
            "  \"native_iteration_visits\":256,\n  \"native_perception_records_cleared\":"<<cleared<<",\n"
            "  \"native_perception_records_copied\":"<<copied<<",\n  \"actor_tail_and_pool_guards_intact\":true,\n"
            "  \"native_snapshot_actor_indices\":256,\n  \"native_snapshot_highest_corpse_index\":319,\n"
            "  \"native_animation_pointer_lookups\":256,\n  \"native_corpse_snapshot_goal_copy_verified\":true,\n  \"converted_reset_active_flags\":256,\n"
            "  \"native_actor_pointer_roundtrips\":"<<actorReferences<<",\n  \"native_sentient_pointer_roundtrips\":"<<sentientReferences<<",\n"
            "  \"expanded_perception_save_descriptors\":"<<perceptionFields<<",\n  \"appended_perception_cleared_on_reuse\":true,\n"
            "  \"runtime_expansion_enabled\":false,\n"
            "  \"substituted_dependencies\":[\"memset MS-ABI REP STOSB\",\"iterator predicate records visits\",\"perception callbacks skipped\",\"snapshot profiler isolated\"],\n"
            "  \"scope\":\"Native routines in a private Linux MS-ABI fixture. Full checkpoint streams, snapshot consumers, all AI perception consumers and Windows gameplay are not executed.\"\n}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
