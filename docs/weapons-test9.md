# WEAPONS test9

Historical prototype. The returned run found checkpoint baseline contamination,
failed OFF cleanup after recovery, and G18/dual FPS defects. Use
[test10](weapons-test10.md) for the current repair and acceptance run.

Build **0.1.8-weapons-test9**, for the exact supplied AMD64 `iw4sp.exe`.
This is a focused repair of test8, with reversible Give All and Force Dual Wield.
Portable/static checks pass; the new in-game effects require your retail run.

## Install

1. Fully close MW2. Extract **BO1MW2-weapons-test9.zip** outside the game folder.
2. Run the new package's **Uninstall.cmd**, then **Install.cmd**. Select the
   folder containing `iw4sp.exe`, found through Steam Single Player > Properties
   > Installed Files > Browse. The installer places only `d3d9.dll` beside it.
   Saved FOV and old logs are preserved. Do not overwrite another wrapper.
3. Launch Single Player through Steam, enter a campaign mission, and wait until
   you can control the player. Press **F4**, then enter **WEAPONS**.
4. Up/Down selects a row, Enter toggles, Left/Right on Give All selects its bank,
   and Backspace/Esc goes back. F4 closes the menu. Gameplay keeps running.

## What changed

| Control | ON | OFF |
| --- | --- | --- |
| Give All (usable banks) | Preserves original equipment and adds loaded primary bullet guns with a readable player model and verified ammo/clip IDs. Initializes right magazines and reserve ammo. Left/Right changes banks within the native 15-slot limit. | Restores the captured original owned weapons, metadata, ammo, and held weapon for the same validated inventory/player lifetime. |
| Force Dual Wield | Enables native `bg_forceDualWield`, compatible owned-weapon flags, left magazines, and the native hand field. `ON*` means the held gun is outside the verified compatibility filter. Switch away/back if the view model has not refreshed. | Restores the captured native bool and edited per-weapon/left-ammo state; derives the current gun's ordinary hand state. Naturally dual-wielded original guns stay natural. |
| Rapid Fire | Corrected unique catalog signature enables the existing current-gun timing override: half normal fire time, minimum 20ms. | Restores the owned timing on OFF, switching weapons/banks, or loss of player readiness. |

The native global `g_giveAll` bypass is held OFF during bank mode. It previously
exposed inappropriate catalog entries without initializing inventory/ammo.
This test does not load additional fastfiles. Scripted, offhand, missing-model,
non-bullet, and alternate-linked new guns are excluded. Existing equipment is
preserved. Full ammo tables skip a candidate instead of overwriting a live pool.
The bank/grant totals and individual weapon names/pool IDs appear in the log.

Give All and Dual Wield coordinate their snapshots so either OFF order removes
that feature without reviving the other. OFF restores pre-toggle ammo; it does
not preserve ammo spent with these modes enabled. Pickup/script changes and
changed map/player/asset identities invalidate stale inventory snapshots rather
than blindly overwriting a mission's inventory. Restoration retries appear as
`UNDO`; blocked/unready reads appear as `LOCKED`.

Snapshots are held in this process. Same-process checkpoint reloads revalidate
live IDs and asset identities before using them. They are not persisted across
a full game exit; turn both modes OFF before saving/exiting this test. An older
checkpoint containing modified inventory needs separate recovery evidence.
No cheat request is saved; explicitly saved FOV remains the only preference.

Native dual-wield eligibility does not guarantee every model/animation is
correct on every map; the first owner test must confirm rendering and firing.
It does not provide akimbo assets/animations to a gun that forbids dual wield.
Rapid Fire changes shared asset timing, so AI using that weapon may be affected.
It does not turn semi-auto into automatic or remove reload/equip delays.

## Focused sequence

1. Leave Infinite Ammo and No Reload OFF. Use **Weapon info to log**; record
   the original weapons and ammo. Toggle Give All ON, close F4, and cycle the
   weapons with normal game controls. Test visible models, shots, reloads and
   ammo on several guns. Open F4 and try Left/Right banks, if more than one.
   Toggle OFF; the original inventory/ammo/held gun should return. Log again.
2. With Give All OFF, hold a dual-capable handgun/SMG available in the mission.
   Toggle Force Dual Wield ON, close F4, and switch away/back once if needed.
   Fire both hands and reload. Toggle OFF and verify the original single/dual
   state and left ammo return. Record `ON*` and the weapon name if unsupported.
3. Test both together: Give ON -> Dual ON -> Dual OFF -> Give OFF. Repeat:
   Dual ON -> Give ON -> Give OFF -> Dual OFF. Cycle/firing should still work,
   and neither OFF sequence should restore a stale dual flag or an added gun.
4. Hold a normal rifle. Rapid Fire should unlock; record info before/ON/OFF,
   then test switch/back with it ON. Try bank switching with it ON. OFF should
   restore the original timing. For a semi-auto gun, press for each shot.
5. After the isolated checks, try a checkpoint reload and mission exit/re-entry.
   Observe requested state, banks, restoration, `LOCKED`/`WAIT`/`UNDO`, and
   any unexpected weapons/models. Turn weapon modes OFF before a full exit.
   Recheck F4 navigation, God ON/OFF, health and saved FOV.

Stop at an unexpected result and record the exact map, weapon, bank, last action,
and observed symptom. Close MW2 and run **Diagnose.cmd** before another launch
replaces `latest.log`; attach its ZIP and the observations in [testing.md](testing.md).
The log is `<MW2>\bo1mw2\logs\latest.log`, with `%LOCALAPPDATA%\BO1MW2` fallback.
Return any crash text/minidump or Windows Application Error module/offset.

To remove the mod, close MW2 and run **Uninstall.cmd**. This removes the owned
DLL, preserving original game files, settings and logs. Removing the DLL cannot
undo inventory already embedded in an external game save.

