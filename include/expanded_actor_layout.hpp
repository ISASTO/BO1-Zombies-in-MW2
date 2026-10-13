#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace mod::expansion {
// Experimental layout used by the isolated native prototype, not the DLL.
// Append perception rather than inserting it: all original brain field
// offsets, including goals, animation state and active flags, stay unchanged.
inline constexpr std::size_t kActors=256,kSentients=274,kCorpses=64;
inline constexpr std::size_t kOldStride=0x1560,kPerceptionStart=kOldStride;
inline constexpr std::size_t kRecordBytes=48,kStride=kPerceptionStart+kSentients*kRecordBytes;
inline constexpr std::size_t kSentientStride=0x88;
static_assert(kStride==0x48c0 && kStride%16==0);
inline std::optional<std::size_t> PerceptionOffset(std::size_t sentient) {
    if(sentient>=kSentients)return std::nullopt;
    return kPerceptionStart+sentient*kRecordBytes;
}
inline std::optional<std::uint16_t> SnapshotIndex(bool corpse,std::size_t index) {
    if(index>=(corpse?kCorpses:kActors))return std::nullopt;
    return static_cast<std::uint16_t>(index+(corpse?kActors:0));
}
struct SaveField {std::uint64_t offset=0;std::uint32_t kind=0,reserved=0;};
static_assert(sizeof(SaveField)==16);
inline std::vector<SaveField> ExpandedSaveFields(const std::vector<SaveField>& original) {
    std::vector<SaveField> out;
    for(const auto& f:original) {
        if(!f.kind)break;
        const bool perception=f.kind==14 && f.offset>=0x370 && f.offset<0xca8 && (f.offset-0x370)%48==0;
        if(!perception)out.push_back(f);
    }
    for(std::size_t n=0;n<kSentients;++n)out.push_back({kPerceptionStart+n*kRecordBytes+40,14,0});
    out.push_back({});return out;
}
} // namespace mod::expansion
