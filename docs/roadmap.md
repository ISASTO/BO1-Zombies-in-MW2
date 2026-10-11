# Incremental roadmap

Updated 2026-10-11. Complete one useful, testable change at a time.

1. **Phase 1 core accepted:** retail launch, F4 menu/navigation, level/player
   reads, displayed health, and God toggle work in the owner's test4 estate run.
   The owner also confirms God state survives their checkpoint reloads.
   Keep second-map/reset checks in subsequent small tests.
2. **Test5 preview accepted in the front end:** matching diagnostics cover all
   100 rounds, bounds and navigation. No mission/regression run was included.
   The new common_zombie.ff normal tuning recovery matches existing numbers.
3. **Current test6: FOV selector with saved preference:** static exact-build
   registration/domain and camera reads verified; portable selector/persistence
   tests and Windows build pass. Test 60-120 selection, explicit Save/reset,
   full restart persistence, ADS and checkpoint/mission reapplication in retail.
   This implements the owner's request using the mod's own settings file.
4. Audit remaining BO1 configuration overrides and dependency closure, then incrementally test round
   controller scheduling and an IW4 actor adapter. Identify required native
   targeting, pursuit, damage/notetrack and death semantics before enabling AI.
5. Audit/import one local asset type at a time with versioned conversion tools.
   Validate zombie behavior against BO1 observations before expanding gameplay,
   special rounds, weapon features, or a native IW4 menu.

Source and tooling belong in GitHub. Original assets, extracted original scripts,
executables, crash dumps, and complete returned logs remain private local inputs.
