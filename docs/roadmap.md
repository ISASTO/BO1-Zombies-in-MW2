# Incremental roadmap

Updated 2026-10-11. Complete one useful, testable change at a time.

1. **Phase 1 core accepted:** retail launch, F4 menu/navigation, level/player
   reads, displayed health, and God toggle work in the owner's test4 estate run.
   The owner also confirms God state survives their checkpoint reloads.
   Keep second-map/reset checks in subsequent small tests.
2. **Current test5:** read-only normal-solo BO1 round-rules preview, from actual
   local GSC fallback values and native numeric evidence. Obtain the missing
   tuning StringTable before claiming final retail/Kino parameters.
3. **FOV selector with saved preference, requested by owner 2026-10-11:** identify
   the exact build's FOV dvar/API, value range, ADS behavior, and mission/reload
   resets. Add a keyboard selector and explicit save/reset controls. Store the
   preference in the mod's own configuration and restore it on later launches
   when game state is valid. Verify persistence after a full game restart.
   The owner explicitly permits finishing the current work first; FOV is not
   implemented in test5. No engine address or safe range is assumed yet.
4. Recover final BO1 tuning and dependency closure, then incrementally test round
   controller scheduling and an IW4 actor adapter. Identify required native
   targeting, pursuit, damage/notetrack and death semantics before enabling AI.
5. Audit/import one local asset type at a time with versioned conversion tools.
   Validate zombie behavior against BO1 observations before expanding gameplay,
   special rounds, weapon features, or a native IW4 menu.

Source and tooling belong in GitHub. Original assets, extracted original scripts,
executables, crash dumps, and complete returned logs remain private local inputs.
