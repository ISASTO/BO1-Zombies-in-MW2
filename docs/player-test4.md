# Player test 4: correct the entity-number guard

**Owner result, 2026-10-11: passed in the tested estate session.** The owner
confirms correct health, working God ON/OFF, and God state retained through
their checkpoint reloads. The matching log has complete slot-zero reads,
five successful toggles preserving unrelated flags, health 2/70/100, and a
blocked God selection after return to the main menu. No new crash evidence is
included. See [test4 evidence](runtime-test4-evidence.json). The Phase 1 core
milestone is accepted on this exact build; second-map/reset checks remain.

## Evidence from the working menu

The owner reports that menu test 3 works. The returned ZIP's receipt and DLL
hash match `0.1.2-menu-test3`. Its log shows the first frame at 3440x1440,
successful font creation, 82 menu inputs, all six categories, a loaded `estate`
server/map, and a return to no active server. No new crash evidence is included.
The old three dumps are unchanged test 1 files, not new failures.

Player readiness did not pass. All mission snapshots remained `LOADING / PLAYER
NOT READY`, and three God selections were blocked. The old code checked a
32-bit integer at entity `+0x00` as if it were the slot number. That was a layout
mistake: native code uses a byte there for entity type. Its allocator derives an
array index and stores it as a **16-bit WORD at `+0x7e`**, at instruction RVA
`0x1a0953`. Native entity/script handling independently zero-extends a WORD from
that field at RVA `0x19afb4`. The exact-build verification tool uniquely matches
both instructions and decodes/compares their member displacement to our layout.

The old log's default zero health was not proof that health was actually zero;
validation could return before reading it. The Transition state was also not
proof that the owner was still loading. Test 4 records a specific player-read
status and number to resolve that ambiguity.

## Small change

Read the correct WORD identity, then validate the same health and flags fields
as before. Unknown executable, unresolved signatures, absent server/map,
unreadable fields, nonzero entity number, implausible health, or nonpositive
health keep God Mode unavailable. A failed read shows UNKNOWN health. God Mode
still changes only bit zero of the native flags, once per explicit selection.
There are no new cheats, engine function calls, AI hooks, or asset imports.

The portable regression first failed with the previous identity guard. It uses
an independently encoded fixture with a nonzero type byte, slot number zero at
`+0x7e`, unrelated nonzero bytes immediately after that WORD, health 100, and
unrelated flags. After the correction it passes, as do wrong-slot, unreadable
field, dead-player, and invalid-health cases. The existing menu/pattern and
private graphics-table regression tests also pass. The native graphics hook
source is unchanged from the working test 3.

## Focused owner test

1. Close MW2. Extract BO1MW2-player-test4.zip outside the game directory.
2. Run Uninstall.cmd to remove this project's previous DLL, then Install.cmd
   and select the same folder containing iw4sp.exe.
3. Launch SP through Steam. At the main menu, God Mode should remain LOCKED.
4. Load `estate` or another campaign mission and wait until you can move/fire.
   Press F4. Record the displayed map, player state, and health. A living player
   should report LEVEL ACTIVE / PLAYER ALIVE with positive health.
5. In PLAYER, turn God Mode ON with Enter. Take ordinary enemy gunfire and
   observe health/survival. Turn it OFF and confirm ordinary damage returns.
6. Confirm that NPC activity and WASD/mouse/fire continue with the menu open,
   and that normal controls return after closing it.
7. Reload a checkpoint, return to the main menu, then load another mission.
   Check state/health updates and that God is LOCKED outside a level. Its actual
   state after a reload is read from the game; persistence is not promised.
8. Exit MW2, run Diagnose.cmd, and attach the resulting ZIP. Report the health,
   God-on/God-off observations, reload result, and any last action before a fault.

If identity/health still fails after player control begins, collect the results
without trying to force an unlock. A new launch overwrites latest.log. Full
instructions, including optional device-reset testing, are in [testing.md](testing.md).

## Limits

The layout correction is statically confirmed, covered by regression tests, and
now accepted through owner observations and test4 diagnostics. The observed
checkpoint persistence is not a universal guarantee: the mod reads the actual
flag and does not continuously reapply it. Device reset, a second mission,
Steam-overlay interoperability and cutscene/player swaps remain separate checks.
No original game assets or returned logs/dumps
are included in the source repository or test distribution.
