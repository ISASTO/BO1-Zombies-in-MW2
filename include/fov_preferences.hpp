#pragma once
#include <optional>
#include <string>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mod {
inline constexpr int kDefaultFov = 65; // Exact native registration default.
inline constexpr int kMinFov = 60;     // Deliberate menu subset of native 1..160.
inline constexpr int kMaxFov = 120;
inline constexpr int kFovStep = 5;
inline constexpr std::size_t kDvarFloatCurrent = 0x10;
struct NativeFovValue { bool valid=false; float current=0.f; };
template<class Reader>
NativeFovValue ReadNativeFovValue(std::uintptr_t dvar,Reader read) {
    NativeFovValue result;
    std::uint8_t type=255;
    if (!dvar || !read(dvar+0xc,&type,sizeof(type)) || type!=1
        || !read(dvar+kDvarFloatCurrent,&result.current,sizeof(result.current))
        || !std::isfinite(result.current) || result.current<1.f || result.current>160.f) return result;
    result.valid=true; return result;
}
inline bool ValidSelectedFov(int value) {
    return value >= kMinFov && value <= kMaxFov && value % kFovStep == 0;
}
inline std::optional<int> ParseFovPreference(const std::string& input) {
    if (input.size() > 128) return {};
    std::string text;
    for (std::size_t i=0; i<input.size(); ++i) {
        if (input[i]=='\r') {
            if (i+1>=input.size() || input[i+1]!='\n') return {};
        } else text.push_back(input[i]);
    }
    const std::string prefix="version=1\nfov=";
    if (text.compare(0,prefix.size(),prefix)!=0) return {};
    if (!text.empty() && text.back()=='\n') text.pop_back();
    const char* first=text.data()+prefix.size();
    const char* last=text.data()+text.size();
    if (first>=last) return {};
    for (const char* p=first;p!=last;++p) if (*p<'0' || *p>'9') return {};
    int value=0;
    const auto result=std::from_chars(first,last,value);
    if (result.ec!=std::errc{} || result.ptr!=last || !ValidSelectedFov(value)) return {};
    return value;
}
inline std::string SerializeFovPreference(int value) {
    if (!ValidSelectedFov(value)) return {};
    return "version=1\nfov="+std::to_string(value)+"\n";
}
inline bool FovWriteAllowed(bool supported,bool resolved,bool ready,float current,int desired) {
    return supported && resolved && ready && std::isfinite(current) && current>=1.f && current<=160.f
        && ValidSelectedFov(desired);
}
struct FovPreference {
    int selected = kDefaultFov;
    std::optional<int> saved;
    std::optional<int> desired;
    bool releaseAfterDefault = false;
    void load(std::optional<int> value) {
        saved = value && ValidSelectedFov(*value) ? value : std::nullopt;
        selected=saved.value_or(kDefaultFov); desired=saved; releaseAfterDefault=false;
    }
    bool step(int direction) {
        const int next=selected+(direction<0?-kFovStep:kFovStep);
        if (!ValidSelectedFov(next)) return false;
        selected=next; desired=next; releaseAfterDefault=false; return true;
    }
    void savedSuccessfully() { saved=selected; desired=selected; releaseAfterDefault=false; }
    void defaultSuccessfullyCleared() {
        saved.reset(); selected=kDefaultFov; desired=kDefaultFov; releaseAfterDefault=true;
    }
    void applied() {
        if (releaseAfterDefault) { desired.reset(); releaseAfterDefault=false; }
    }
};
} // namespace mod
