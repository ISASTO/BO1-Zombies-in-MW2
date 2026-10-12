#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace mod {
// Exact SHA256-gated retail bindings. These are independently checked by
// tools/verify_combat_bindings.py before packaging; no historical SP offsets.
inline constexpr std::uintptr_t kFrameClockCallRva=0x2344b8,kFrameClockRva=0x174d00;
inline constexpr std::uintptr_t kSaveReadCallRva=0x17f3f3,kSaveReadRva=0x17f8e0;
inline constexpr std::uintptr_t kSaveWriteCallRva=0x181b45,kSaveWriteRva=0x180cf0;
inline constexpr std::uintptr_t kActorInitRva=0x12ce20,kDamageRva=0x167d30,kDObjUpdateRva=0x19ee10;
inline constexpr std::size_t kAiModelIndex=0x284,kSentientIgnoreMe=0x14,kSentientIgnoreAll=0x15;
inline constexpr std::size_t kAiAttachmentModels=0x208,kAiAttachmentTags=0x246,kAiAttachmentFlags=0x148;
inline constexpr std::size_t kAiAttachmentCount=31;
inline constexpr std::size_t kAiEnemySightValid=0x309,kAiEnemySightPosition=0x30c;
struct AiAppearance {
    std::uint16_t model=0;
    std::array<std::uint16_t,kAiAttachmentCount> models{},tags{};
    std::array<std::string,kAiAttachmentCount> tagNames{};
    std::uint32_t flags=0;
};
inline constexpr const char* kFriendlyCharacters[]={"Soldier","Ghost","Price","Soap","Roach"};
inline constexpr int kFriendlyCharacterCount=5;
inline constexpr std::array<std::uint8_t,15> kActorInitPrologue={
    0x40,0x56,0x57,0x41,0x57,0x48,0x81,0xec,0xa0,0,0,0,0x45,0x33,0xff};
inline std::array<std::uint8_t,14> AbsoluteJump(std::uintptr_t target) {
    std::array<std::uint8_t,14> code{0xff,0x25,0,0,0,0};
    std::memcpy(code.data()+6,&target,8); return code;
}
inline bool RelativeCall(std::uintptr_t at,std::uintptr_t target,std::array<std::uint8_t,5>& out) {
    const auto delta=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(at+5);
    if(delta<std::numeric_limits<std::int32_t>::min() || delta>std::numeric_limits<std::int32_t>::max()) return false;
    out[0]=0xe8; const auto rel=static_cast<std::int32_t>(delta);std::memcpy(out.data()+1,&rel,4);return true;
}
} // namespace mod
