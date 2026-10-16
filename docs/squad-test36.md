# Test36: fix Test35's checkpoint save corruption

Version: **0.1.35-squad-test36**. This replaces Test35. Test35's capacity
changes are kept, and its one defect is fixed. Actor capacity remains
256 / 274 sentients.

## Returned Test35 result

**What the owner saw:**

- A "child1" exceeded dialog.
- A checkpoint reload loaded to a black screen with some sound but no
  gameplay.
- After a restart, a "child0" error appeared without spawning anybody.
- Errors happened more often than in Test34.

**What the logs show.** Five runs were kept as `session-*.log`, and the
details are in the [evidence](runtime-test36-evidence.json).

- **No pool was anywhere near its limit when the errors came:**
  - children 68k–90k of 409,592
  - every child pool 8.5k–11.5k of 51,199
  - parents 22k–30k of 65,526

  These "exceeded" errors were false.
- **The errors follow checkpoint saves.** In the Estate runs and the last
  Contingency run the error comes 1–13 s after an `AI_CHECKPOINT_WRITE`.
- **A checkpoint load restores only part of the script state.** In the
  no-spawn Contingency run there were 39,742 children and 12,894 objects
  before the load, and only 12,674 children and 2,313 objects after it. That
  is the black screen: most of the level's script state was missing.
- The crash dump at 0x227ddc is the VM resuming a thread whose saved stack
  was freed. That happens right after a save.

## Cause

Test35 enlarged and relocated the two savegame maps that track every script
object (object id → save id, save id → object id). The generator found every
instruction that addresses the maps by absolute or rip-relative address.

The save marker (`0x217fc0` / `0x218290`) instead holds the address of the VM
globals block (scrVarPub, `0x15a0380`) in a register and reaches the maps at
`+0x68` and `+0x12068`. Those accesses were not relocated. The marker
therefore wrote part of its marks into the old maps and part into the new
ones. The save's sweep (`0x2210b0`) then treated live objects as unreachable
and cleared them, and the save itself captured only a fraction of the VM.

Reproduced natively: with the Test35 patch set, the exact native marker
reaches **2 of 1,000** chained objects.

## Fix

- **Map A** (object id → save id) now grows **in place**: 0x9000 → 0xffff
  entries, over the old map B. All its accesses, including the short
  `scrVarPub+0x68` forms, are unchanged.
- **Map B** (save id → object id) moves into the stock variable-list storage,
  which the relocated list no longer uses. Its 16 direct references and all
  4 `scrVarPub+0x12068` references are retargeted. The generator requires
  exactly these 4 (`0x217ffc`, `0x218018`, `0x2182da`, `0x218350`).
- **New generator check:** every memory access whose base register holds a
  VM-global address is traced, including registers copied from it. Every
  access that crosses into a relocated area must be handled, or the build
  fails.
- Everything else from Test35 is unchanged:
  - 65,526 parent objects
  - 8 child pools
  - shared helper spawners
  - the spawn guard and load shedding
  - session logs

## Validation

- **[Native fixture](script-vm-test36-probe.json).** It adds a savegame
  mark-and-sweep test.
  - It builds a chain of objects linked through pointer fields, plus one
    unreachable object.
  - It runs the native marker and the native sweep.
  - **Stock:** 36,798 of 36,798 marked.
  - **Test36:** 65,462 of 65,462 marked, highest id 0xfffd. The sweep keeps
    every reachable object's fields and clears only the unreachable one.
  - **The Test35 patch set fails this test.**
  - All the earlier child, parent, release/refill, memory-tree and
    checkpoint checks pass.
  - The generated header, applied the way the DLL applies it, gives
    identical results.
- **Fixture fix:** image-relative fixups now honour their base, matching the
  DLL.
- **Generator checks:** 669 native edits. They are disjoint from the
  actor-capacity rows, and all 99 runtime signatures are unchanged.

## Limits

No Windows gameplay test. Checkpoints saved by Test35 are damaged. Start
missions fresh from Mission Select, not with Resume Game.
