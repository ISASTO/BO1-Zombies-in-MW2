#pragma once
#include <array>
#include <cstdint>
namespace mod {
// Early (DllMain) native capacity: script child-variable pools 2 -> 8 and
// parent objects 36,856 -> 65,526 (with object-key names and saved object
// numbers covering the full 16-bit id space), the script memory tree's pointer
// allocations moved to an owned heap, checkpoint slot buffers 1 MiB -> 32 MiB,
// dobj transfer records 1024 -> 8192 and entity handle records 4095 -> 32767.
struct ScriptVmStatus {
    bool attempted=false,enabled=false;
    const char* reason="NOT_ATTEMPTED";
    std::uint32_t failedRva=0;
    std::uintptr_t host=0,region=0,heap=0;
    std::array<std::uintptr_t,2> saves{};
    unsigned patches=0,relays=0;
};
struct ScriptVmUsage {
    bool valid=false;
    std::array<unsigned,8> children{};
    unsigned childTotal=0,childCapacity=0,childPeakPool=0,poolCapacity=0;
    unsigned parents=0,parentCapacity=0;
    unsigned mtNativeUsedNodes=0,mtNativeNodes=65536;
    std::uint64_t heapUsed=0,heapPeak=0,heapCommitted=0,heapReserved=0;
    unsigned heapAllocations=0,heapFallbacks=0,heapRejectedFrees=0,heapSizeMismatches=0;
    std::array<std::uint32_t,2> saveUsed{},saveSize{};
    unsigned entitiesInUse=0,entityCapacity=2046;
    unsigned dobjTransfers=0,dobjCapacity=0;
    // Entity/sentient handle info records (fatal "ENT_HANDLE_COUNT exceeded").
    unsigned entityHandles=0,entityHandleCapacity=0;
    // Per-frame model lighting patches, both native frame slots (fatal
    // "modelLightingPatchList ran out of elements" at 4096).
    std::array<unsigned,2> modelLighting{};unsigned modelLightingCapacity=4096;
};
bool InstallScriptVmExpansion();
const ScriptVmStatus& GetScriptVmStatus();
ScriptVmUsage ReadScriptVmUsage();
// Spawn governor: nonzero name of the resource that lacks headroom for one
// more actor (children, parents, script strings, entities), or nullptr.
const char* ScriptVmSpawnRefusal(const ScriptVmUsage& usage);
// Load shedding: nonzero when a fatal native pool is about to run out
// regardless of new spawns (owned actors are removed until it clears).
const char* ScriptVmEmergency(const ScriptVmUsage& usage);
void LogScriptVm(bool detailed);
} // namespace mod
