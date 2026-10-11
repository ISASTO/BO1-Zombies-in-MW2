# Incremental roadmap

Updated 2026-10-11. Complete one useful, testable change at a time.

1. **Phase 1 core accepted:** retail launch, F4 menu/navigation, level/player
   reads, displayed health, and God toggle work in the owner's test4 estate run.
   The owner also confirms God state survives their checkpoint reloads.
   Keep second-map/reset checks in subsequent small tests.
2. **Test5 preview accepted in the front end:** matching diagnostics cover all
   100 rounds, bounds and navigation. No mission/regression run was included.
   The new common_zombie.ff normal tuning recovery matches existing numbers.
3. **Test6 FOV accepted by owner:** matching logs show saved 100 loaded/applied,
   default 65/clear, selection and save. God protection/menu state disagreed
   after a return/restart; the exact cause is unconfirmed by its old fields.
4. **Test7 PLAYER accepted by owner:** God session reconciliation/guard handling,
   reserve ammo, native clip sustain, noclip, speed, fixed-jump compatibility,
   reset and the compact eight-row menu. The returned run confirms God
   OFF/death/recovery and valid fields but has no per-feature toggle actions.
5. **Test8 feedback received:** matching estate log confirms speed/jump
   adjustment and several world apply/restore paths. Owner reports invisible,
   empty Give All weapons and LOCKED Rapid Fire. Runtime correctly rejects
   three overlapping catalog matches; offline consuming regex hid ambiguity.
6. **Current test9: focused reversible WEAPONS repair:** filtered loaded primary
   inventory banks with native ammo IDs; Force Dual Wield for compatible guns;
   coordinated OFF restoration; corrected catalog and overlap-aware verifier.
   Eight C++ suites, 12 Python fixtures, 51 exact anchors, clean AMD64 build and
   actual-test8-to-test9 upgrade pass. Owner models/firing/undo/both-order checks
   are next. Undo snapshots are in-process; checkpoint/save recovery requires
   separate evidence. Kill All Enemies still needs hostile filtering and an
   engine-thread death path; no health-zero shortcut is enabled.
7. Audit remaining BO1 configuration overrides and dependency closure, then incrementally test round
   controller scheduling and an IW4 actor adapter. Identify required native
   targeting, pursuit, damage/notetrack and death semantics before enabling AI.
8. Audit/import one local asset type at a time with versioned conversion tools.
   Validate zombie behavior against BO1 observations before expanding gameplay,
   special rounds, weapon features, or a native IW4 menu.

Source and tooling belong in GitHub. Original assets, extracted original scripts,
executables, crash dumps, and complete returned logs remain private local inputs.
