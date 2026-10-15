# Test33: compass table overflow and native animation pools

Version: **0.1.32-squad-test33**. Actor capacity remains 256 / 274 sentients.
All Test31 and Test32 guards remain.

## Returned Test32 result

The owner reached **~160 actors on Estate** (previous wall 96–98), then
crashed. Contingency crashed after friendly fire forced a checkpoint reload,
and again while spawning toward ~100 actors. Three new dumps, installed DLL
`7a368d2d…` ([evidence](runtime-test33-evidence.json)):

| Dump | Map | Primary cause | Final fault |
| --- | --- | --- | --- |
| 070656 | Estate, 150–162 actors | engine error "exceeded maximum number of anim info" | command list during error shutdown (`0x1ef903`) |
| 070816 | Contingency, 63 actors | checkpoint reload executes a console command | corrupted command entry (`0x272628`) |
| 070921 | Contingency, ~58–100 actors | engine error "exceeded maximum number of anim info" | command list during error shutdown (`0x1ef903`) |

### The Test32 hardware watch caught the writer

In every run the watch recorded thousands of writes (97,647 on Estate) into
the static command entries, all from **`0x92d49`, `0x92d50`, `0x92d57`,
`0x92d68`** — the native compass/minimap entity update `0x92c00`.

That routine keeps 42 records of `0x2c` bytes at `0x3fdf50`: **32 actor
slots**, 2 player slots and 8 other slots. The actor slot comes from the
client entity→actor map, which the capacity expansion widened to 256. Actor
slots past 31 therefore write past the table:

| Actor slots | Memory overwritten | Effect |
| --- | --- | --- |
| 32–41 | player/other compass records | wrong minimap icons |
| 51–63 | static console command entries | crash on the next command lookup (checkpoint reload, death, shutdown) — the long-running `0x1ef903` crash family |
| 93–209 | client glass pane state table `0x3fef50` | bogus "shatter pane N" requests; Test32 blocked **43,892** of them on Estate, for panes up to #495 |

This is the root cause of the whole glass crash chain from Test25 to Test31:
the glass code was being fed actor positions as pane states. Test32's
shatter guard was correct but downstream.

### Anim info pool

Separately, the native XAnim anim-info pool is a static 4,096-record table.
Estate used about 26 records per actor; Contingency about 40–70. Both runs hit
the native engine error, whose shutdown then tripped the corrupted command
list.

## Changes

1. **Compass actor-slot bound** (relays `0x4616200..0x4616500`). Actor slots
   ≥ 32 are not tracked on the compass. The slot lookup returns "not found"
   for them, and the second compass writer (`0x92580`) now honors "not found"
   instead of writing record −1 as stock code does. Actors 0–31 are tracked
   exactly as before. Counters: `AI_COMPASS`.
2. **Anim-info pool relocated to 16,384 records** (arena `0x4640000`, 1 MiB,
   guard page after). All 58 RIP-relative and 10 image-relative references
   are retargeted; the circular free-list init modulus and end bound change
   from 4,096 to 16,384. Native allocation and free code are unchanged.
   `AI_ANIM_INFO used/peak` reports real usage.
3. **Anim-transfer budget relocated to 16,384 records** (arena `0x4741000`).
   This per-snapshot server→client animation copy was 2,048 records; a
   checkpoint reload transfers every actor's tree at once.
4. The static command watch stays armed: after this fix it should report
   **zero** hits. Any hit identifies a new writer.

## Validation

- [Native table fixture](tables-test33-probe.json) runs the exact compass and
  XAnim code. On the Test32 image: actor slot 62 writes into the command
  entries, slot 100 writes into the glass place table, anim-info allocation
  stops at **4,095** with "exceeded maximum number of anim info", and anim
  transfer stops at **2,048** with "exceeded maximum number of anim transfer
  info". On Test33: owned compass slots produce identical records, eight
  expanded slots (32–255) leave everything past the table untouched, the
  free list links all 16,383 records, allocation reaches 16,383 and transfer
  16,384 before the native error, and the old static arrays stay unused.
- All ten earlier native suites and the consumer audit pass on Test33; 19 C++
  and 31 Python tests pass.

## Known non-fatal limit

At 150 actors the renderer's model lighting cache reported 10,813 allocation
misses (`AI_RENDER_WARNING category=13`). Affected models reuse cached
lighting; it cannot crash. It may need expanding later for visual quality.

## Limits

No Windows gameplay test. Contingency's per-actor animation use means 256
Contingency actors plus a full corpse set is close to the new pool size;
`AI_ANIM_INFO` will show it. Script-variable pools are the next untested
native limit at 256.
