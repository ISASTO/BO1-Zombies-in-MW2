// Execute the Test38 native builtin-call sequence against exact retail code
// in an isolated Linux MS-ABI image: native Scr_AddInt/Scr_AddFloat pushes,
// the native parameter readers, exact builtins (min, max, gettime) and the
// native zero-object notify cleanup. No MW2 startup, imports or game callbacks.
#include "script_call.hpp"
#include "native_hooks.hpp"
#include "zombie_bindings.hpp"
#include <sys/mman.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t kSize=0x5000000,kStack=0x4f00000;
constexpr std::uintptr_t kScrGetFloatRva=0x222ee0,kScrGetIntRva=0x2230d0,kScrGetNumParamRva=0x223180;
constexpr std::uintptr_t kBuiltinMaxRva=0x189d80,kBuiltinGetTimeRva=0x185910,kLevelTimeRva=0xd96ec8;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::uint8_t* image=nullptr;
using GetFloat=float(MOD_NATIVE_ABI*)(unsigned);
using GetInt=int(MOD_NATIVE_ABI*)(unsigned);
using GetCount=unsigned(MOD_NATIVE_ABI*)();
struct Observed {unsigned count=0;float first=0,second=0;int third=0;bool called=false;} observed;
// Stand-in builtins that read their parameters with the exact native readers.
void MOD_NATIVE_ABI ReadOnlyBuiltin(unsigned entref) {
    observed.called=true;observed.count=reinterpret_cast<GetCount>(image+kScrGetNumParamRva)();
    observed.first=reinterpret_cast<GetFloat>(image+kScrGetFloatRva)(0);
    observed.second=reinterpret_cast<GetFloat>(image+kScrGetFloatRva)(1);
    observed.third=reinterpret_cast<GetInt>(image+kScrGetIntRva)(2);
    (void)entref;
}
// Stand-in for Scr_ExecEntThreadNum with VM_Execute's parameter accounting
// (0x224700: params counted in inparamcount are consumed, the return value is
// left on the stack with inparamcount = remaining + 1; 0x22274f..0x22278b then
// pops it and decrements inparamcount). Records what the thread received.
volatile mod::NativeScriptParameters* liveParameters=nullptr;
struct ThreadObserved {int entnum=-1;unsigned classnum=99,count=0;int handle=0;int args[4]{};unsigned freed=0;std::uint16_t id=0;bool leak=false;} thread;
std::uint16_t MOD_NATIVE_ABI ExecEntThreadStandIn(int entnum,unsigned classnum,int handle,unsigned count) {
    auto& p=*liveParameters;
    thread.entnum=entnum;thread.classnum=classnum;thread.handle=handle;thread.count=count;
    auto* top=reinterpret_cast<mod::ScriptValue*>(p.top);
    for(unsigned n=0;n<count && n<4;++n) {int v=0;std::memcpy(&v,&top[-static_cast<int>(n)].raw,4);thread.args[n]=v;}  // arg 0 is on top
    const unsigned remaining=p.incoming-count;
    auto* start=top-count;                       // VM_Execute: startTop
    start[1]={0,0,0};                            // return value (undefined) above it
    p.top=reinterpret_cast<std::uintptr_t>(start+1);p.incoming=remaining+1;
    if(!thread.leak) {p.top-=16;--p.incoming;}   // Scr_ExecEntThreadNum pops the return value
    return thread.id=static_cast<std::uint16_t>(0x4000+handle);
}
void MOD_NATIVE_ABI FreeThreadStandIn(std::uint16_t id) {if(id==thread.id)++thread.freed;}
void MOD_NATIVE_ABI SubtractBuiltin(unsigned) {
    const float a=reinterpret_cast<GetFloat>(image+kScrGetFloatRva)(0),b=reinterpret_cast<GetFloat>(image+kScrGetFloatRva)(1);
    reinterpret_cast<void(MOD_NATIVE_ABI*)(float)>(image+mod::kScrAddFloatRva)(a-b);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x18fcb90 && bytes.size()<kStack,"Invalid private image");
        image=static_cast<std::uint8_t*>(mmap(nullptr,kSize,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        Check(image!=MAP_FAILED,"mmap failed");
        struct Release {void* memory;~Release(){munmap(memory,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        auto& parameters=*reinterpret_cast<volatile mod::NativeScriptParameters*>(image+mod::kScriptParametersRva);
        auto* stack=reinterpret_cast<mod::ScriptValue*>(image+kStack);
        mod::ScriptCallNatives natives;
        natives.addInt=reinterpret_cast<decltype(natives.addInt)>(image+mod::kScrAddIntRva);
        natives.addFloat=reinterpret_cast<decltype(natives.addFloat)>(image+mod::kScrAddFloatRva);
        natives.addString=reinterpret_cast<decltype(natives.addString)>(image+mod::kScrAddStringRva);
        natives.addVector=reinterpret_cast<decltype(natives.addVector)>(image+mod::kScrAddVectorRva);
        natives.addEntityNum=reinterpret_cast<decltype(natives.addEntityNum)>(image+mod::kScrAddEntityNumRva);
        natives.addUndefined=reinterpret_cast<decltype(natives.addUndefined)>(image+mod::kScrAddUndefinedRva);
        natives.notify=reinterpret_cast<decltype(natives.notify)>(image+mod::kScriptNotifyRva);
        const auto empty=[&]() {
            for(int n=0;n<16;++n)stack[n]={0xdeadbeef,0,0};
            stack[0].type=7; // native idle sentinel (codepos)
            parameters.top=reinterpret_cast<std::uintptr_t>(stack);parameters.incoming=0;parameters.outgoing=0;
        };
        const auto balanced=[&]() {return parameters.top==reinterpret_cast<std::uintptr_t>(stack) && !parameters.incoming && !parameters.outgoing;};
        using Arg=mod::ScriptArg;
        // 1. Argument order and count through the exact native readers.
        empty();observed={};
        auto result=mod::CallScriptBuiltin(parameters,natives,&ReadOnlyBuiltin,0,{Arg::Float(1.5f),Arg::Float(-4.25f),Arg::Int(42)});
        Check(observed.called && observed.count==3 && observed.first==1.5f && observed.second==-4.25f && observed.third==42,
            "Native readers did not see arguments in script order");
        Check(result==mod::ScriptCallResult::Ok && balanced(),"Builtin without a return value left parameters on the stack");
        // 2. A returned value is visible, then released by the native cleanup.
        empty();mod::ScriptReturn ret;
        result=mod::CallScriptBuiltin(parameters,natives,&SubtractBuiltin,0,{Arg::Float(10.f),Arg::Int(3)},&ret);
        Check(result==mod::ScriptCallResult::Ok && balanced() && ret.present && ret.value.type==mod::kScriptFloat && ret.asFloat()==7.f,
            "Native return value was not first-minus-second or was not released");
        // 3. Exact retail builtins.
        empty();
        result=mod::CallScriptBuiltin(parameters,natives,reinterpret_cast<mod::ScriptBuiltin>(image+kBuiltinMaxRva),0,{Arg::Float(-2.f),Arg::Int(9)},&ret);
        Check(result==mod::ScriptCallResult::Ok && balanced() && ret.present && ret.asFloat()==9.f,"Exact native max() returned the wrong value");
        empty();
        result=mod::CallScriptBuiltin(parameters,natives,reinterpret_cast<mod::ScriptBuiltin>(image+mod::kBuiltinMin.rva),0,{Arg::Float(3.5f),Arg::Float(-2.f)},&ret);
        Check(result==mod::ScriptCallResult::Ok && balanced() && ret.present && ret.asFloat()==-2.f,"Exact native min() returned the wrong value");
        *reinterpret_cast<int*>(image+kLevelTimeRva)=123456;empty();
        result=mod::CallScriptBuiltin(parameters,natives,reinterpret_cast<mod::ScriptBuiltin>(image+kBuiltinGetTimeRva),0,{},&ret);
        Check(result==mod::ScriptCallResult::Ok && balanced() && ret.present && ret.value.type==mod::kScriptInteger && ret.asInt()==123456,
            "Exact native gettime() returned the wrong value");
        // 4. A busy VM (owned parameters) is never touched.
        empty();parameters.incoming=1;parameters.top=reinterpret_cast<std::uintptr_t>(stack+1);
        observed={};
        result=mod::CallScriptBuiltin(parameters,natives,&ReadOnlyBuiltin,0,{Arg::Int(1)});
        Check(result==mod::ScriptCallResult::Busy && !observed.called && parameters.incoming==1 && parameters.top==reinterpret_cast<std::uintptr_t>(stack+1),
            "Builtin call ran inside a VM that owned parameters");
        empty();parameters.outgoing=2;
        Check(mod::CallScriptBuiltin(parameters,natives,&ReadOnlyBuiltin,0,{})==mod::ScriptCallResult::Busy && parameters.outgoing==2,
            "Builtin call ran while another builtin's parameters were live");
        // 5. Invalid requests are refused before any push.
        empty();
        Check(mod::CallScriptBuiltin(parameters,natives,nullptr,0,{Arg::Int(1)})==mod::ScriptCallResult::Invalid && balanced(),"Null builtin accepted");
        Check(mod::CallScriptBuiltin(parameters,natives,&ReadOnlyBuiltin,0,{Arg::String(nullptr)})==mod::ScriptCallResult::Invalid && balanced(),"Null string accepted");
        // 6. Repeated calls keep the stack balanced.
        empty();
        for(int n=0;n<1000;++n) {
            result=mod::CallScriptBuiltin(parameters,natives,reinterpret_cast<mod::ScriptBuiltin>(image+kBuiltinMaxRva),0,{Arg::Int(n),Arg::Float(.5f)},&ret);
            Check(result==mod::ScriptCallResult::Ok && balanced() && ret.asFloat()==std::max(static_cast<float>(n),.5f),"Repeated native calls drifted");
        }
        // 7. Script threads on an entity (Test39 zombie setup): arguments pushed
        // with the exact Scr_AddInt, received in script order, thread freed,
        // stack restored; a thread that leaves a value is reported.
        liveParameters=&parameters;
        empty();thread={};
        result=mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,37,0x1234,{});
        Check(result==mod::ScriptCallResult::Ok && balanced() && thread.entnum==37 && thread.classnum==0 && thread.handle==0x1234 && thread.count==0 && thread.freed==1,
            "Thread without arguments not run/freed on its entity");
        empty();thread={};
        result=mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,5,77,{Arg::Int(0),Arg::Int(9)});
        Check(result==mod::ScriptCallResult::Ok && balanced() && thread.count==2 && thread.args[0]==0 && thread.args[1]==9 && thread.freed==1,
            "Thread arguments not in script order or stack not restored");
        empty();thread={};thread.leak=true;
        result=mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,5,77,{});
        Check(result==mod::ScriptCallResult::Unbalanced && !parameters.incoming,"A thread that left a value was not reported and released");
        empty();parameters.outgoing=1;thread={};
        Check(mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,5,77,{})==mod::ScriptCallResult::Busy && thread.entnum==-1,
            "Thread started while builtin parameters were live");
        empty();
        Check(mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,5,0,{})==mod::ScriptCallResult::Invalid
            && mod::CallScriptThread(parameters,natives,&ExecEntThreadStandIn,&FreeThreadStandIn,0x10000,5,{})==mod::ScriptCallResult::Invalid && balanced(),
            "Thread with no handle or a bad entity accepted");
        std::cout<<"{\"probe\":\"exact-native builtin call\",\"argument_order_native_readers\":true,"
            "\"returned_value_read_then_released\":true,\"exact_min_max_gettime\":true,\"busy_vm_untouched\":true,"
            "\"invalid_requests_refused\":true,\"repeated_calls_balanced\":1000,\"native_dependencies_substituted\":false,"
            "\"thread_calls_balanced_with_vm_execute_accounting\":true,"
            "\"scope\":\"Scr_AddInt/Scr_AddFloat, Scr_GetNumParam/Scr_GetFloat/Scr_GetInt, exact min/max/gettime and the zero-object notify cleanup. "
            "String, vector and entity pushes need the live script string/memory tables and are exercised only in game. "
            "Thread calls use a stand-in for Scr_ExecEntThreadNum that applies VM_Execute's verified parameter accounting; "
            "running a compiled thread needs the live VM and is exercised only in game.\"}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
