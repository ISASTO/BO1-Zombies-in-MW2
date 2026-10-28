#pragma once
// Test44: MW2 asset pools enlarged from DllMain, before DB_Init links them
// (see the Test44 section of zombie_bindings.hpp). Fails closed: if any
// table entry, routine or state differs from the exact build, nothing is
// written and the stock pool stays.
#include <cstddef>
#include <cstdint>

namespace mod::assetpools {
struct Status {
    bool attempted=false,applied=false;
    const char* reason="NOT_ATTEMPTED";
    int loadedSoundCapacity=0;
    std::uintptr_t pool=0;
};
// The checks, on an image already in memory (host = module base). Pure: used
// by Expand() and the tests.
const char* Refusal(std::uintptr_t host);
bool Expand();
const Status& GetStatus();
// Entries in use (capacity minus the free list), or -1 before DB_Init linked
// the pool or when the free list is unreadable.
int LoadedSoundsInUse();
void LogStatus();
} // namespace mod::assetpools
