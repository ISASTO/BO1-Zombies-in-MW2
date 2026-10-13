#pragma once
#include <cstdint>
#include <array>
#include <cstring>

namespace mod {
// Exact native VM layout: top followed by incoming and outgoing counts.
// Scr_Notify consumes outgoing parameters even when its own argument count is
// zero. A hidden native operation inside another builtin must isolate counts,
// leaving the caller's stack values and references in place.
struct NativeScriptParameters {
    std::uintptr_t top=0;
    std::uint32_t incoming=0,outgoing=0;
};
struct NativeScriptVmState {
    std::uintptr_t code=0;
    std::uint32_t localId=0,localCount=0;
    std::uintptr_t top=0,startTop=0;
};
static_assert(sizeof(NativeScriptVmState)==32);
// External native work must enter with the native idle stack/frame base.
// Parameter counts alone do not identify an empty VM context: native APIs can
// leave a previous callback's top/frame behind when no parameters are owned.
class NativeScriptWorkFrame {
    volatile NativeScriptParameters& parameters;
    NativeScriptVmState& state;
    volatile std::uintptr_t& framePointer;
    NativeScriptParameters savedParameters{};
    NativeScriptVmState savedState{};
    std::uintptr_t savedFrame=0,stackBase=0;
    volatile std::uintptr_t* declarations=nullptr;
    std::uintptr_t savedDeclarations=0;
    std::uint8_t* frameData=nullptr;
    std::uint8_t* stackData=nullptr;
    std::array<std::uint8_t,40> savedFrameData{};
    std::array<std::uint8_t,16> savedStackData{};
    bool active=false;
public:
    NativeScriptWorkFrame(volatile NativeScriptParameters& p,NativeScriptVmState& s,
        volatile std::uintptr_t& frame,int functionCount,int errorLevel,std::uintptr_t stack,std::uintptr_t frameBase,
        std::uint8_t* initialFrame=nullptr,std::uint8_t* initialStack=nullptr,
        volatile std::uintptr_t* localDeclarations=nullptr,std::uintptr_t declarationBase=0)
        :parameters(p),state(s),framePointer(frame),stackBase(stack) {
        if(functionCount || errorLevel!=-1 || p.incoming || p.outgoing || !stack || !frameBase)return;
        savedParameters={p.top,p.incoming,p.outgoing};savedState=s;savedFrame=frame;
        declarations=localDeclarations;
        if(declarations) {savedDeclarations=*declarations;*declarations=declarationBase;}
        frameData=initialFrame;stackData=initialStack;
        if(frameData) {
            std::memcpy(savedFrameData.data(),frameData,savedFrameData.size());
            std::memset(frameData,0,savedFrameData.size());
        }
        if(stackData) {
            std::memcpy(savedStackData.data(),stackData,savedStackData.size());
            // Scr_Init sets the bottom value's native type to 7 (codepos).
            // Keep a parked callback's sentinel out of this idle context.
            std::memset(stackData,0,savedStackData.size());
            const std::uint32_t type=7;std::memcpy(stackData+8,&type,sizeof(type));
        }
        p.top=stack;frame=frameBase;s={0,0,0,stack,stack};active=true;
    }
    NativeScriptWorkFrame(const NativeScriptWorkFrame&)=delete;
    NativeScriptWorkFrame& operator=(const NativeScriptWorkFrame&)=delete;
    bool valid() const {return active;}
    void abandon() {active=false;}
    bool close(int functionCount,int errorLevel) {
        if(!active)return false;
        active=false;
        if(functionCount || errorLevel!=-1 || parameters.incoming || parameters.outgoing)return false;
        // With no owned parameters or active frames, top is only bookkeeping.
        // Restore the caller even if the final callback left a different top.
        if(frameData)std::memcpy(frameData,savedFrameData.data(),savedFrameData.size());
        if(stackData)std::memcpy(stackData,savedStackData.data(),savedStackData.size());
        if(declarations)*declarations=savedDeclarations;
        state=savedState;framePointer=savedFrame;parameters.top=savedParameters.top;return true;
    }
};
static_assert(sizeof(NativeScriptParameters)==16);
class NativeScriptParameterFrame {
    volatile NativeScriptParameters& live;
    NativeScriptParameters saved;
    bool active=false;
public:
    explicit NativeScriptParameterFrame(volatile NativeScriptParameters& parameters):live(parameters) {
        saved={live.top,live.incoming,live.outgoing};
        if(!saved.top || (saved.top&15) || saved.incoming>2048 || saved.outgoing>2048)return;
        live.incoming=0;live.outgoing=0;active=true;
    }
    NativeScriptParameterFrame(const NativeScriptParameterFrame&)=delete;
    NativeScriptParameterFrame& operator=(const NativeScriptParameterFrame&)=delete;
    ~NativeScriptParameterFrame() {close();}
    bool valid() const {return active;}
    const NativeScriptParameters& caller() const {return saved;}
    void abandon() {active=false;} // A checkpoint reset owns the replacement VM.
    bool close() {
        if(!active)return false;
        active=false;
        if(live.top!=saved.top || live.incoming || live.outgoing)return false;
        live.incoming=saved.incoming;live.outgoing=saved.outgoing;return true;
    }
};
} // namespace mod
