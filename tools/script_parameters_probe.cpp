// Execute only exact-retail Scr_Notify and its integer/vector reference cleanup
// in an isolated Linux MS-ABI image. No MW2 startup, imports or game callbacks.
#include "script_parameters.hpp"
#include "native_hooks.hpp"
#include <sys/mman.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t kSize=0x5000000,kStack=0x4f00000,kVector=0x4f01004;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Value {std::uintptr_t data;std::uint32_t type,padding;};
static_assert(sizeof(Value)==16);
using Notify=void(__attribute__((ms_abi))*)(unsigned,unsigned,unsigned);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x18fcb90 && bytes.size()<kStack,"Invalid private image");
        auto* image=static_cast<std::uint8_t*>(mmap(nullptr,kSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        struct Release {void* memory;~Release(){munmap(memory,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        auto& parameters=*reinterpret_cast<mod::NativeScriptParameters*>(image+mod::kScriptParametersRva);
        auto* stack=reinterpret_cast<Value*>(image+kStack);
        auto& refs=*reinterpret_cast<std::uint16_t*>(image+kVector-4);
        const auto notify=reinterpret_cast<Notify>(image+mod::kScriptNotifyRva);
        const auto reset=[&](unsigned incoming=0) {
            for(int n=0;n<8;++n)stack[n]={static_cast<unsigned>(n),6,0};
            refs=3;image[kVector-1]=0;
            stack[5]={reinterpret_cast<std::uintptr_t>(image+kVector),4,0};
            parameters={reinterpret_cast<std::uintptr_t>(stack+6),incoming,3};
        };
        reset();notify(0,0,0);
        Check(parameters.top==reinterpret_cast<std::uintptr_t>(stack+3) && parameters.outgoing==0 && refs==2,
            "Exact native notification did not reproduce caller-parameter consumption");
        for(unsigned incoming:{0u,2u}) {
            reset(incoming);const auto original=parameters;
            {
                mod::NativeScriptParameterFrame frame(parameters);Check(frame.valid(),"Parameter frame refused valid native values");
                notify(0,0,0);
                Check(parameters.top==original.top && !parameters.incoming && !parameters.outgoing && refs==3,
                    "Isolated native notification consumed caller values or references");
                {
                    mod::NativeScriptParameterFrame nested(parameters);notify(0,0,0);
                    Check(nested.close(),"Nested native operation did not balance its parameter frame");
                }
                Check(frame.close(),"Native operation did not restore the caller's parameter frame");
            }
            Check(parameters.top==original.top && parameters.incoming==incoming && parameters.outgoing==3 && refs==3,
                "Caller parameters were not restored exactly");
            parameters.incoming=0;notify(0,0,0);
            Check(parameters.top==reinterpret_cast<std::uintptr_t>(stack+3) && refs==2,"The caller cannot subsequently release its own parameters");
        }
        reset();
        {mod::NativeScriptParameterFrame frame(parameters);parameters={reinterpret_cast<std::uintptr_t>(stack),0,0};frame.abandon();}
        Check(parameters.top==reinterpret_cast<std::uintptr_t>(stack) && !parameters.outgoing,"Checkpoint reset was overwritten by an old parameter frame");
        std::cout<<"{\"probe\":\"exact-native script parameter isolation\","
            "\"unprotected_caller_parameters_consumed\":3,\"unprotected_vector_reference_decremented\":true,"
            "\"isolated_caller_stack_and_references_preserved\":true,\"nested_parameters_restored\":true,"
            "\"checkpoint_reset_preserved\":true,\"native_dependencies_substituted\":false,"
            "\"scope\":\"Scr_Notify with no notification object, and native scalar/vector reference cleanup; no campaign scripts or Windows gameplay.\"}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
