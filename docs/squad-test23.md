# Test23: mission-load actor-iterator crash

Version: `0.1.22-squad-test23`.

The three new Test22 dumps from 13:39:45, 13:40:24 and 13:40:43 all fault in the
same native actor-team iterator at executable RVA `0x1286e0`. Their exception
contexts show actor index **451**, an actor-pointer offset of `0x802a48` and an
active-flag read at `0x803f3f` relative to the relocated pool. That read is beyond
the entire 8 MiB allocation. This is a concrete expansion-patch bug, independent
of spawned friendlies or hitmarker activation. The returned DLL hash matches
Test22, and the MP asset initialization succeeded in the logs.

## Cause and correction

The original loop guard starts at `0x1286fe`:

```asm
49 83 f8 20    cmp r8, 32
7c dc          jl  loop
```

Test22's site list mistakenly used `0x1286ff`, one byte into that instruction.
Decoding from there loses the `49` REX prefix and produces `CMP EAX, 32`. The
expanded relay consequently compared EAX, the low DWORD of the current actor
pointer, against 256. When the address's low DWORD is negative as a signed value,
the loop keeps advancing past actor 255. At positive low DWORDs it instead stops
prematurely and misses later actors.

Test23 starts the replacement at the actual instruction boundary and preserves
`CMP R8, 256`. It retains all 256 actors, 274 sentients, existing animation and
snapshot changes, original MP hitmarkers and checkpoint format version 1.
It does not reduce capacity to hide this crash.

The patch generator now rejects capacity guards that begin inside an original
instruction. Existing partial immediate/displacement edits remain valid because
they intentionally modify scalar fields, not control-instruction entry points.
The regenerated runtime plan audits all existing capacity-guard sites.

## Verification

The new test assembles the actual old and corrected runtime patch headers into
private images with an external arena. It executes the original native iterator
without engine callback substitutions. An inaccessible page range immediately
past the 256-actor pool turns an overrun into an explicit failure. Fixed,
non-overwriting mmap placements exercise both pointer-sign cases.

- Old Test22 reproduces the fault at `0x1286e0` on actor 256 at the guard boundary.
  The retail allocation had readable intervening data, so the returned crashes
  reached index 451 before faulting.
- Old Test22's positive-pointer case reproduces missing later actors.
- Corrected bytes pass 1,026 cases per address layout, **2,052 total**, covering
  empty pools, sparse high slots, no matching team, dense iteration and exhaustion.
- 18 C++ suites and 31 Python tests pass, including instruction-prefix validation.
- Expanded native save/load, legacy migration, client snapshots, animation
  lifecycle and linked sentient-handle tests continue to pass.

[Returned crash evidence](runtime-test23-crash-evidence.json),
[guarded native iterator regression](iterator-test23-probe.json), and
[expanded runtime regression](expanded-runtime-test23-probe.json) record the
scope and limitations. Fault contexts come from the minidump exception stream;
thread-list contexts are later handler snapshots. The captured patch bytes and
verified Test22 source identify the relay; private relay memory was not included
in these dumps.

The DLL builds for Windows AMD64. Actual Windows mission loading after this
fix, compressed checkpoints and crowded-AI gameplay remain owner tests.

## Install and next test

Close MW2, uninstall Test22, extract the entire Test23 ZIP and run `Install.cmd`.
The installer backs up `players` under `bo1mw2/backups/before-test23-*`.
No game EXE, fastfile or IWD is modified on disk. Test22's expanded checkpoint
layout is unchanged, so there is no format migration between these two builds.

First load Estate and Contingency normally, before spawning additional actors.
Confirm the AI count row shows `/ 256`, then test saves/death/reload and larger
populations. Use `CollectLogs.cmd` afterward or `Diagnose.cmd` if startup fails.
