#include "vtable_patch.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct FakeDevice { void** vptr; int calls = 0; };
int PrivateSetState(FakeDevice* self, int state) { ++self->calls; return state + 700; }
int NativeSetState(FakeDevice* self, int state) {
    // Observed Windows SetRenderState dispatch at byte offset 0x430 (slot 134).
    // The COM public interface has 119 entries; the native table has more.
    return reinterpret_cast<int (*)(FakeDevice*, int)>(self->vptr[134])(self, state);
}
void OldMethod() {}
void NewMethod() {}
void ThirdPartyMethod() {}
bool Exchange(void** slot, void* replacement, void* expected) {
    if (*slot != expected) return false;
    *slot = replacement;
    return true;
}
}
int main() {
    auto oldMethod = reinterpret_cast<void*>(OldMethod);
    auto newMethod = reinterpret_cast<void*>(NewMethod);
    auto thirdParty = reinterpret_cast<void*>(ThirdPartyMethod);
    std::array<void*, 160> table{};
    table.fill(oldMethod);
    table[57] = reinterpret_cast<void*>(NativeSetState);
    table[134] = reinterpret_cast<void*>(PrivateSetState);
    const auto before = table;
    FakeDevice first{table.data()}, second{table.data()};
    std::array<mod::SlotPatch, 4> patches{{{2,newMethod},{16,newMethod},{17,newMethod},{42,newMethod}}};
    const auto result = mod::InstallVtableSlots(table.data(), patches.data(), patches.size(), Exchange);
    Check(result.installed, "four public hooks install");
    Check(first.vptr == table.data() && second.vptr == table.data(), "both object vptrs retain native table identity");
    auto setState = reinterpret_cast<int (*)(FakeDevice*, int)>(table[57]);
    Check(setState(&first,206) == 906 && first.calls == 1, "native private slot 134 remains callable after hooks");
    Check(setState(&second,185) == 885 && second.calls == 1, "a second object sharing the table still works");
    for (std::size_t i=0; i<table.size(); ++i) {
        const bool patched = i==2 || i==16 || i==17 || i==42;
        Check(table[i] == (patched ? newMethod : before[i]), "all unselected slots including the private tail are untouched");
    }
    for (const auto& patch : patches) Check(patch.original == oldMethod, "original method remains available for forwarding");

    // A competing edit must cause rollback without overwriting its replacement.
    table = before;
    unsigned attempts = 0;
    const auto conflict = mod::InstallVtableSlots(table.data(), patches.data(), patches.size(),
        [&](void** slot, void* replacement, void* expected) {
            if (++attempts == 3) *slot = thirdParty;
            return Exchange(slot,replacement,expected);
        });
    Check(!conflict.installed && conflict.rollbackComplete, "failed patch transaction rolls back prior hooks");
    Check(table[2] == oldMethod && table[16] == oldMethod && table[17] == thirdParty,
          "rollback preserves a concurrent third-party edit");
    Check(table[42] == oldMethod && table[134] == before[134], "unreached hook and private slot remain native");

    // If another owner changes one of our installed slots, report incomplete
    // rollback and preserve that owner's edit instead of blindly restoring it.
    table = before;
    attempts = 0;
    const auto interruptedRollback = mod::InstallVtableSlots(table.data(), patches.data(), patches.size(),
        [&](void** slot, void* replacement, void* expected) {
            if (++attempts == 2) { table[2] = thirdParty; return false; }
            return Exchange(slot,replacement,expected);
        });
    Check(!interruptedRollback.installed && !interruptedRollback.rollbackComplete,
          "incomplete rollback is reported");
    Check(table[2] == thirdParty, "rollback never clobbers a later owner");
    std::cout << "Shared-table/private-dispatch and hook conflict tests passed.\n";
}
