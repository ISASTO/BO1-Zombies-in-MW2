# Test31: bound native glass-fragment construction

Version: **0.1.30-squad-test31**. Actor capacity remains 256, with 274
sentients. The Test30 renderer bridge and its expanded storage remain active.

## Returned Test30 result

The owner reported no preceding flickering and another crash around 97 actors.
The returned log confirms **97 total actors**. This is positive evidence for
the rendering change, although one run does not establish complete stability.

The new exception is at **iw4sp.exe+0x1095e5**, the RET of the native glass
piece builder. The return address and six saved registers contain repeating
glass vertex indices and 0xff polygon terminators. There are 700 consecutive
repeating bytes at the return slot. The piece header records **416 DWORDs**
of geometry for a native local array holding **231 DWORDs (924 bytes)**.
Its generated-fragment extent is 393 DWORDs; 23 preceding geometry records
account for the difference. See the [numeric evidence](runtime-test31-fracture-evidence.json).

The GPU, CPU, animated-surface, frame, handle and mapping failure counters
are all zero. The earlier non-finite query/feature guards also report zero.
This is a glass-fragment output overrun, distinct from the Test29 renderer
startup regression and the earlier invalid collision query.

## Changes

The native triangulator can return a two-vertex result. Edge bookkeeping can
then repeat that invalid result without consuming the remaining topology.
Its output writes and terminators previously had no effective capacity gate.
The native loop could overwrite its caller's saved registers and return slot,
then continue into allocation/publication using the oversized count.

Test31 keeps native geometry construction and adds these checks:

- Bound the native boundary-edge array and its input counts.
- Check capacity before either native polygon-output write.
- Reject fragments with fewer than three vertices, and bound the result to
  the existing native vertex representation.
- Include the polygon terminator and alignment padding in the output bound.
- Mark a failed piece and skip native allocation/publication for it.
- Record invalid fragments, output-limit rejections and invalid inputs in
  **AI_GLASS_FRACTURE**, included by the usual log collection.

Rejected output remains private scratch data. Valid pieces continue through
the original allocator and publisher. Four new body relays describe their
existing native frames with standalone unwind records. No relay grows RSP or
calls another function. The total registered non-leaf relay count is 46.

## Validation

The [native glass fixture](fracture-test31-probe.json) runs the exact owned
executable's polygon generator, edge bookkeeping and producer decision using
the current relocated runtime patches. It places an inaccessible page exactly
after the original 924-byte piece output boundary.

The Test30 baseline overruns that boundary while emitting repeated two-vertex
records, faulting at its native terminator writer **0x10ab15**. A full-buffer
baseline faults at its first native polygon write **0x109e9c**. These reproduce
the overwrite mechanism; the returned Windows dump faults later at RET.

The corrected fixture checks valid geometry byte-for-byte against Test30,
rejects repeated degenerate and overfull output, exercises invalid input and
boundary-edge counts, and runs the native parent producer to verify that a
rejected piece is not published while a valid one is published.

The retained actor/checkpoint, iterator, animation ownership, shot history,
awareness, threat, skeleton, renderer and collision fixtures are rerun with
the Test31 plan. Their detailed results and the compiler/installer checks are
included in this package and the repository.

This prevents the demonstrated output overrun. The first upstream geometry
update that produced the malformed topology was not captured. The fixture
uses documented memory/world/asset substitutions and does not run Windows
missions, real glass physics or actual rendering. Retail testing is still
needed to establish stability past the former 97-actor crash.

## Install and retest

Follow [Test31 instructions](test31-testing-protocol.md). Keep the initial
crowd test free of explosives, then perform the separate glass-impact test.
If it crashes again, collect logs before restarting and include the approximate
actor count and whether any flickering preceded it.
