# Test30: correct the Test29 mission-start renderer regression

Version: **0.1.29-squad-test30**. The live target remains 256 actors / 274
sentients. This update corrects the renderer mapping; the 72 MiB skinning
buffers, guarded animated-record pools and glass-physics checks remain active.

## Returned failure and cause

Both returned Estate crashes occur at `iw4sp.exe+0x645bd`, before a mod actor
has been spawned. The native CPU renderer reads `mesh+0x10`, with a null mesh.
This is a Test29 regression, distinct from the earlier crowded glass-physics
fault. The installed DLL hash matches the delivered Test29 build.

The faulting record pointers are the original surface-pool origin plus
`0x8000*4` and `0x8001*4`. These are the first two animated handles introduced
by Test29, incorrectly treated as native quarter-offsets. The dump retains
five valid published animated mappings and 380 bytes of records. The correct
records are at offsets 0 and 76 in the owned buffer, with non-null mesh fields;
their forward and reverse mappings agree.

The missed renderer path adds the surface origin as **0x10a60 quarter units**
and scales the sum by four. Test29's direct byte-offset inventory did not
include this representation. Its declared reader comparisons therefore passed
without exercising the consumer that failed in the mission.

The [numeric crash evidence](runtime-test30-startup-evidence.json) verifies
both returned contexts without publishing private paths or raw dump memory.

## Corrected consumers

The expanded inventory finds six uses of the quarter-unit representation:

| Native site | Corrected outputs |
| --- | --- |
| 0x5dfa2 | Initial surface pointer and light index; retain the native RAX quarter result. |
| 0x5e2da | Next grouped surface pointer. |
| 0x5e3a9 | Next surface pointer in one grouped draw path. |
| 0x5e5a5 | Next surface pointer in the alternate grouped draw path. |
| 0x643d3 | Initial CPU-skinned surface pointer and light index. |
| 0x648cf | Subsequent CPU-skinned surface pointer. |

Each relay resolves the original packed ID through the existing native-frame
table, executes the original ADD to preserve its flags and any live scalar
output, then supplies the full record pointer. Native light indices, other
registers and SIMD state remain intact. The first and subsequent record paths
are corrected together, preventing a fix that only survives the first draw.

The original rigid quarter-offset IDs still resolve to their original storage.
Animated records retain the existing guarded storage, bounded publication,
native worker submission and per-frame reuse boundaries. No actor limit is
lowered and no model is hidden to bypass the startup fault.

There are now 28 bridged field/pointer reader sites, two packed producers and
one publication gate. The native inventory includes 34 direct representation
sites, scanning byte, half-word, quarter-word and eight-byte origin constants.
The 31 bridge relays and seven rendering/physics relays are checked against
their native frames; all 42 registered non-leaf entries include the four
retained threat, awareness and glass-lifetime relays.

## Validation and limits

The [crowd fixture](crowd-test30-probe.json) now reproduces Test29's invalid
read at **the same instruction, 0x645bd**. It executes the original quarter-unit
addressing block followed by the original mesh read, isolating the intervening
asset, shader and D3D work. This is a native addressing regression, not a
simulation of a complete Windows mission.

The corrected fixture passes:

- 1,680 native packed-producer/reader comparisons, including all six new sites,
  both native frame slots, rigid and animated boundary IDs, all GPRs/SIMD
  registers and relevant flags.
- 14 CPU-renderer record reads through the formerly failing addressing path,
  including the first and subsequent animated IDs, both frames and upper/lower
  ID boundaries.
- The retained 3,840 model allocation cases, 24,000 concurrent reservations and
  8,000 concurrent handle publications, with bounded exhaustion and coherent
  per-frame records.
- The prior glass-fault reproduction and finite-query/feature guards.

The expanded native actor/checkpoint, iterator, animation ownership, shot
history, awareness, threat, skeleton and glass lifetime regressions are also
checked with the current generated plan. The build verification records the
unit tests, Windows DLL checks and isolated packaged installer checks.

Real Windows rendering, mission startup, physics integration and stable
256-actor gameplay still require an owner retest. The new failure has a
specific reproduced cause and correction; this does not establish that the
earlier crowd flicker or source of invalid glass physics is fully resolved.

## Retest

Close MW2, uninstall Test29, extract the complete Test30 package and run its
Install.cmd. Start Estate fresh through Mission Select. First leave all mod
toggles off and play for two minutes without spawning. Check the weapon,
campaign NPCs and normal movement. Repeat mission loading on Contingency.

Only after normal startup works, return to the staged crowd protocol. Begin
with 16 and 32 actors, then 48 and 64. Stop adding actors at the first flicker
and select AI info to log. If a mission still crashes, collect its logs before
launching again and report the mission and whether any actor had been spawned.
