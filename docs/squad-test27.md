# Test27: nearby-awareness corruption and full consumer review

Version: `0.1.26-squad-test27`. Live capacity: **256 actors / 274 sentients**.
Original multiplayer hitmarkers and the existing menu features remain included.

## Returned failures

Both supplied archives contain the same two Test26 dumps, with the correct
published Test26 DLL hash. Estate last completed a spawn at 64 active actors;
Contingency reached 56. Both preserve a primary `BG_EvaluateTrajectory` error
before a secondary fault during error cleanup.

The invalid trajectory DWORDs resolve exactly to relocated sentient addresses.
Contingency's XMM8 low QWORD is that same sentient pointer, and its low DWORD
matches the invalid trajectory type. [Numeric evidence](runtime-test27-crash-evidence.json)
contains hashes and offsets without raw dump memory or private game paths.

## Correction

A separate nearby-awareness routine at `0x152100` still allocated only
**50 rows of 16 bytes**. Its expanded sentient iterator can produce more than
50 records. Entry 51 begins at `RSP+0x360`, which is also saved XMM8. Native
qsort includes that record; restoring the saved register therefore loads a
sentient pointer. This is distinct from the 24-byte threat buffer repaired in
Test26. The exact old Test26 runtime plan reproduces this overwrite.

Test27 grows awareness to 274 rows, moves all 13 upper RSP references, adjusts
the shared exit and five unwind descriptions covering seven function ranges.
The pre-existing R11 references still name entry RSP and need no displacement
change. Both expanded routines use ordered page touches before stack allocation;
their two relay unwind entries register before any native hook is published.
Registration or protection failure leaves the original native code intact.

The broader [consumer audit](actor-capacity-test27-audit.md) also corrects five
late indexed perception accesses and expands the native `ai_count` default and
domain to 256. Eight inaccessible pages separate the relocated data blocks;
checked reads exclude these pages. The old 32-slot menu hint is replaced with
an explanation that mission NPCs share the pool.

## Executed checks

- **1,264 native awareness cases**, covering every actor slot and **302,464
  candidate records**. Boundary counts include 31/32, 49/50/51, 59/60, 90,
  97, 128, 255/256 and 273. Four complete passes visit all 256 actor slots
  with 273 candidates each. Native sorting/comparison, distances/weights,
  refreshes, eight saved GPRs and ten XMM registers are checked.
- **1,370 cases** execute the five late perception instructions at all 274
  indices and verify unrelated actor bytes are preserved. The native dvar
  registration checks default/minimum/maximum and unchanged other arguments.
- Both frame/unwind inventories match actual native save instructions; partial
  relay prologs recover the original pushes in the static unwind simulation.
- Existing native threat/handle, animation/corpse, guarded iterator and shooting
  history regressions pass. Full population save/load and client snapshot
  fixtures execute with eight resource guard pages, including stock migration.
- **19 C++ suites and 31 Python tests pass.** Win64 build, DLL ABI and packaged
  installer/uninstaller checks are recorded in
  [build verification](squad-test27-build-verification.json).

Results: [awareness](awareness-test27-probe.json),
[threat](threat-test27-probe.json), [animation](animation-test27-probe.json),
[history/shooting](history-test27-probe.json), [iterator](iterator-test27-probe.json),
[population/snapshot](expanded-runtime-test27-probe.json),
[consumer inventory](actor-capacity-test27-audit.json).

The fixture reproduces the definite overwrite and the register/pointer pattern
seen in Contingency. It does not replay the final client-state write or either
complete Windows mission. Geometry/visibility callbacks are isolated; actual
Windows guard-page growth, runtime frame unwinding and skeleton rendering have
not been exercised here. **Error-free 256-actor gameplay is not yet established.**

## Installation and owner stress test

Close MW2, extract the complete Test27 ZIP, run its Uninstall.cmd to remove the
previous wrapper, then Install.cmd. The installer validates the executable and
DLL and backs up the local players directory to `before-test27-*`.

Start fresh missions from Mission Select for this retest. Test Estate and
Contingency through 50, 60, 90 and 100, with mission enemies
active. If stable, continue toward the **shared total capacity of 256**, which
includes existing campaign actors. Then test mixed teams, firing, actor deaths,
corpses, autosave, checkpoint reload, player death and Remove Spawned Actors.
Run Diagnose.cmd/CollectLogs.cmd if a new failure occurs so the matching build,
crash dump and recent log are retained.
