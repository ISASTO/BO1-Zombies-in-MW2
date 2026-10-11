# PLAYER test 7

Build `0.1.6-player-test7`, for the same exact supplied AMD64 executable.
This iteration fixes session/native God-state reconciliation, implements every
current PLAYER row, and makes the menu smaller. At publication retail behavior was pending
the owner's test. No Zombies AI, asset import, or weapon/world actions are added.

## Install

1. Fully close MW2. Extract **all** of `BO1MW2-player-test7.zip` outside the game
   folder, such as a folder on the Desktop.
2. Run the package's **Uninstall.cmd**, entering the folder containing the
   installed `iw4sp.exe`. Then run **Install.cmd** with that same folder.
3. Launch MW2 Single Player normally through Steam. Your saved FOV remains in
   `bo1mw2/settings.ini`; this build does not save player cheat preferences.
4. Enter a campaign mission and wait until you can move and fire. Press **F4**,
   select **PLAYER**, and use **Up/Down** and **Enter**. All eight rows should
   fit at once. **Backspace/Esc** goes back; **F4** closes the menu.

Only the package's `d3d9.dll` is installed beside `iw4sp.exe`. The scripts,
probe, README and test instructions stay outside the game folder. Manual
installation requires removing this project's previous DLL first; never
overwrite another wrapper. Original game files are unchanged on disk.

## Test God before the other controls

1. On a fresh launch, keep all player toggles OFF. Resume the save/checkpoint
   that previously stayed invulnerable. With **God OFF** and **Noclip OFF**,
   take ordinary enemy gunfire. Health should drop without an ON/OFF cycle.
2. Enable God and take ordinary gunfire. Disable it and confirm health can
   drop again. Check the displayed health and label after each action.
3. Enable God, reload a checkpoint in the same running process, and confirm
   God remains ON and works. Then disable God and reload again; it should
   remain OFF and damage should work.
4. Enable God, fully quit to the Desktop, relaunch through Steam, and resume.
   God should start OFF and ordinary damage should work. This does not erase
   your saved FOV. Collect diagnostics after this fresh-launch check before
   another launch overwrites `latest.log`.

**GUARDED** means a different verified native protection is active, including
noclip/UFO, scripted invulnerability, demigod, or the entity's no-damage state.
It is not a claim that the God toggle is ON. **OFF\*** means the additional
protection fields could not be read; record that label and return the log.
Scripted mission failures are separate from ordinary damage.

## Test the other PLAYER rows separately

| Row | ON behavior | OFF / cleanup check |
| --- | --- | --- |
| FOV settings | Existing selector/save/default page | Saved FOV and ADS still behave as in test6 |
| Infinite Ammo | Existing reserve pools replenish to 999; magazines still empty and reload | Replenishment stops; unchanged injected counts restore their captured baseline, and further reloads consume reserves |
| No Reload | Normal gun clip ammo stops decreasing | Clip ammo decreases again. If the clip was empty when enabled, reload once first |
| Noclip | Native free flight through collision with WASD/mouse; try jump and crouch bindings for vertical motion | Return to clear, solid ground before turning it OFF; collision and walking return |
| Super Speed | Default base movement speed changes from 190 to 380 | Restores the captured previous base speed |
| Super Jump | Native jump height increases from 39 to 117 | Land before disabling; subsequent jumps return to normal |
| Reset player toggles | Requests all six cheats OFF | Check damage, ammo consumption, collision, speed and jump again; saved FOV is preserved |

Use a normal campaign gun for ammo checks. Native No Reload
has special-weapon branches for Javelin/Stinger, AC130 and
certain scripted/support weapons. Infinite Ammo changes reserve pools only;
it does not give weapons or refill every offhand clip. Use God while comparing
jump height, then turn it OFF again for the final damage test. Noclip itself
blocks ordinary damage, even with the separate God toggle OFF.

All player toggles are session-only. They retain the requested state through
checkpoint/level reloads within the same process. F4 closing the menu does not
disable them. A full process restart starts them OFF. **LOCKED** means the
required exact-build fields or living-player gate are unavailable; no forced
unlock is performed. Failed resolution of a new feature does not disable the
working FOV/core menu.

## Return observations and diagnostics

Close MW2 and run **Diagnose.cmd** from this package. Attach its resulting ZIP
and the short form in [testing.md](testing.md). If anything fails, include the
last action, map, label, health/ammo behavior and any exact crash text. For a
layout problem, include the resolution and an in-game screenshot if possible.
Collect after the relevant failing run; a new launch replaces `latest.log`.

## Implementation and limits

God now has an explicit desired session state, reconciled with native flags
every 16ms while the player is alive. OFF clears the verified God/demigod
bits, even if a checkpoint restored them. On a verified new player lifetime,
explicit God OFF, or Reset, it also clears the separate scripted
invulnerability bit once when normal movement and native damage are enabled.
It preserves unrelated bits and never forces the entity's take-damage field.
Later mission protection is observed and shown as GUARDED instead of being
continuously erased. This one-time cleanup can remove protection at that
spawn/checkpoint; broader campaign sequencing still needs owner coverage.

Test6's log recorded God's entity bit OFF throughout the reported invulnerable
period and health 2 after an ON/OFF cycle. It did not record the separate
client protection flags. That supports the mismatch report, but does not
prove which native path caused it. Test7 records those fields and lifetime
changes. See [test6 evidence](runtime-test6-evidence.json).

No complete IW4 or T5 player structure is assumed. Sixteen optional signatures
corroborate the new partial fields, dvar consumers, and jump data. The reserve
edit uses an eight-byte compare/exchange that preserves its ID and compares
ID/count together; recycled entries cannot receive another pool's baseline.
Ownership is discarded on a server gap, map/client change or command-time
rewind. No Reload uses a byte compare/exchange on the named bool current;
speed uses a four-byte compare/exchange on the named integer current.

The retail jump consumers use pooled **39/78** float constants directly, not
the registered `jump_height` dvar. Super Jump updates only that verified
adjacent pair to **117/234** in this process, briefly makes its data page
writable and restores the page protection. OFF or loss of player readiness
restores the pair. Instructions, gravity, and the disk executable are unchanged.
All four height references and the single impulse reference are checked.
This is an exact-build compatibility implementation, not an assertion that a
generic jump-height dvar works. Native runtime effects await the test above.

At 3440x1440 the panel becomes 550x451 pixels instead of 912x810; row text is
about 45% smaller, and eight rows replace the old six-row viewport. Font source,
D3D9 vtable hooks, and rendering-state restoration are retained. Six portable
C++ suites, nine Python fixtures, 23 unique patterns and the Windows build
pass. PowerShell installation/diagnostic/removal checks run in an isolated
filesystem; this Linux workspace cannot execute retail MW2 or Windows probes.

## Test8 correction and acceptance

The owner accepts test7; see [redacted evidence](runtime-test7-evidence.json).
The earlier frag/flash interpretation was incorrect: their equal-name branches
bypass consumption to the native return when player_sustainAmmo is enabled.
Normal frag/flash grenades therefore share No Reload. Test8 explicitly labels
Infinite Grenades as linked and adds independent branch verification. Other
special-weapon paths are unchanged. See [test8](world-weapons-test8.md).
