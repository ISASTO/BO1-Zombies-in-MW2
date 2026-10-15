#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace mod::expansion {
inline constexpr std::uint32_t kSkinCacheBytes=0x4800000; // 72 MiB, 32 bytes/vertex.
inline constexpr std::uint32_t kCrowdSurfaceBytes=0x200000; // Per native frame slot.
inline constexpr std::uint32_t kSurfaceHandles=0x8000;
inline constexpr std::uint32_t kSurfaceReverseBytes=kCrowdSurfaceBytes/4*2;
inline constexpr std::uint32_t kSurfaceTableBytes=0x10000*8;
inline constexpr std::uint32_t kCrowdStateRva=0x47e0000;
inline constexpr std::uint32_t kCrowdStateBytes=0x400;
inline constexpr std::uint32_t kRenderFrameBaseRva=0x458fb00,kRenderFrameStride=0x13d700;
struct CrowdFrameCounters {
    std::uint64_t surfaces=0;
    std::uint32_t used=0,peak=0;
    std::uint64_t reverse=0,table=0;
};
struct CrowdResourceState {
    std::uint64_t clientSkeleton=0,serverSkeleton=0;
    std::uint32_t glassRebuilds=0,glassIgnoredFrees=0;
    std::array<std::uint8_t,0x28> reserved{};
    std::array<CrowdFrameCounters,2> frames{}; // +0x40, +0x60.
    std::uint32_t gpuFailures=0,cpuFailures=0,surfaceFailures=0,badFrames=0;
    std::uint32_t resets=0;
    std::uint32_t gpuPeak=0,cpuPeak=0;
    std::array<std::uint32_t,2> handleCounts{};
    std::uint32_t handleFailures=0,mapFailures=0,reservedCount=0;
    std::array<std::uint32_t,4> secondFrameLow{};
    std::array<std::uint64_t,2> framePointers{};
    std::array<std::uint8_t,0x30> reservedCounters{};
    std::array<std::uint32_t,64> warnings{}; // +0x100, before native throttling.
    std::uint32_t invalidGlassQueries=0,invalidFeatures=0,queryClaim=0,lastInvalidFeature=0;
    std::uint64_t firstQuery=0,firstVertexData=0;
    std::array<std::uint8_t,0xb8> query{};
    std::uint32_t queryStage=0;
    std::uint32_t invalidFragments=0,fragmentOutputRejections=0,fragmentInputRejections=0;
    std::uint32_t lastFragmentVertices=0,lastFragmentBytes=0;
    std::array<std::uint8_t,0x110> tail{};
};
static_assert(sizeof(CrowdFrameCounters)==32 && sizeof(CrowdResourceState)==kCrowdStateBytes);
static_assert(offsetof(CrowdFrameCounters,used)==8 && offsetof(CrowdFrameCounters,peak)==12);
static_assert(offsetof(CrowdFrameCounters,reverse)==16 && offsetof(CrowdFrameCounters,table)==24);
static_assert(offsetof(CrowdResourceState,frames)==0x40 && offsetof(CrowdResourceState,warnings)==0x100);
static_assert(offsetof(CrowdResourceState,gpuFailures)==0x80 && offsetof(CrowdResourceState,resets)==0x90);
static_assert(offsetof(CrowdResourceState,handleCounts)==0x9c && offsetof(CrowdResourceState,secondFrameLow)==0xb0);
static_assert(offsetof(CrowdResourceState,framePointers)==0xc0);
static_assert(offsetof(CrowdResourceState,invalidGlassQueries)==0x200 && offsetof(CrowdResourceState,query)==0x220);
static_assert(offsetof(CrowdResourceState,queryStage)==0x2d8);
static_assert(offsetof(CrowdResourceState,invalidFragments)==0x2dc && offsetof(CrowdResourceState,lastFragmentBytes)==0x2ec);
// Body-only relays have generated standalone unwind records for the exact
// already-executed native frames. The warning entry is a true leaf relay.
struct CrowdRelay {std::uint32_t begin,end,unwind;};
inline constexpr std::array<CrowdRelay,11> kCrowdRelays{{
    {0x4612000,0x4612100,0x4613a00}, // GPU bounded reservation.
    {0x4612100,0x4612200,0x4613a40}, // CPU bounded reservation.
    {0x4612200,0x4612400,0x4613a80}, // Independent skinned surface records.
    {0x4612400,0x4612600,0x4613ac0}, // Original per-frame reset boundary.
    {0x4613000,0x4613400,0x4613b00}, // Non-finite glass query.
    {0x4613400,0x4613600,0x4613b40}, // Invalid collision feature selector.
    {0x4613800,0x4613a00,0x4613b80}, // Reject bad glass pose before world traversal.
    {0x4615000,0x4615200,0x4615700}, // Glass fragment input and edge limits.
    {0x4615200,0x4615400,0x4615740}, // Bound native polygon output writes.
    {0x4615400,0x4615600,0x4615780}, // Reject degenerate or oversized output.
    {0x4615600,0x4615700,0x46157c0}, // Gate native piece publication.
}};
} // namespace mod::expansion
#include "crowd_bridge_layout.hpp"
