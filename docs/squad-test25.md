# Test25: animation-tree ownership and corpse lookup

Version: `0.1.24-squad-test25`. Live capacity remains **256 actors / 274 sentients**.
Native corpse capacity remains 64. Original MP hitmarkers, previous shooting
fixes and version-1 expanded checkpoints remain included.

The returned Test24 run loads and plays missions, but all three screenshots
show severely distorted NPC skeletons. The player weapon and environment appear
intact. New dumps occur after the latest observed populations of 61, 55 and 59
actors. The 55 value is the last heartbeat; it is not an exact fault-time count.

One dump faults in native DObj creation at `0x2957be`, reading animation-tree
field `+8` through the noncanonical pointer `0x10001000005d7`. Its low word is
entity number 1495. The other two dumps fault while removing the `openmenu`
command during engine error/shutdown; their earlier triggering engine errors
and complete corruption chains are not preserved in the exception contexts.

Test25 also preserves the native Com_Error recursion state and 4 KiB message
buffer in crash dumps, and copies an active error message into the text report.
The original formatter's destination and size are verified at `0x1fb6cc` and
`0x1fb6d8`. This is crash-only work, with no additional hook or per-frame cost.
If engine error cleanup still fails, the earlier error should remain available
instead of being hidden by its later command-removal fault.

## Three concrete relocation defects

| Path | Test24 defect | Test25 correction |
| --- | --- | --- |
| Temporary animation-tree allocation | Initial cursor read and cursor store moved to the arena, but final read at `0x12c715` still used original `0xf50f30`. Concurrent leases returned the same mutable tree while marking different occupancy flags. | Move the final read to the same relocated cursor. Check distinct ownership, exhaustion, release and reuse. |
| Corpse searches | `0x131eae`, `0x13229f`, `0x132dde` reference `0xf50b48`, the one-past-end entity-number field of the 64-corpse array. Test24 mistook them for temporary tree users and moved the endpoints into the arena. | Preserve their original endpoints. An absent entity must stop after 64 corpse records and use the native fallback. |
| Actor-to-corpse animation exchange | `0x132000` and `0x13201e` still used animation-root-relative `+0x1ea0`, the original 32-tree array. Low actors used cleared old pointers; higher indices overlapped packed corpse records and could supply them as tree pointers. | Exchange ownership with the expanded 256-tree array. Preserve the existing corpse array and all its other fields. |

The shared-tree bug provides a direct cause of animation state interference.
The high-slot exchange reproduces the class of packed-data pointer seen in DObj
creation. The wrongly moved search endpoints let lookups and removals operate
outside their actual array. These are fixes to the expansion patch, not changes
to native AI decisions or character assets.

The two exchange displacements use the native animation-global root `0xf4e1a0`.
The generator now records that base explicitly, separate from level-relative
snapshot fields. Otherwise external-arena relocation would compute the wrong
address even if a fixed-base fixture passed. Both kinds of reference are tested
with the arena outside the image.

A focused native reference audit covers the original animation-global region
and actor-tree-relative displacement candidates. It identifies four existing
direct actor-tree references plus these two ownership references, all three
scratch cursor accesses, and the three corpse endpoints. Similar constants in
stack frames and unrelated structures are not changed.

## Verification and limits

The new probe applies the actual old and corrected patch headers to the exact
native executable in a private MS-ABI fixture. It reproduces all three old
defects: duplicate live leases, acceptance of a fake corpse just past the array,
and actor 59 supplying packed corpse data as an animation tree.

Corrected checks pass:

- 336 distinct simultaneous temporary animation trees, with safe exhaustion.
- 1,000 native release/reallocation cycles, correct occupancy flags, and no
  alias between concurrently held trees.
- 195 checks across three native corpse searches/removals, covering all 64
  valid records and an absent entity with poisoned data immediately afterward.
- Native ownership swaps for all 256 actor slots, preserving every unrelated
  actor tree and corpse field.
- Existing native history/interpolation, guarded shot rewind/restoration,
  2,052 team-iterator cases, 256 allocations, animation lifecycle, snapshots,
  expanded save/load and legacy migration. The runtime plan has 243 checked
  native fragments plus three arena fragments.
- All 18 C++ suites and 31 Python tests. AMD64 DLL and installer checks pass.

The earlier temporary-tree test checked only nonzero leases and exhaustion,
which missed aliasing. The full population fixture now also verifies each
returned tree against the relocated cursor and requires 336 unique identities.

Tree identities in the new ownership probe are distinct opaque tokens. Real
XAnim node reset, corpse geometry callbacks and subsequent script notifications
are isolated. The test does not evaluate real skeleton assets, boot the Windows
renderer, or reproduce the complete engine error/shutdown crash chains.
Corrected visual animation and crowded retail stability remain owner tests.

See [returned evidence](runtime-test25-crash-evidence.json),
[animation regression](animation-test25-probe.json),
[population regression](expanded-runtime-test25-probe.json),
[history regression](history-test25-probe.json),
[iterator regression](iterator-test25-probe.json), and
[build verification](squad-test25-build-verification.json).

## Install and retest

Close MW2, uninstall Test24, extract the whole Test25 ZIP and run `Install.cmd`.
The installer backs up player/save data under `bo1mw2/backups/before-test25-*`.
No game EXE, fastfile or IWD is changed. The expanded save format remains version 1.

Restart Estate and Contingency from the beginning for this check. First inspect
campaign NPCs before spawning anything, then add a small squad and check normal
animation, movement and combat. If those look correct, test past 60 actors and
increase toward 256. Include kills/corpse transitions, autosaves, death and
checkpoint reload. Use `CollectLogs.cmd` after testing.
