# Test24: actor history and shooting-buffer crashes

Version: `0.1.23-squad-test24`. Live capacity remains **256 actors / 274 sentients**.
Original multiplayer hitmarker image, sound, timing and existing menu options
remain included. Expanded checkpoint layout is still version 1.

The returned Test23 archive contains four new faults. The DLL hash matches the
published Test23 build; its logs show that expansion and MP assets initialized.
Estate failed during loading, and Contingency failed during shooting and after
spawning a few allies. These faults are separate from Test22's iterator overrun.

| Returned dumps | Native instruction | Cause |
| --- | --- | --- |
| 14:17:52 and 14:20:02 | `0x232e68` | The unrolled frame-history writer used old actor-relative displacements. It read a null entity pointer at `actor_cursor + 0x1560`, inside the expanded layout rather than the next actor. |
| 14:18:42 and 14:19:22 | `0x1a187e` | The shot-rewind loop scanned 256 actors with only 32 temporary valid flags. Both exception contexts show it reaching actor 40, reading outside the flags and dereferencing a null entity. |

## Corrections

The frame-history writer's seven additional actor pointers and fourteen active
flag reads now use the expanded `0x48c0` stride. Its eight-actor cursor step
changes from `0xab00` to `0x24600`. The matching history validator receives the
same conversion, including its biased negative displacements and entity-reuse
checks. One old signed-byte displacement cannot hold the new address: a relay
widens it to a DWORD while preserving its SIB registers, immediate and flags.

Three native shooting rewind/restore loops now stop after **32 records**, matching
their actual `0x320`-byte caller buffers and companion loops. Their byte extent
is `32 * 0x48c0 = 0x91800`, instead of `256 * 0x48c0 = 0x48c000`.
The live actor pool, actor allocation, teams, perception, animations, snapshots
and saves still support 256 actors. This bounds temporary shooting records;
it does not restore the 32-actor spawning limit.

The original historical rewind representation is deliberately retained at 32
actors. Actors in slots 32..255 retain their current server pose during shots
and remain available for ordinary collision/hit detection. Expanding this
separate history representation needs coordinated changes to all history,
interpolation and caller stack buffers; it is not done in this release.
Startup diagnostics explicitly report this distinction.

Wrong history reads and excess pose restoration provide a concrete mechanism
for jitter, incorrect angles and guns pointing upward. The native regression
verifies that poses are restored correctly with these fixes. Visual friendly
movement and combat behavior still require an owner retest in the retail game.

The patch generator also rejects replacement spans containing undecodable data,
in addition to Test23's instruction-entry validation. Exact original-byte and
relocation checks still apply to every runtime mutation.

## Verification

The new private test applies the published old and corrected patch headers to
the exact native executable with its runtime arena outside the main image.
No executable, dump or game asset is redistributed.

- Old Test23 reproduces the returned writer fault at `0x232e68`.
- Old Test23 reproduces the returned shooting fault at `0x1a187e`. This case
  substitutes a correct 32-slot validator to isolate the independent excessive
  shooting-loop bound; the corrected cases run the original validator.
- Corrected code passes 640 native history pose checks, 2,048 active/recycled
  entity validation checks, 32 temporal interpolation checks and ring wrap.
- Native shot rewind/restoration passes 516 checks with dense and sparse low
  slots, all 256 live actors, and an inaccessible page immediately after the
  800-byte backup. Both inline weapon restore loops execute. Higher actor poses
  remain unchanged. Only world relinking and memset transport are substituted.
- Test23's guarded team-iterator regression still passes 2,052 cases.
- The existing native population regression still passes 256 allocations,
  274 linked sentient handles, 320 client goal pairs, animation lifecycle,
  expanded save/load and stock checkpoint migration with 244 checked patches.
  Its other checkpoint domains and booted-world dependencies remain isolated.
- All 18 C++ suites and 31 Python tests pass. The Windows AMD64 DLL has the
  expected system imports, ten D3D proxy exports, ASLR and NX.

See [returned fault evidence](runtime-test24-crash-evidence.json),
[history and shooting regression](history-test24-probe.json),
[iterator regression](iterator-test24-probe.json),
[expanded runtime regression](expanded-runtime-test24-probe.json), and
[build verification](squad-test24-build-verification.json).

These tests execute narrowly scoped native routines on Linux. They do not boot
Windows MW2 or prove crowded-battle, visual animation, compressed-checkpoint or
whole-engine stability.

## Install and retest

Close MW2, uninstall the current mod, extract the whole Test24 ZIP and run
`Install.cmd`. The installer backs up local player/save data under
`bo1mw2/backups/before-test24-*`; it leaves the game files on disk intact.
Test22/23 expanded checkpoints use the same versioned format.

Start a fresh Estate mission and then Contingency. Shoot before spawning
anything, then spawn a small squad and check their poses, following and combat.
After those checks pass, increase the population and test autosaves, death and
checkpoint reload. Confirm the menu shows `/ 256`. Run `CollectLogs.cmd` after
testing so a remaining failure can be compared with these two corrected paths.
