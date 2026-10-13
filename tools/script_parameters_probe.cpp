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
        // Reproduce the external-work contract with the exact native cleanup:
        // a parked VM may have a stale public top while it owns no parameters.
        // Callback frame writes and ignored return values must stay private.
        reset();parameters.incoming=parameters.outgoing=0;
        const auto parkedParameters=parameters;
        mod::NativeScriptVmState state{0x12345678,19,2,reinterpret_cast<std::uintptr_t>(stack+3),reinterpret_cast<std::uintptr_t>(stack+1)};
        const auto parkedState=state;
        std::array<std::uint8_t,40> frameBytes{};frameBytes.fill(0x5a);
        const auto parkedFrameBytes=frameBytes;
        const auto stackSentinel=stack[0];
        std::uintptr_t framePointer=reinterpret_cast<std::uintptr_t>(frameBytes.data()+8);
        const auto parkedFrame=framePointer;
        std::uintptr_t declarations=0x12345800;const auto parkedDeclarations=declarations;
        mod::NativeScriptWorkFrame work(parameters,state,framePointer,0,-1,reinterpret_cast<std::uintptr_t>(stack),
            reinterpret_cast<std::uintptr_t>(frameBytes.data()),frameBytes.data(),reinterpret_cast<std::uint8_t*>(stack),&declarations,0x12345000);
        Check(work.valid() && parameters.top==reinterpret_cast<std::uintptr_t>(stack) && state.top==state.startTop,
            "External native work did not enter an empty stack/frame context");
        Check(frameBytes==std::array<std::uint8_t,40>{} && stack[0].data==0 && stack[0].type==7 && stack[0].padding==0,
            "Parked frame/sentinel data leaked into the initial work context");
        frameBytes.fill(0xab);state={0x98765432,7,8,reinterpret_cast<std::uintptr_t>(stack+2),reinterpret_cast<std::uintptr_t>(stack)};
        declarations=0x12345080;
        ++refs;stack[1]={reinterpret_cast<std::uintptr_t>(image+kVector),4,0};
        parameters.top=reinterpret_cast<std::uintptr_t>(stack+1);parameters.incoming=1;
        notify(0,0,parameters.incoming);
        Check(refs==3 && work.close(0,-1),"Ignored native return value was not released before context restoration");
        Check(parameters.top==parkedParameters.top && framePointer==parkedFrame && declarations==parkedDeclarations && std::memcmp(&state,&parkedState,sizeof(state))==0
            && frameBytes==parkedFrameBytes && std::memcmp(stack,&stackSentinel,sizeof(Value))==0,
            "External work leaked its VM frame, stack or local state into the parked campaign context");
        const auto parkedDepth=(state.top-state.startTop)/16;
        Check(parkedDepth==2,"Restored campaign stack would underflow when archived");
        mod::NativeScriptWorkFrame busy(parameters,state,framePointer,1,-1,reinterpret_cast<std::uintptr_t>(stack),parkedFrame);
        Check(!busy.valid() && parameters.top==parkedParameters.top,"Active script frame must defer external AI work");
        std::cout<<"{\"probe\":\"exact-native script parameter isolation\","
            "\"unprotected_caller_parameters_consumed\":3,\"unprotected_vector_reference_decremented\":true,"
            "\"isolated_caller_stack_and_references_preserved\":true,\"nested_parameters_restored\":true,"
            "\"checkpoint_reset_preserved\":true,\"native_dependencies_substituted\":false,"
            "\"external_vm_context_restored\":true,\"ignored_native_return_reference_released\":true,\"active_script_work_deferred\":true,"
            "\"scope\":\"Scr_Notify with no notification object, and native scalar/vector reference cleanup; no campaign scripts or Windows gameplay.\"}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
