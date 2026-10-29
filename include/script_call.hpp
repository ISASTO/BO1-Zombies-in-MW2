#pragma once
#include "script_parameters.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#if defined(_WIN32)
#define MOD_NATIVE_ABI
#else
#define MOD_NATIVE_ABI __attribute__((ms_abi))
#endif

namespace mod {
// Calls one exact-retail script builtin from native code, the way the VM's
// builtin-call opcode does, inside an idle NativeScriptWorkFrame:
//   1. arguments are pushed last-to-first with the native Scr_Add* functions
//      (each pushes one value and counts it in the "incoming" counter),
//   2. those values become the builtin's parameters (the "outgoing" counter
//      that Scr_GetNumParam/Scr_Get* read, index 0 = first argument),
//   3. the builtin runs; a return value is pushed with Scr_Add* (which first
//      releases the parameters),
//   4. the exact native zero-object notify releases whatever remains.
// Only call builtins with arguments already validated by the caller: a
// native script error outside a running script is fatal for the level.
struct ScriptCallNatives {
    void (MOD_NATIVE_ABI* addInt)(int)=nullptr;
    void (MOD_NATIVE_ABI* addFloat)(float)=nullptr;
    void (MOD_NATIVE_ABI* addString)(const char*)=nullptr;
    void (MOD_NATIVE_ABI* addVector)(const float*)=nullptr;
    void (MOD_NATIVE_ABI* addEntityNum)(unsigned,unsigned)=nullptr;
    void (MOD_NATIVE_ABI* addUndefined)()=nullptr;
    void (MOD_NATIVE_ABI* notify)(unsigned,unsigned,unsigned)=nullptr;
    // Scr_AddAnim: pushes a VAR_ANIMATION (13) value (tree index << 16 | anim
    // index). Optional: only animation arguments need it.
    void (MOD_NATIVE_ABI* addAnim)(std::uint32_t)=nullptr;
    bool complete() const { return addInt && addFloat && addString && addVector && addEntityNum && addUndefined && notify; }
};
using ScriptBuiltin=void(MOD_NATIVE_ABI*)(unsigned);
struct ScriptArg {
    enum class Kind { Int, Float, String, Vector, Entity, Undefined, Anim } kind=Kind::Undefined;
    int integer=0; float number=0.f; const char* text=nullptr; std::array<float,3> vector{}; unsigned entity=0;
    static ScriptArg Int(int v) { ScriptArg a; a.kind=Kind::Int; a.integer=v; return a; }
    static ScriptArg Float(float v) { ScriptArg a; a.kind=Kind::Float; a.number=v; return a; }
    static ScriptArg String(const char* v) { ScriptArg a; a.kind=Kind::String; a.text=v; return a; }
    static ScriptArg Vector(const std::array<float,3>& v) { ScriptArg a; a.kind=Kind::Vector; a.vector=v; return a; }
    static ScriptArg Entity(unsigned number) { ScriptArg a; a.kind=Kind::Entity; a.entity=number; return a; }
    static ScriptArg Anim(std::uint32_t value) { ScriptArg a; a.kind=Kind::Anim; a.entity=value; return a; }
};
// Native VariableValue: 8-byte union then the 32-bit type.
struct ScriptValue { std::uint64_t raw=0; std::uint32_t type=0,padding=0; };
static_assert(sizeof(ScriptValue)==16);
inline constexpr std::uint32_t kScriptUndefined=0,kScriptString=2,kScriptVector=4,kScriptFloat=5,kScriptInteger=6,kScriptAnimation=13;
struct ScriptReturn {
    bool present=false; ScriptValue value;
    // Test45: a returned vector (gettagorigin), copied before the value is
    // released (the VM's vector value is a pointer into its vector store).
    bool hasVector=false; std::array<float,3> vector{};
    float asFloat() const { float f=0.f; std::memcpy(&f,&value.raw,4); return value.type==kScriptInteger?static_cast<float>(asInt()):f; }
    int asInt() const { int i=0; std::memcpy(&i,&value.raw,4); return i; }
};
enum class ScriptCallResult { Ok, Busy, Invalid, Unbalanced };
inline const char* ScriptCallText(ScriptCallResult r) {
    switch(r) {
        case ScriptCallResult::Ok: return "OK";
        case ScriptCallResult::Busy: return "VM_BUSY";
        case ScriptCallResult::Invalid: return "INVALID";
        case ScriptCallResult::Unbalanced: return "UNBALANCED";
    }
    return "UNKNOWN";
}
inline constexpr unsigned kMaxScriptCallArgs=8;
inline unsigned ScriptEntityRef(unsigned number) { return number&0xffffu; } // class 0 = entity

inline ScriptCallResult CallScriptBuiltin(volatile NativeScriptParameters& p,const ScriptCallNatives& n,ScriptBuiltin builtin,
    unsigned entref,std::initializer_list<ScriptArg> args,ScriptReturn* out=nullptr) {
    if(out) *out={};
    if(!builtin || !n.complete() || args.size()>kMaxScriptCallArgs) return ScriptCallResult::Invalid;
    for(const auto& a:args) {
        if(a.kind==ScriptArg::Kind::String && !a.text) return ScriptCallResult::Invalid;
        if(a.kind==ScriptArg::Kind::Anim && !n.addAnim) return ScriptCallResult::Invalid;
    }
    if(p.incoming || p.outgoing || !p.top || (p.top&15)) return ScriptCallResult::Busy;
    const auto top=p.top;
    for(auto it=args.end();it!=args.begin();) {
        --it;
        switch(it->kind) {
            case ScriptArg::Kind::Int: n.addInt(it->integer); break;
            case ScriptArg::Kind::Float: n.addFloat(it->number); break;
            case ScriptArg::Kind::String: n.addString(it->text); break;
            case ScriptArg::Kind::Vector: n.addVector(it->vector.data()); break;
            case ScriptArg::Kind::Entity: n.addEntityNum(it->entity,0); break;
            case ScriptArg::Kind::Undefined: n.addUndefined(); break;
            case ScriptArg::Kind::Anim: n.addAnim(it->entity); break;
        }
    }
    if(p.incoming!=args.size() || p.top!=top+16*args.size()) {
        n.notify(0,0,p.incoming);
        return ScriptCallResult::Unbalanced;
    }
    p.outgoing=p.incoming; p.incoming=0;
    builtin(entref);
    if(out && p.incoming && p.top) {
        out->present=true; std::memcpy(&out->value,reinterpret_cast<const void*>(p.top),sizeof(ScriptValue));
        if(out->value.type==kScriptVector && out->value.raw) {
            std::memcpy(out->vector.data(),reinterpret_cast<const void*>(static_cast<std::uintptr_t>(out->value.raw)),sizeof(out->vector));
            out->hasVector=true;
        }
    }
    n.notify(0,0,p.incoming);
    return !p.incoming && !p.outgoing && p.top==top?ScriptCallResult::Ok:ScriptCallResult::Unbalanced;
}

// Runs a compiled script function as a thread on an entity, the way the
// engine runs callbacks (e.g. animscripted -> animscripts\scripted::init):
// arguments pushed last-to-first with Scr_Add*, Scr_ExecEntThreadNum(entnum,
// class 0, handle, count) executes until the thread waits or ends and pops its
// return value, Scr_FreeThread releases the thread id. Afterwards the stack
// and both counters must be exactly as before.
using ScriptExecEntThread=std::uint16_t(MOD_NATIVE_ABI*)(int,unsigned,int,unsigned);
using ScriptFreeThread=void(MOD_NATIVE_ABI*)(std::uint16_t);
inline ScriptCallResult CallScriptThread(volatile NativeScriptParameters& p,const ScriptCallNatives& n,ScriptExecEntThread exec,
    ScriptFreeThread release,unsigned entnum,int handle,std::initializer_list<ScriptArg> args) {
    if(!exec || !release || handle<=0 || entnum>0xffffu || !n.complete() || args.size()>kMaxScriptCallArgs) return ScriptCallResult::Invalid;
    for(const auto& a:args) {
        if(a.kind==ScriptArg::Kind::String && !a.text) return ScriptCallResult::Invalid;
        if(a.kind==ScriptArg::Kind::Anim && !n.addAnim) return ScriptCallResult::Invalid;
    }
    if(p.incoming || p.outgoing || !p.top || (p.top&15)) return ScriptCallResult::Busy;
    const auto top=p.top;
    for(auto it=args.end();it!=args.begin();) {
        --it;
        switch(it->kind) {
            case ScriptArg::Kind::Int: n.addInt(it->integer); break;
            case ScriptArg::Kind::Float: n.addFloat(it->number); break;
            case ScriptArg::Kind::String: n.addString(it->text); break;
            case ScriptArg::Kind::Vector: n.addVector(it->vector.data()); break;
            case ScriptArg::Kind::Entity: n.addEntityNum(it->entity,0); break;
            case ScriptArg::Kind::Undefined: n.addUndefined(); break;
            case ScriptArg::Kind::Anim: n.addAnim(it->entity); break;
        }
    }
    if(p.incoming!=args.size() || p.top!=top+16*args.size()) {
        n.notify(0,0,p.incoming);
        return ScriptCallResult::Unbalanced;
    }
    const auto thread=exec(static_cast<int>(entnum),0,handle,static_cast<unsigned>(args.size()));
    release(thread);
    if(p.incoming || p.outgoing || p.top!=top) {
        if(p.incoming) n.notify(0,0,p.incoming);
        return ScriptCallResult::Unbalanced;
    }
    return ScriptCallResult::Ok;
}
} // namespace mod
