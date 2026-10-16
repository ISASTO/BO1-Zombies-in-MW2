# Test37: object-key names, saved object numbers, entity handles, full sweep

Version: **0.1.36-squad-test37**. Builds on Test36. Actor capacity remains
256 / 274 sentients.

## Returned Test36 result

**What the owner saw:**

- "exceeded maximum number of child1 script variables"
- "exceeded maximum number of parent script variables"
- "capacity seems to be getting worse"

**What the logs show** ([evidence](runtime-test37-evidence.json)):

| Run | Spawned at the end | Child pools | Parents | End |
| --- | --- | --- | --- | --- |
| Estate | 153 | ~10.5k of 51,199 each | 27,297 / 65,526 | crash at `0x227ddc` (VM resuming a thread stack) |
| Estate | 95 | ~11.1k each | 28,578 | engine error dialog |
| Contingency | 81 | ~9.3k each | 24,103 | engine error dialog |

No pool was more than about a quarter used. The pools weren't full; their
contents were corrupt.

## Cause

Test35 raised script object ids from 0x9000 to 0xfffd. The engine also uses
object ids as **variable names**: an array keyed by an object stores the
entry under name 0x10000 + id. Notify and endon wait lists work this way,
with waiting threads keyed by thread id.

Six engine routines decide "is this name an object key?" with `name <
0x19000`, which is 0x10000 + the old 0x9000. They are:

- `0x21d15d` (create)
- `0x21d766` (remove)
- `0x21df11`
- `0x21e550` / `0x21e682` (key readback)
- `0x221138` (checkpoint save and load)

For any object id ≥ 0x9000 the key was treated as an integer index, so:

- **No reference was held for it.** A waiting thread could be freed while a
  wait list still pointed at it. Its id was reused, and when the notify
  fired the VM resumed whatever now owned that id, which is the crash at
  `0x227ddc`. Releases that no longer matched corrupted the free lists,
  giving the false "exceeded" errors.
- **Checkpoint saves skipped it**, and loads mapped it back to the wrong
  object.

Ids ≥ 0x9000 come early. `AllocObject` balances new objects by child-pool
load, so childless thread objects pile onto one free list whose ids rise in
steps of 8. That list passed id 0x9000 with only 24–30k objects live, which
matches the 81–153 actors where the runs failed.

## Changes

1. **Object-key names cover every id.** The six checks now use 0x20000. The
   four array-index range checks built on the same constant (`0x21bb11`,
   `0x21d6c0`, `0x21e5b6`, `0x21f1c6`: `index+0x7e7000 <= 0xfe6fff`) now use
   `0x7e0000` / `0xfdffff`, so integer keys can never collide with object
   keys.
2. **Saved object numbers are unsigned.** The checkpoint loader read them as
   signed 16-bit (`movsx` at `0x2193b6`), so a checkpoint holding 32,768 or
   more script objects would have crashed on load. Stock MW2 never reaches
   that; 200+ actors would. It now uses `movzx`.
3. **Entity handle records: 4,095 → 32,767.** Every set entity or sentient
   handle (enemy, look target, goal, cover, and so on) holds one record, and
   exhaustion is the fatal "ENT_HANDLE_COUNT exceeded". The pool moves to
   owned storage with a guard page. All 6 references and the init bound are
   inventoried and checked against the original bytes.
4. **New telemetry, spawn guard and load shedding:**
   - entity handle records (`ENGINE_LIMITS entity_handles=`)
   - per-frame model lighting patches, whose stock limit of 4,096 is fatal
     (`model_lighting=`)
5. **The full sweep** ([capacity-sweep-test37.md](capacity-sweep-test37.md))
   is now a build check: `tools/audit_capacity_sweep.py`.

Everything from Test34–36 remains.

## Validation

- **[Native fixture](script-vm-test37-probe.json)**, running the exact
  engine code:
  - **Object keys.** An array created by the native `GetArray`, keyed by
    objects 0x7fff and 0xfffd:
    - On Test36's patch set, key 0xfffd holds no reference, isn't marked for
      save, and the array-index check overlaps the object-key range.
    - On Test37 both keys are referenced, kept alive when their creator lets
      go, released when removed, and marked for save. The index boundary is
      exact (−0x7e0000 accepted, −0x7e0001 rejected).
  - **Saved object numbers**, through the native writer `0x219bc0` and
    reader `0x219360`: stock faults on 0x8123; Test37 restores 0x1234,
    0x8123 and 0xfff0.
  - **Entity handles.** Stock stops at 4,095 with "ENT_HANDLE_COUNT
    exceeded". Test37 links 32,767; the native entity-free path releases all
    of them, and the pool refills to 32,767.
  - All earlier VM checks still pass: 409,487 children, 65,526 objects,
    savegame mark and sweep over 65,462 chained objects, and the memory-tree
    and checkpoint slots. The generated header, applied with a relocated
    region, gives identical results.
- **Sweep:** passes on Test37 and fails on Test36's patch set at exactly the
  fixed sites.
- **Existing suites:** all earlier native suites and audits pass; 20 C++ and
  38 Python tests pass.

## Limits

- No Windows gameplay test yet.
- 65,526 is the hard ceiling for script objects (16-bit ids).
- 2,046 is the hard ceiling for entities.
- Checkpoints saved by Test35/36 can contain the damaged keys. Start missions
  fresh from Mission Select.
