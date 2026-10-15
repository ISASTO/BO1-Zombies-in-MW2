# Test32: never fracture a glass piece the shatter lookup did not find

Version: **0.1.31-squad-test32**. Actor capacity remains 256, with 274
sentients. All Test31 renderer, actor, glass-fragment and lifetime guards
remain active.

## Returned Test31 result

The owner reported crashes again around 96–98 spawned actors. The returned
archives contain three new Test31 dumps (installed DLL `e9e9b914…`):

| Dump | Fault | Scene | Path |
| --- | --- | --- | --- |
| 20261015-055729 | `0x10e903` | Estate, 96 friendlies, 0 enemies | Client glass shatter |
| 20261015-060313 | `0x104469` | Estate, ~98 actors, 0 enemies | Client glass shatter |
| 20261015-055938 | `0x272628` | Estate, player death, 61 owned actors removed | Static command list |

All Test31 fracture counters (invalid fragments, output limit, inputs) are
zero in every log. These are not the overrun Test31 bounded.

### The two glass crashes share one stock path

Both stacks unwind to client glass place-state application (`0xa2000`, return
`0xa2209`) → shatter lookup `0x100870` → native fracture `0x103a80` (return
`0x1009f0`). See [numeric evidence](runtime-test32-shatter-evidence.json).

`0x100870` scans the active piece bitmap for a piece whose state records the
place. A match sets a stack flag at `[rsp+0x40]` and stores the index in a
local at `[rsp+0x3c]` that is **never initialized**. The stock routine then
calls fracture **unconditionally**, ignoring the flag.

| Dump | Place | Found flag | Index passed to fracture | Note |
| --- | --- | --- | --- | --- |
| 055729 | 10 | 0 | `0x7ff7` (stack residue) | Place 10 has no active piece |
| 060313 | 27 | 0 | `0` (stack residue) | Estate loads places 0–26 only |

Fracture therefore runs on an inactive or unrelated piece record. The first
dump faults reading that record (a zero-vertex record 1 MB past the piece
array); the second walks a broken edge list built from it.

The same stack signature (`0x1009f0`, `0xa2209`) is present in the **Test30**
glass crash (`20261014-161533`, place 0, found 0, index 0). Its repeated
two-vertex fragments came from fracturing that stale record, which is why
bounding the fragment output in Test31 moved the crash rather than ending it.
The Test25 and Test27 glass-allocator dumps also contain this call chain but
with the flag set; whether earlier garbage fractures caused their free-list
damage is not established.

### Why the shatter reached a place with no piece

Not yet captured. Place 27 is outside the 27 loaded places, so the requested
state was invalid when it was applied. Place 10 had no active piece while its
client state was still below "shattered". The Test31 crash-1 run also logged
17 ignored duplicate glass releases between 80 and 96 actors. Test32 records
the evidence needed for this upstream step (below) instead of assuming it.

### The command-list crash

`20261015-055938` follows player death (`AI_CLEANUP reason=PLAYER_DEATH`).
Console command execution walks the native command list and reads a name
pointer `0xc501c4ea00012492` from the static node at `0x3fea20`. The same
family (`0x1ef903`, `0x1ef8f0`, `0x1ef2b0`) recurs in **eight** returned
crashes from Test24 to Test31 on Estate, Contingency, Boneyard and Ending.
Each time a static node at `0x3fe908..0x3fea20` holds float-like data in its
name or link field. Earlier reports classed these as secondary shutdown
faults; they share one damaged region. The store that damages it has never
been captured.

## Changes

1. **Shatter lookup guard** (`0x1009d6`, relay `0x4616000`). When the lookup
   found no piece, skip the native fracture call. The caller still records
   the applied place state exactly as after a stock fracture. A found piece
   takes the unchanged native path. Counter: `missing_place_shatters`, plus
   the last place.
2. **Server place damage bound** (`0x16a060`, entry relay `0x4616100`). The
   server indexes 12-byte place records with a trace hit id and no bound.
   Out-of-range places are rejected and the native caller is recorded.
   Counter: `rejected_place_damage`, last place and caller RVA.
3. **Ignored-release ring.** The Test28 duplicate/out-of-range glass release
   guard now records the native caller and piece for the last eight ignored
   releases (`AI_GLASS_IGNORED_FREE`). This identifies which glass path
   releases pieces it does not own.
4. **Static command watch** (diagnostic only). After the first level is
   running, x64 debug registers watch the four damaged command-node fields on
   every existing thread. A write records the instruction, value and image
   return addresses (`AI_STATIC_WATCH_HIT`, also in the crash report) and the
   game continues unchanged. Threads that already use debug registers are
   left alone.
5. **Crash dumps** also include the client per-place glass states
   (`0x3fef50`), the server place records, and image data referenced by any
   faulting register.

Two new relays have standalone unwind descriptions (one describes the
existing `0x100870` frame, one is an entry relay with no prologue). Neither
pushes, pops, calls or writes RSP. 371 checked native entries, 63 arena
fragments, 48 registered relay unwind entries.

## Validation

- [Native glass-state fixture](glass-state-test32-probe.json): runs the exact
  `0x100870`, `0x16a060` and `0x100ed0` code.
  - Test31 image: a shatter for a place with no piece **calls fracture with
    the stack residue `0x7ff77ff7`** — the returned dump mechanism. An
    out-of-range server place faults on its unbounded record read
    (`0x16a0a0`) at a guard page placed after 27 records.
  - Test32 image: 15 owned-piece shatters across 1–1051 pieces fracture the
    owning piece exactly as before; 5 missing-piece shatters skip fracture and
    are counted; 27 places produce identical damage accumulation and 54
    weaken/destroy state publications; 5 out-of-range places are rejected with
    the native caller recorded; 4 ignored releases are recorded with the
    correct piece and caller while an active release is unchanged.
- All nine existing native suites pass with the Test32 plan (runtime,
  resources including 100,000 glass lifetime operations, iterator, animation,
  history, awareness, threat, crowd with 1,680 packed-record comparisons,
  fracture). Consumer audit passes.
- 19 C++ and 31 Python tests pass. The DLL is AMD64 PE32+, exports the same
  ten D3D9 entry points and links no external GCC/C++ runtime.

## Limits

The guard stops the demonstrated corruption at its first native step. It does
not explain why a shatter request reached an absent or nonexistent place.
The static command watch is new Windows code that the Linux fixtures cannot
execute; if it misbehaves, report it. None of this is a Windows gameplay test,
and 256 actors are not established as stable.

## Install and retest

Follow the [Test32 instructions](test32-testing-protocol.md).
