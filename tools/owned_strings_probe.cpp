// Native string-user/field reference handling in a private MS-ABI fixture.
// Only single-thread locks and the pool allocator/free are substituted.
#include "ai_persistence.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t kSize=0x5000000,kPool=0x4f00000,kFreeCount=0x4f10000;
constexpr std::size_t kPoolSlot=0x1466008,kHash=0x1568280,kStringSlot=32;
using Intern=unsigned(__attribute__((ms_abi))*)(const char*);
using Set=void(__attribute__((ms_abi))*)(std::uintptr_t,unsigned);
using Hierarchy=void(__attribute__((ms_abi))*)(void*,void*);
std::uintptr_t nativeImage=0;
void NativeFault(int,siginfo_t*,void* context) {
    const auto rip=static_cast<std::uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
    _exit(rip==nativeImage+0x29967d?77:78);
}
template<class T> void Put(std::uint8_t* image,std::size_t at,T value) {std::memcpy(image+at,&value,sizeof(value));}
template<class T> T Get(std::uint8_t* image,std::size_t at) {T v{};std::memcpy(&v,image+at,sizeof(v));return v;}
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()<kPool,"Invalid private image");
        auto* image=static_cast<std::uint8_t*>(mmap(nullptr,kSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        struct Release {void* p;~Release(){munmap(p,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        // Deterministic allocation at one private pool slot, and a native
        // release recorder. No original heap or Windows OS locks are entered.
        const std::uint8_t allocate[]={0xb8,kStringSlot,0,0,0,0xc3};
        std::memcpy(image+0x216f00,allocate,sizeof(allocate));
        image[0x274580]=image[0x2745e0]=0xc3;
        const std::uint8_t freeCode[]={0xff,0x05,0,0,0,0,0xc3};
        std::memcpy(image+0x2175e0,freeCode,sizeof(freeCode));
        Put(image,0x2175e2,static_cast<std::int32_t>(kFreeCount-(0x2175e0+6)));
        Put(image,kPoolSlot,reinterpret_cast<std::uintptr_t>(image+kPool));
        const auto intern=reinterpret_cast<Intern>(image+mod::kAiInternStringRva);
        const auto set=reinterpret_cast<Set>(image+0x21ad70);
        const auto slot=kPool+kStringSlot*16;
        const auto references=[&](){return Get<std::uint16_t>(image,slot);};
        const auto frees=[&](){return Get<unsigned>(image,kFreeCount);};
        const auto reset=[&]() {
            std::memset(image+kHash-4,0,0x38008);
            std::memset(image+kPool,0,0x20000);
        };
        const auto oldMarkers=[&](std::array<std::uint16_t,32>& fields) {
            for(auto& field:fields) {
                const auto id=intern(mod::kOwnedSpawnerTag);
                Check(id==kStringSlot,"Native marker allocation returned another slot");
                set(reinterpret_cast<std::uintptr_t>(&field),id);
                auto temporary=static_cast<std::uint16_t>(id);
                set(reinterpret_cast<std::uintptr_t>(&temporary),0);
            }
        };
        reset();
        const auto first=intern(mod::kOwnedSpawnerTag);const auto firstRefs=references();
        const auto second=intern(mod::kOwnedSpawnerTag);
        Check(first==second && firstRefs==1 && references()==1,
            "Native repeated user-1 lookup unexpectedly acquired a temporary reference");
        reset();std::array<std::uint16_t,32> oldFields{};oldMarkers(oldFields);
        Check(references()==1,"Old marker path did not reproduce missing field references");
        set(reinterpret_cast<std::uintptr_t>(&oldFields[0]),0);
        Check(frees()==1 && oldFields[1]==kStringSlot,"Old path did not free a label still used by 31 helpers");
        // DObj model vectors and archived VM buffers use the same native pool.
        // Reusing that freed slot demonstrates what another stale label
        // release does to their first pointer, without booting the game.
        constexpr std::uint64_t model=0x7ff6b8e48ea8ULL;
        Put(image,slot,model);
        set(reinterpret_cast<std::uintptr_t>(&oldFields[1]),0);
        set(reinterpret_cast<std::uintptr_t>(&oldFields[2]),0);
        Check(Get<std::uint64_t>(image,slot)==model-2,
            "Stale native label releases did not decrement a reused DObj model pointer");
        // Execute the returned crash's actual hierarchy routine with a valid
        // one-bone model, then corrupt that vector through the stale labels.
        // The child catches only the fixture fault to report its native RVA.
        reset();oldFields={};oldMarkers(oldFields);set(reinterpret_cast<std::uintptr_t>(&oldFields[0]),0);
        constexpr std::size_t object=0x4f20000,bits=0x4f21000,xmodel=0x4f30000,parents=xmodel+0x80;
        Put(image,slot,reinterpret_cast<std::uintptr_t>(image+xmodel));image[slot+8]=0xff;
        Put(image,object+8,std::uint16_t(64));image[object+0xd]=1;image[object+0xe]=1;
        Put(image,object+0xa8,reinterpret_cast<std::uintptr_t>(image+slot));
        image[xmodel+8]=1;image[xmodel+9]=1;
        Put(image,xmodel+0x30,reinterpret_cast<std::uintptr_t>(image+parents));
        const auto hierarchy=reinterpret_cast<Hierarchy>(image+0x2995a0);
        hierarchy(image+object,image+bits);
        Check(Get<unsigned>(image,bits)==0x80000000,"Valid native one-bone hierarchy did not preserve its root bit");
        set(reinterpret_cast<std::uintptr_t>(&oldFields[1]),0);set(reinterpret_cast<std::uintptr_t>(&oldFields[2]),0);
        Check(Get<std::uintptr_t>(image,slot)==reinterpret_cast<std::uintptr_t>(image+xmodel)-2,
            "Native stale releases did not move the valid model pointer back two bytes");
        const auto child=fork();Check(child>=0,"Unable to isolate the native failure");
        if(!child) {
            rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);nativeImage=reinterpret_cast<std::uintptr_t>(image);
            struct sigaction action{};action.sa_sigaction=NativeFault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
            sigaction(SIGSEGV,&action,nullptr);
            hierarchy(image+object,image+bits);_exit(0);
        }
        int status=0;Check(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77,
            "Reused native model corruption did not reproduce the returned hierarchy fault RVA");
        reset();oldFields={};oldMarkers(oldFields);set(reinterpret_cast<std::uintptr_t>(&oldFields[0]),0);
        constexpr std::uint64_t code=0x2ce5b87dad3ULL;
        Put(image,slot,code);
        set(reinterpret_cast<std::uintptr_t>(&oldFields[1]),0);
        Check(Get<std::uint64_t>(image,slot)==code-1,
            "Stale native label release did not decrement a reused VM archive code pointer");
        reset();std::array<std::uint16_t,32> newFields{};
        for(auto& field:newFields)
            Check(mod::AssignOwnedAiMarker(reinterpret_cast<std::uintptr_t>(&field),mod::kOwnedSpawnerTag,intern,set),
                "Corrected marker assignment failed");
        Check(references()==33 && frees()==0,"Corrected assignments did not retain 32 field references and the user reference");
        for(auto& field:newFields)
            Check(mod::AssignOwnedAiMarker(reinterpret_cast<std::uintptr_t>(&field),mod::kOwnedSpawnerTag,intern,set),
                "Repeated corrected assignment failed");
        Check(references()==33,"Repeated field assignment leaked or discarded a reference");
        for(auto& field:newFields) {
            set(reinterpret_cast<std::uintptr_t>(&field),0);
            Check(frees()==0,"Corrected path freed storage while user-1 still owns it");
        }
        Check(references()==1,"Corrected field cleanup did not leave exactly the engine-user reference");
        for(unsigned cycle=0;cycle<16;++cycle) {
            for(auto& field:newFields)
                Check(mod::AssignOwnedAiMarker(reinterpret_cast<std::uintptr_t>(&field),mod::kOwnedSpawnerTag,intern,set),
                    "Replacement marker assignment failed");
            Check(references()==33,"Replacement squad has incorrect field ownership");
            for(auto& field:newFields)set(reinterpret_cast<std::uintptr_t>(&field),0);
            Check(references()==1 && frees()==0,"Replacement cleanup damaged shared storage");
        }
        // Assignment failure must not overwrite an existing field.
        std::uintptr_t changed=0;
        Check(!mod::AssignOwnedAiMarker(1,mod::kOwnedSpawnerTag,[](const char*){return 0u;},[&](auto,auto){++changed;}) && !changed,
            "Failed interning wrote a marker field");
        std::cout<<"{\"probe\":\"native shared ownership-label lifetime\","
            "\"repeated_user1_lookup_acquires_temporary_reference\":false,"
            "\"old_32_field_reference_count\":1,\"old_first_cleanup_frees_live_shared_slot\":true,"
            "\"old_remaining_stale_fields\":31,\"reused_dobj_pointer_decrement\":2,"
            "\"reused_vm_archive_pointer_decrement\":1,\"fixed_32_field_reference_count\":33,"
            "\"healthy_native_bone_hierarchy_passed\":true,\"old_reused_model_native_fault_rva\":\"0x29967d\","
            "\"fixed_32_field_cleanup_premature_frees\":0,\"fixed_remaining_engine_user_references\":1,"
            "\"fixed_repeated_field_assignments_balanced\":true,\"fixed_replacement_cycles\":16,"
            "\"native_hash_user_mask_setter_and_release_unchanged\":true,"
            "\"substituted_dependencies\":[\"single-thread locks\",\"pool allocator/free recorder\"],"
            "\"scope\":\"Private Linux MS-ABI reference/pool-reuse fixture. Campaign scripts, Windows gameplay and full checkpoint streams are not executed.\"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
