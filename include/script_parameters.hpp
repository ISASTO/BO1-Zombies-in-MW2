#pragma once
#include <cstdint>

namespace mod {
// Exact native VM layout: top followed by incoming and outgoing counts.
// Scr_Notify consumes outgoing parameters even when its own argument count is
// zero. A hidden native operation inside another builtin must isolate counts,
// leaving the caller's stack values and references in place.
struct NativeScriptParameters {
    std::uintptr_t top=0;
    std::uint32_t incoming=0,outgoing=0;
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
