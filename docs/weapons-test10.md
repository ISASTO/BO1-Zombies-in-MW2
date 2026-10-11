# WEAPONS test10

Build **0.1.9-weapons-test10**, for the exact supplied AMD64 MW2 executable.
This repairs the three defects reported in test9. Portable/static verification
passes; new gameplay, animation appearance and FPS require your retail run.

## Install and establish the baseline

1. Close MW2. Extract **BO1MW2-weapons-test10.zip** outside the game directory.
2. Run the new package's **Uninstall.cmd**, then **Install.cmd**, selecting the
   folder containing `iw4sp.exe`. Only the owned DLL is replaced; FOV/settings
   and logs are preserved.
3. Launch Single Player through Steam and **restart the mission once** for this
   first test. A checkpoint saved by test9 may already contain granted guns;
   its unmodified inventory cannot be inferred after the old process exits.
4. Wait for player control, press **F4**, and open **WEAPONS**. Up/Down selects,
   Enter toggles, Left/Right on Give All changes banks, and F4 closes.
   `RESTART` means the initial inventory is full and no clean snapshot exists.

## Repairs and scope

| Control | Change |
| --- | --- |
| Give All | Retains one original baseline plus the IDs of weapons/ammo pools introduced by every bank. A checkpoint containing an earlier bank is cleaned before reapplying the selected bank; it never becomes the new original inventory. |
| Give All OFF | Removes known introduced guns by identity, across previous banks and slot reorder. An unchanged loadout restores the original snapshot; foreign native pickups/pools are preserved while introduced entries are removed. |
| Force Dual Wield | Updates the held primary gun, rather than scanning all 15 slots repeatedly. Temporarily clears the native campaign veto where verified animation data is available. |
| Dual animation compatibility | Prefers registered authored hand animations. Otherwise borrows the gun's existing normal animation names and arena-owned animation definition for the two hand slots; native code creates separate playback instances. `COMPAT` identifies this generic fallback. |
| Performance | Caches immutable model/weapon metadata, removes repeated full snapshots and all-slot dual scans, and logs updater time/read counts. Actual FPS still needs comparison in the same scene. |

The G18's campaign asset is named `glock` and had `noDualWield=1` in test9.
The old native bool could not bypass that veto. The new compatibility path
requires readable normal animation names and an already registered normal
definition before publishing hand state. It does not import new gun assets or
promise authentic akimbo animations for every campaign gun. Two rendered
view models also have a real rendering cost.

OFF, weapon/bank switching and readiness loss conditionally restore the edited
veto, name pointers, cached definition pointers, slot flags and left ammo.
Borrowed arena definitions are not allocated or freed by this DLL. Native
cache reset, stride, animation selector and bounded name count are independently
corroborated by exact-build signatures. No engine routine is called.

Readable asset identities are checked independently of positive firing time;
equipment and launcher entries are not treated as grantable guns. Temporary
loading/invalid reads defer reconciliation without losing the original baseline.
PLAYER/WORLD controls retain their independent gates. Changed player/map or
mismatched assets block stale writes. New grants retain the existing primary,
model, alternate-chain, ammo-ID and 15-slot filters.

Undo history remains in-process. It survives checkpoint reloads and toggles
during this run, including a checkpoint containing a previous bank. Turn the
weapon modes OFF before saving/quitting. Cross-process cleanup of already
modified external saves is not implemented. Saved FOV remains persistent.

## Acceptance run

Keep Infinite Ammo and No Reload OFF for initial ammo comparisons.

1. Record original weapons/ammo with **Weapon info to log**. Give ON, select
   bank2, reload a checkpoint saved with bank1, then use Left/Right through
   all available banks. On estate's observed catalog there were four.
2. Give OFF. Verify added guns disappear, original guns/ammo return, and normal
   switching/firing works. Repeat ON/OFF and checkpoint reload once. If a normal
   pickup occurs, verify it is retained and the known grants are removed.
3. Hold G18/`glock`; Dual ON, switch away/back once if the view model needs
   refresh, and test both triggers, reloads, models and ammo. Test another gun.
   Record `ON`, `COMPAT`, `WAIT` or `ON*`. OFF must return the previous
   single/natural-dual state.
4. Compare Dual OFF/ON/OFF at the same position/camera, with Rapid Fire and
   Explosive Bullets OFF. Record approximate FPS and any remaining collapse.
   Stay in each state at least ten seconds for timing/heartbeat samples.
5. Test both OFF orders: Give ON -> Dual ON -> Dual OFF -> Give OFF; then
   Dual ON -> Give ON -> Give OFF -> Dual OFF. Try checkpoint reload while
   both are ON. Reset weapons, and check God/health/FOV/Rapid Fire regressions.

Close MW2 and run **Diagnose.cmd** before another launch replaces `latest.log`.
Return its ZIP and [testing.md](testing.md) observations, including exact weapon
names, bank labels and any crash/module/offset. New logs include
`DUAL_ANIM_PROFILE`, `DUAL_ASSET`, live/original veto values, bank recovery
and `WEAPON_PERF` average/peak updater time and memory reads.

The game and Windows probe cannot run in this Linux workspace. Eight C++ suites,
12 Python fixtures, 57 overlap-aware native signatures, the AMD64 PE and isolated
actual-test9 upgrade are verified. The next acceptance gate is this in-game run;
after it passes, prioritize the BO1 normal round/spawn controller and a verified
IW4 actor adapter, with actual BO1 behavior/data as the reference.
