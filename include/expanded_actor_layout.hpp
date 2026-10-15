#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <array>
#include <cstring>
#include "crowd_resources.hpp"

namespace mod::expansion {
// Experimental Test22 native layout; also exercised by isolated probes.
// Append perception rather than inserting it: all original brain field
// offsets, including goals, animation state and active flags, stay unchanged.
inline constexpr std::size_t kActors=256,kSentients=274,kCorpses=64;
inline constexpr std::size_t kOldStride=0x1560,kPerceptionStart=kOldStride;
inline constexpr std::size_t kRecordBytes=48,kStride=kPerceptionStart+kSentients*kRecordBytes;
inline constexpr std::size_t kSentientStride=0x88;
inline constexpr std::uint32_t kArenaRva=0x4000000,kArenaBytes=0x800000;
struct ArenaRange {std::uint32_t rva,bytes;};
// Independent resource storage, with an inaccessible whole page after every
// data block. In-page tails remain padding; no live object occupies a guard.
inline constexpr std::uint32_t kResourcePointersRva=0x47e0000;
inline constexpr std::size_t kClientSkeletonBytes=0x900000,kServerSkeletonBytes=0x800000;
// Test33: native XAnim anim-info pool, 16,384 x 64-byte records (stock 4,096).
inline constexpr std::uint32_t kAnimInfoRva=0x4640000,kAnimInfoRecords=0x4000,kAnimInfoBytes=kAnimInfoRecords*64;
// Per-snapshot anim transfer records, 0x1c bytes each (stock 2,048).
inline constexpr std::uint32_t kAnimTransferRva=0x4741000,kAnimTransferRecords=kAnimInfoRecords,kAnimTransferBytes=kAnimTransferRecords*0x1c;
inline constexpr std::array<ArenaRange,11> kArenaDataRanges{{
    {0x4000000,static_cast<std::uint32_t>(kActors*kStride)},
    {0x4500000,static_cast<std::uint32_t>(kSentients*kSentientStride)},
    {0x4510000,0x4404},{0x4560000,static_cast<std::uint32_t>(kActors*8)},
    {0x4570000,static_cast<std::uint32_t>(kSentients*2)},
    {0x4580000,0x4404},{0x4590000,8},{0x45a0000,336*8+336+4},
    {kResourcePointersRva,kCrowdStateBytes},
    {kAnimInfoRva,kAnimInfoBytes},
    {kAnimTransferRva,kAnimTransferBytes}}};
constexpr std::array<ArenaRange,kArenaDataRanges.size()> ArenaGuards() {
    std::array<ArenaRange,kArenaDataRanges.size()> out{};
    for(std::size_t n=0;n<out.size();++n)
        out[n]={(kArenaDataRanges[n].rva+kArenaDataRanges[n].bytes+4095u)&~4095u,4096};
    return out;
}
inline constexpr auto kArenaGuards=ArenaGuards();
constexpr bool ArenaLayoutFits() {
    for(const auto& guard:kArenaGuards)
        if((guard.rva&4095u) || guard.bytes!=4096 || guard.rva<kArenaRva ||
           guard.rva-kArenaRva>kArenaBytes-guard.bytes)return false;
    for(std::size_t a=0;a<kArenaGuards.size();++a)for(std::size_t b=a+1;b<kArenaGuards.size();++b)
        if(kArenaGuards[a].rva<kArenaGuards[b].rva+kArenaGuards[b].bytes &&
           kArenaGuards[b].rva<kArenaGuards[a].rva+kArenaGuards[a].bytes)return false;
    for(const auto& range:kArenaDataRanges) {
        if(range.rva<kArenaRva || range.bytes>kArenaBytes || range.rva-kArenaRva>kArenaBytes-range.bytes)return false;
        for(const auto& guard:kArenaGuards)
            if(range.rva<guard.rva+guard.bytes && guard.rva<range.rva+range.bytes)return false;
    }
    for(std::size_t a=0;a<kArenaDataRanges.size();++a)for(std::size_t b=a+1;b<kArenaDataRanges.size();++b)
        if(kArenaDataRanges[a].rva<kArenaDataRanges[b].rva+kArenaDataRanges[b].bytes &&
           kArenaDataRanges[b].rva<kArenaDataRanges[a].rva+kArenaDataRanges[a].bytes)return false;
    return true;
}
static_assert(ArenaLayoutFits());
inline bool ArenaReadable(std::uintptr_t offset,std::size_t bytes) {
    if(offset>kArenaBytes || bytes>kArenaBytes-offset)return false;
    if(!bytes)return true;
    for(const auto& guard:kArenaGuards) {
        const auto begin=guard.rva-kArenaRva;
        if(offset<begin+guard.bytes && begin<offset+bytes)return false;
    }
    return true;
}
// Runtime unwind entry for the ordered stack-page probe of the native threat
// routine. Native chained unwind records are patched with the frame itself.
inline constexpr std::uint32_t kThreatProbeRva=0x4610000,kThreatProbeEndRva=0x461001c,kThreatProbeUnwindRva=0x4610040;
inline constexpr std::uint32_t kAwarenessProbeRva=0x4610100,kAwarenessProbeEndRva=0x461011c,kAwarenessProbeUnwindRva=0x4610140;
inline constexpr std::uint32_t kGlassAllocateRva=0x4611000,kGlassAllocateEndRva=0x4611300,kGlassAllocateUnwindRva=0x4611300;
inline constexpr std::uint32_t kGlassFreeRva=0x4611400,kGlassFreeEndRva=0x4611500,kGlassFreeUnwindRva=0x4611500;
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
