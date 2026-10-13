#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <array>
#include <cstring>

namespace mod::expansion {
// Experimental Test22 native layout; also exercised by isolated probes.
// Append perception rather than inserting it: all original brain field
// offsets, including goals, animation state and active flags, stay unchanged.
inline constexpr std::size_t kActors=256,kSentients=274,kCorpses=64;
inline constexpr std::size_t kOldStride=0x1560,kPerceptionStart=kOldStride;
inline constexpr std::size_t kRecordBytes=48,kStride=kPerceptionStart+kSentients*kRecordBytes;
inline constexpr std::size_t kSentientStride=0x88;
// Runtime unwind entry for the ordered stack-page probe of the native threat
// routine. Native chained unwind records are patched with the frame itself.
inline constexpr std::uint32_t kThreatProbeRva=0x4610000,kThreatProbeEndRva=0x461001c,kThreatProbeUnwindRva=0x4610040;
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
// Native checkpoint segment 3 discriminator. Stock 32/50 streams are decoded
// with stock descriptors, then migrated before the caller resumes.
struct PopulationChunkHeader {
    std::array<char,8> magic{'B','O','1','M','W','2','A','I'};
    std::uint32_t version=1,actors=kActors,sentients=kSentients;
    std::uint32_t actorStride=kStride,sentientStride=kSentientStride,reserved=0;
};
static_assert(sizeof(PopulationChunkHeader)==32);
inline bool SupportedPopulationChunk(const PopulationChunkHeader& h) {
    const PopulationChunkHeader expected;
    return h.magic==expected.magic && h.version==expected.version && h.actors==kActors && h.sentients==kSentients
        && h.actorStride==kStride && h.sentientStride==kSentientStride && !h.reserved;
}
inline bool MigrateLegacyActor(const void* legacy,std::size_t bytes,void* expanded,std::size_t capacity) {
    if(!legacy || !expanded || bytes<0x1558 || capacity<kStride)return false;
    const auto source=reinterpret_cast<std::uintptr_t>(legacy),destination=reinterpret_cast<std::uintptr_t>(expanded);
    if((destination>=source && destination-source<0x1558) || (source>=destination && source-destination<kStride))return false;
    std::memset(expanded,0,kStride);
    std::memcpy(expanded,legacy,0x1558);
    std::memcpy(static_cast<std::uint8_t*>(expanded)+kPerceptionStart,
        static_cast<const std::uint8_t*>(legacy)+0x348,50*kRecordBytes);
    return true;
}
} // namespace mod::expansion
