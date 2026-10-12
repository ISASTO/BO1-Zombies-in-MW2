#pragma once
#include <cstdint>
namespace mod {
enum class NativeHandBuildState { Unknown, Queued, Ready, Canceled, Unavailable, Failed };
struct NativeHandBuildResult {
    NativeHandBuildState state=NativeHandBuildState::Unknown;
    std::uintptr_t right=0,left=0;
};
} // namespace mod
