# Test35: the full 16-bit parent space, shared helper spawners, load shedding

> **Superseded by [Test36](squad-test36.md):** Test35 corrupted checkpoint saves (the save marker reached the relocated object maps through a base register the generator did not relocate). Do not use Test35.

Version: **0.1.34-squad-test35**. Actor capacity remains 256 / 274 sentients.
All Test31–Test34 changes remain.

## Returned Test34 result

**What the owner saw:**

- Estate failed at about **160 actors** with "exceeded maximum number of
  parent script variables".
- Contingency failed at about **200 actors** with the same dialog. That is the
  Test33 child-pool error moved on by roughly 90 actors.
- Diagnose.cmd reported that LoaderProbe.exe was missing.

**What the Contingency log shows** ([evidence](runtime-test35-evidence.json)).
The Estate log was overwritten by the next launch.

- `SCRIPT_VM enabled=1`: the Test34 expansion was active.
- **Children** peaked at 121,089 of 409,592. The child pools are no longer
  the limit.
- **Parent objects** (objects, arrays and script *threads*) are the limit:

  | Actors | Parents in use |
  | --- | --- |
  | 0 | 12,459 |
  | 108 | 35,952 |
  | 160 | 29,169 to 36,155 within 30 s |
  | 200 | 36,196 of 36,856, then the error |

  Combat alone swings the total by about 7,000 within 10 seconds. That is
  roughly 120 objects per actor, most of them script threads.
- **The guard was too small for those swings.** It refused spawns at 137 and
  156 actors. Then, while the count dipped, it allowed spawning to resume,
  and the next combat spike crossed the limit.
- **Entities:** 1,395 at 0 actors and 1,883 at 200, against the fixed
  2,046. About 2.4 entities per actor.
- **Other pools were fine:**
  - script strings: 21,339 / 65,536
  - pointer heap: 188 KB
  - checkpoint: 517 KB / 32 MiB
  - dobj transfers: 658 / 8,192
  - anim info: 3,813 / 16,383

## Changes

1. **Parent objects: 36,856 → 65,526.**
   - Parent ids are 16-bit in the engine's variable entries, so the stock
     0x9000 parents were well short of the id space. Parents now use ids
     0–0xfffd, and the child pools move above them (base 0x9002 → 0x10002;
     child relative ids stay 16-bit within each pool).
   - All 197 native child-base sites, the 2 folded tempVariable offsets, the
     2 pool-dispatch bounds and the savegame parent sweep bound are
     rewritten.
   - The two per-parent savegame maps (0x9000 words each) move to owned
     storage with room for 0xffff ids, together with their 31 references and
     4 clear sizes.
   - Each site is checked against the original bytes before it is written.
2. **Shared helper spawners.** Every owned actor used to keep its own helper
   spawner entity with its own script object. Now one helper per (source
   class, team) serves all owned actors of that kind, the same way campaign
   spawners with `count > 1` work. That saves one entity and its variables
   per actor.
   - A helper is freed only when its last actor is removed.
   - Checkpoint adoption accepts shared helpers.
   - If the native factory ever refuses a reused helper, reuse switches off
     for the rest of the level (`AI_HELPER_REUSE disabled=1`).
3. **The spawn guard is sized to the observed swings.** Spawns are refused
   when:
   - parents are within 1/8 of the limit (8,190)
   - total children are within 16,384
   - a single child pool is within 4,096
   - string nodes are within 4,096
   - entities are within 128
4. **Load shedding.** Four times a second, if parents (within 2,048), any
   child pool (1,024), string nodes (1,024) or entities (32) are about to run
   out, owned actors are removed until they are back under the limit, up to 8
   per check. Corpses and hostiles go first. The log shows
   `AI_RESOURCE_SHED reason=…`, and `AI_COMBAT_STATS resource_shed=` counts
   removals. A combat spike now costs actors, not the game.
5. **Logs from earlier runs are kept.** An engine error dialog is not a crash,
   so the next launch overwrote the only log of the failing run. Previous
   runs are now kept as `session-<date>-<time>.log`, the last eight of them.
   CollectLogs.cmd picks them all up.
6. **Package cleanup.** Diagnose.cmd, DiagnoseStartup.ps1 and
   InstallStartup.cmd belong to the separate startup-diagnostic package
   (which carries LoaderProbe.exe). They are no longer included, so their
   error cannot come up.

## Validation

- **[Native fixture](script-vm-test35-probe.json).** It runs the exact native
  code with the new layout.
  - Stock: 36,862 parent objects, then exactly the returned "exceeded
    maximum number of parent script variables".
  - Test35: 65,526 objects, highest id 0xfffd. Every object's child is found
    by native lookup.
  - The native savegame sweep `0x2210b0`, which walks every parent id through
    the relocated save map, clears all 65,526 objects.
  - All the Test34 child, release/refill, high-pool, memory-tree and
    checkpoint checks still pass: 409,487 children, then a refill to the same
    total.
  - The generated header, applied the way the DLL applies it with a
    relocated region, gives identical results.
- **Generator checks:** 680 native edits, plus the relay code and its unwind data. The child-base site inventory
  (197 sites plus 2 folded) and the save-map references (31) must match
  exactly. Edits are disjoint from the actor-capacity rows, and all 99
  runtime signatures are unchanged.
- **Existing suites:** all earlier native suites and audits pass; 20 C++ and
  35 Python tests pass.

## Limits

- No Windows gameplay test. The shared-helper change is new behaviour in
  real missions.
- 65,526 is the ceiling for parent objects: ids are 16-bit inside the
  engine's variable entries. At ~120 objects per actor plus the level's own,
  256 should fit on Contingency. A map whose scripts use more per actor will
  stop at the guard, or shed actors, rather than crash.
- The entity table (2,046) is still a hard limit. Shared helpers should
  bring Contingency to about 1.4 entities per actor, which fits 256 under
  the guard.
