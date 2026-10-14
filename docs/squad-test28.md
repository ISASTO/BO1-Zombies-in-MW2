# Test28: crowded skeleton storage and glass lifetime guards

Version: `0.1.27-squad-test28`. Live capacity remains **256 actors / 274
sentients**, with 64 corpse records. The original MP hitmarker update remains
included.

## Returned Test27 crash

Both uploads contain the same new Estate crash at 97 total actors. The fault
is a direct invalid read in the native glass-piece allocator at `0x10047c`,
with no earlier native engine error recorded. Its free head is `0x3f33787f`.
Those bits encode the float `0.7010573744773865` and match a captured glass
transform's quaternion. The allocator treats them as an index and reads
`piecePlaces + index * 32`. This is independently the same failing allocator
seen in the earlier Test25 Contingency dump, with a different float-valued head.

The returned dump does **not** capture the first write that damaged this
chain. A skeleton-buffer overwrite of glass is not established. See the
[numeric evidence](runtime-test28-crash-evidence.json).

## Skeleton dependency found in the deeper sweep

The live actor pools and animation trees had grown, but the native per-frame
skeleton storage had not. The actual unmodified native allocators demonstrate
these independent limits:

| Resource | Stock storage | Test28 storage | Stock behavior under maximum-size allocation load |
| --- | --- | --- | --- |
| Client skeletons | 576 KiB | 9 MiB | 72 allocations, then allocation fails and skeleton calculation is skipped/raises the native capacity error |
| Server skeletons | 512 KiB | 8 MiB | Allocation 65 wraps to the first block and changes the skeleton generation |

The number of visible soldiers at exhaustion depends on their bone counts,
other models and repeated evaluations. These are allocation counts, not a
claim that every mission must fail at exactly 64 or 72 actors. Repeated cache
invalidation/exhaustion is a plausible source of the reported visual phasing;
actual rendering remains an owner test.

Both larger buffers are owned allocations with inaccessible end pages. All
five native frame/reset base sites now load the owned pointers, and all four
allocation/wrap bounds match the new storage. Changing only a bound on the old
static array would overrun adjacent globals. Native atomic client allocation,
server wrap/generation handling and DObj cache checks remain intact. No
per-frame allocation, actor removal or new periodic repair scan is introduced.

## Glass allocation and release

The guarded native allocator checks that both the selected free index and its
next link are in range and inactive in the native occupancy bitmap. It detects
an active transform being used as a link before reading its quaternion as an
index. On a damaged link, it rebuilds the free chain using **only inactive
piece-place records** and recomputes the active census from the bitmap. Active
transforms, geometry records, occupancy bits and native geometry budgets are
preserved. A full pool still returns the native exhausted sentinel.

The native release path ignores out-of-range and already-inactive releases,
preventing a second release from creating a cycle and reissuing one live piece.
The old native code reproduces that double-free alias, but the dump alone does
not prove a double free was the first retail cause. Rebuilds use the existing
serialized native allocation context and occur only when a link fails checks.
Normal allocations perform bounded checks without scanning the pool.

Both body-only relays have standalone Windows unwind descriptions matching
the already-executed native prologues. All four relay entries are registered
before publishing hooks. If either skeleton allocation, its guard protection,
native-byte validation or unwind registration fails, expansion stays disabled
and the unpublished allocations are released.

Ten-second diagnostics add skeleton usage/generation and glass repair counters.
Crash-only dumps now retain glass metadata, occupancy and piece-place/state
ranges as well as skeleton counters. Normal gameplay does no extra dump copying
or disk flushes.

## Validation

The [resource fixture](resources-test28-probe.json) executes the real native
Windows-ABI routines in Linux, using the exact checked runtime patch plan at
an externally relocated arena:

- Reproduces the returned free-head fault at **the same native instruction**.
- Reproduces the stock skeleton limits and native double-free live-piece alias.
- Passes 30,720 skeleton allocations over 16 frames, including three passes
  over all 256 actors plus 64 corpses with 255-bone allocation sizes each frame.
  Live ranges stay distinct and the server generation does not wrap within
  those frames.
- Passes 15,680 native DObj create/cache/reuse/frame-invalidation cases, including
  the client's separate inlined allocator, plus both end boundaries and a
  deliberate server wrap.
- Passes 100,000 deterministic glass lifetime operations, 143 corrupt-chain
  cases and 8,172 duplicate/out-of-range releases. Live transforms remain
  unchanged by repair; full-pool and geometry exhaustion still work.
- Audits nine native saved registers and both new unwind descriptions against
  the original prologues and simulates their stack restoration.

The expanded awareness, threat, iterator, animation ownership, shot history,
population snapshot and native population checkpoint regressions also pass.
The [consumer audit](actor-capacity-test28-audit.json) retains all 45 actor stride
checks, nine byte spans, 72 relocated perception sites and 52 indexed data-flow
sites. Historical shot rewind remains a coordinated 32-record representation;
higher live actors retain current server poses during shots.

The native fixtures isolate world/renderer/physics callbacks as specified in
their reports. They do not boot Windows missions, evaluate real skeleton assets,
or execute Windows `RtlVirtualUnwind`. The exact corrupt-head read and the
capacity dependencies are reproduced; **error-free 256-actor retail gameplay
is not yet established**.

## Retail retest

Close MW2, uninstall the previous mod DLL and install the complete Test28
package. Start Estate or Contingency fresh through Mission Select for the first
test, so the run does not inherit state from a previously corrupted checkpoint.
Raise the crowd through the former phasing range, then toward the shared
256-total-actor capacity. Check crowd movement, gunfire/breakable glass,
removal/replacement, autosave and death/reload. If the behavior recurs, return
the new Diagnose ZIP and the approximate count where phasing first appeared.

The installer backs up local player/save data. Version-1 expanded checkpoint
format and stock checkpoint migration are unchanged.
