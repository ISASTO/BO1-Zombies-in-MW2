#pragma once
#include <cstddef>

namespace mod {
struct SlotPatch {
    std::size_t index;
    void* replacement;
    void* original = nullptr;
};
struct PatchResult {
    bool installed = false;
    bool rollbackComplete = true;
};

// Modify only the selected entries in the existing table. Never copy/truncate a
// table or replace an object's vptr: native implementations can have private tail
// entries beyond the documented COM interface. Exchange must compare-and-swap.
template<class Exchange>
PatchResult InstallVtableSlots(void** table, SlotPatch* patches, std::size_t count, Exchange exchange) {
    if (!table || !patches || !count) return {};
    for (std::size_t i = 0; i < count; ++i) {
        if (!patches[i].replacement || !table[patches[i].index] || table[patches[i].index] == patches[i].replacement) return {};
        for (std::size_t j = 0; j < i; ++j) if (patches[j].index == patches[i].index) return {};
        patches[i].original = table[patches[i].index];
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (exchange(table + patches[i].index, patches[i].replacement, patches[i].original)) continue;
        PatchResult result;
        while (i) {
            --i;
            if (!exchange(table + patches[i].index, patches[i].original, patches[i].replacement)) result.rollbackComplete = false;
        }
        return result;
    }
    return {true, true};
}
} // namespace mod
