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
6. **Test9 feedback received:** Rapid Fire and bank cycling work; checkpoint
   recovery discarded the original snapshot and captured a full bank as the
   baseline. OFF then restored that contaminated state. G18 has native
   noDualWield=1; all-slot dual scans were expensive. No new crash evidence.
7. **Test10 weapon repair, retail pending:** retain original snapshot and introduced
   IDs across checkpoints/banks; selectively remove introduced guns on OFF;
   preserve foreign pickups; cache metadata and update only the held gun. A
   reversible normal-animation/arena-definition fallback provides generic dual
   compatibility where authored hand assets are unavailable. Eight C++ suites,
   12 Python fixtures, 57 exact anchors, AMD64 build and actual-test9 upgrade
   pass. Owner checkpoint/undo/G18/FPS/both-order acceptance is next. Initial
   testing starts from a clean mission restart; history is in-process.
8. **Current combined test11, explicitly authorized before owner testing:**
   BO1 source-derived normal round/spawn controller with simulated tickets,
   in-game diagnostics, bounded starts and game-clock/lifetime guards. Scoped
   ordinary-player melee admission port and local dependency audit are tested
   in source. Ten C++ suites and 17 Python fixtures cover owned code/tooling;
   live gameplay and all test10 weapon repairs remain pending in tomorrow's
   combined package. No native actor, damage or asset calls were added.
9. Use that evidence to fix regressions, then identify a verified IW4 SP
   engine-thread scheduling point and read-only actor/sentient/asset identities.
   Establish one safe spawn/init/cleanup fixture; translate targeting, pursuit,
   collision, damage/notetrack and death semantics before enabling live AI.
10. Audit/import one local asset type at a time with versioned conversion tools.
   Validate zombie behavior against BO1 observations before expanding gameplay,
   special rounds, weapon features, or a native IW4 menu.

Source and tooling belong in GitHub. Original assets, extracted original scripts,
executables, crash dumps, and complete returned logs remain private local inputs.
