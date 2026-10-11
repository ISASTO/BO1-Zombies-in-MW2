# WEAPONS / WORLD test 8

Historical test. The owner's returned run exposed invisible/empty Give All
weapons and an overlapping catalog pattern that blocked Rapid Fire. Use
[test9](weapons-test9.md) for the current focused repair and reversible toggles.

Build `0.1.7-world-weapons-test8` targets the same supplied AMD64 executable.
The owner accepts test7's PLAYER behavior and requests adjustable speed/jump
and the next WEAPONS/WORLD controls. This build still has no Zombies gameplay.

## Install and controls

1. Fully exit MW2. Extract **all** of `BO1MW2-world-weapons-test8.zip` into a
   folder outside the game installation.
2. Run this package's **Uninstall.cmd** and select the MW2 directory containing
   `iw4sp.exe`. Then run **Install.cmd** and select that same directory.
   Upgrade/removal preserves saved FOV and logs. Do not overwrite an unrelated
   `d3d9.dll`; the installer checks ownership and hashes.
3. Launch Single Player normally through Steam, enter a campaign mission,
   then press **F4**. Use **Up/Down** to select, **Enter** to toggle/open,
   **Left/Right** to change adjustable values, and **Backspace/Esc** to go back.
   F4 closes the menu; play continues while it is open.

Adjusting a value while OFF changes the selection. Enter enables it. Values
change live while ON. All player/weapon/world requests and strength selections
are session-only; only explicitly saved FOV persists across process launches.

| Row | Implementation / selection | Important scope |
| --- | --- | --- |
| PLAYER / Super Speed | 1x–5x base speed, steps of 0.5x; selected default 2x | Restores captured native base speed on OFF |
| PLAYER / Super Jump | 1x–8x normal height, steps of 1x; selected default 3x | Land before reducing; fall damage still applies |
| WEAPONS / Give All (loaded) | Native access to the current map's loaded weapon catalog | Toggle, then cycle weapons normally; does not load every game's weapon or create inventory assets |
| Infinite Grenades (linked) | Shares `player_sustainAmmo` with PLAYER / No Reload | Verified normal frag/flash bypasses; toggling either row also changes the other row and gun clip sustain |
| Rapid Fire | Halves the held bullet gun's native firing delay; minimum 20ms | Previous gun restored on switch; reload/equip delays and semi-auto input behavior unchanged; shared gun asset can affect AI |
| Explosive Bullets | Native global bullet option | Can affect AI gunfire; use God ON for the initial isolated test |
| Weapon info to log | Records held weapon identity/type/timing and control values | Use before/after toggles and weapon switches |
| WORLD / Slow Motion | 0.25x–1x, steps of 0.25x; selected default 0.5x | Whole game clock, including player and scripts; never sets zero |
| Freeze AI | Native actor-think gate | Both friendly/enemy thinking; animation, physics and scripts can continue |
| Gravity | 100–1600, steps of 100; selected default 400; normal 800 | Player's verified fixed constant plus native projectile float dvar |
| Kill All Enemies | PLANNED, no action | Requires verified hostile filtering and an engine-thread death path |

**Reset weapon toggles** disables its three new toggles and the linked PLAYER
No Reload request. **Reset world toggles** disables its three controls and
resets their selected strengths. Cleanup conditionally restores values the mod
still owns; native intervening changes and reused weapon assets are preserved.
Dead/loading/unresolved states can display LOCKED or WAIT. ON is based on the
observed native value, not just the requested toggle. Rapid Fire shows AT MIN
if the gun is already at the minimum and no timing edit was needed.

## Retail test sequence

Test controls separately, using a normal walking combat section first.

1. Keep God and Noclip OFF. Confirm ordinary damage still works. Test speed
   arrows while OFF, then ON at two strengths, then OFF. Repeat for jump,
   landing between changes. Confirm normal movement returns.
2. Give All ON: cycle through available map weapons; OFF: check ordinary
   access returns. Some map-loaded weapons can be scripted/special; record
   their names and any unusual equip behavior.
3. No Reload OFF: fire a gun and throw a frag/flash. Enable Infinite Grenades:
   check both grenade counts and gun clips, and the linked PLAYER row.
   Disable it: check consumption resumes. Keep Infinite Ammo OFF for this test.
4. Log weapon info with Rapid Fire OFF, then ON. Fire, switch to another
   normal gun, log again, then switch back. OFF/reset should restore timing.
   Test a semi-auto gun separately; it should still require individual presses.
5. Test Explosive Bullets briefly with God ON and Noclip OFF. Disable it and
   verify ordinary bullets return. Record both player and AI effects.
6. Test slow motion at 0.5x and 0.25x, then OFF. Freeze AI briefly, then OFF;
   confirm actors resume thinking. Test gravity 400 and 1200 with Super Jump
   OFF, land, then OFF; subsequent jumps should use normal gravity.
7. Reset both groups. Test one enabled control through checkpoint reload and
   mission exit/re-entry. Fully quit with controls ON, relaunch: requests
   start OFF and gameplay should be ordinary; saved FOV should remain.

Stop at the first unexpected result, preserve the log and note the last action.
Close MW2 and run **Diagnose.cmd** before another launch replaces `latest.log`.
Attach its ZIP and report map/resolution, the failed row/value, ON/OFF behavior,
any LOCKED/WAIT/AT MIN label, and exact crash text. A screenshot helps with UI
clipping. The collector includes receipt, settings, logs, probe results and
available crash evidence. Uninstall.cmd is the rollback.

Portable tests and exact-file analysis do not execute retail Windows gameplay.
The new behavior awaits this owner run; second-map, cutscene and device-reset
coverage remain separate checks.
