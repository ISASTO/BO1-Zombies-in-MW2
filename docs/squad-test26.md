# Test26: native threat stack and high-index sentient consumers

Version: `0.1.25-squad-test26`. Live capacity remains **256 actors / 274
sentients**, with 64 native corpses and the existing version-1 checkpoint format.

The owner reports much better Test25 animations and stability around 50 spawned
friendlies, with further failures near 60 and 90. The matching returned archive
contains two new crashes with the published Test25 DLL hash.

| Returned run | Last completed spawn population | Evidence |
| --- | ---: | --- |
| Estate, 15:49:17 | 56 active actors | The crash-only error capture preserves `BG_EvaluateTrajectory: unknown trType: -1111617280`. Its bits are float `-0.0928058624`, rather than a trajectory enum. A saved native argument identifies client entity 1202. The reported exception occurs later during error cleanup. |
| Contingency, 15:53:23 | 98 active actors | Native allocator `0x10047c` reads its free-list head as `0x3f161262`, also a float (`0.586218`). Its index/address arithmetic matches the captured invalid read. |

These observations show memory corruption. The exact native routine inspection
then exposes a definite source of corruption in Test25, independently reproduced
by [the threat probe](threat-test26-probe.json). The fixture does not reproduce
either complete Windows crash chain, so causal attribution of both symptoms
remains an inference pending the owner retest.

## Changes

| Native path | Test25 defect | Test26 correction |
| --- | --- | --- |
| Threat selection, `0x156e00..0x1573cd` | A 50-row stack array at `RSP+0x70` receives every eligible opposing sentient from the expanded iterator. Rows are 24 bytes. Row 51 begins at `RSP+0x520`, overwriting saved XMM7/XMM6; subsequent rows overlap saved GPRs and caller data. | Keep all 274 rows. Grow the frame from `0x568` to `0x1a68`, move all 25 upper-frame references and change all three exits. Preserve native best-target tracking and the complete secondary candidate list. |
| Threat frame unwinding | Increasing the stack subtraction alone would leave Windows frame restoration incorrect and could skip a guard page. | Update eight native unwind descriptions covering 13 function ranges. Touch both new stack pages in order. Register the probe's own `RUNTIME_FUNCTION` before any native entry reaches it; registration failure leaves native code intact. |
| Biased sentient handles | Six image-relative resolutions still use original `sentientPool-0x88`. High handles can resolve original entity/global memory rather than a relocated sentient. | Redirect all six to the relocated pool bias. Keep nearby RIP-relative dvar references unchanged. |
| Recent threats, shared knowledge and melee scans | Three perception vector bases and five bounds still use the old inline/50-sentient representation. | Use appended perception records and traverse all 274 sentients. Original corpse/weapon/stack structures with similar offsets are preserved. |
| Entity reference cleanup | An unrolled two-sentient loop still clears only the original pool. | Retain its native field/stride logic over all 274 relocated sentients. |

No new recurring background scanner or per-frame logging was added. The existing
mod AI service, native behavior, original multiplayer hitmarkers, menu features
and animation ownership fixes remain included.

## Verification

The probe applies the published checked patch plan to the exact owned AMD64
executable in a disposable mapped image, with an arena outside that image.
Original game files and dump memory are excluded from the installer/source.

- Reproduces Test25's saved-register corruption at **candidate 51**.
- Passes **112 native threat cases**: 0, 1, 32, 49, 50, 51, 59, 60, 90, 97,
  128, 255, 256 and 273 candidates, each repeated eight times.
- Checks eight saved GPRs and ten saved XMM registers per case, complete
  candidate records, normalized direction data and the highest-scoring target.
- Instruments the three native exits and confirms each specific path restores
  the correct frame and saved registers.
- Checks **273 native recent-threat searches**, **1,644 native handle
  resolutions** and entity-reference cleanup across **274 sentients**.
- Cross-checks all 13 native unwind ranges against actual patched save
  instructions and verifies three partial-prolog states of the relay.
- Passes earlier animation/tree ownership, guarded iterator, native shooting
  history, complete population save/load, legacy migration, snapshot and
  linked-handle regressions. All **18 C++ suites / 31 Python tests** pass.
- Builds the AMD64 proxy with its original 10 exports and seven system DLL
  dependencies. The packaged PowerShell installer/uninstaller passes version,
  DLL hash, save backup, local MP texture extraction and settings preservation.

Native score/world predicates, secondary threat reduction and enemy commit are
isolated in the threat fixture; actual candidate construction, perception lookup,
team traversal and best-target tracking execute. Windows guard-page growth,
`RtlVirtualUnwind`, complete missions, skeleton rendering and maximum stable
population have **not** been executed in this Linux workspace.

Unwind format/API checks use Microsoft's primary references:
[x64 exception handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170)
and [RtlAddFunctionTable](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable).

## Owner retest

Close MW2, remove the previous wrapper with its Uninstall script, then extract
and install the complete Test26 package. Start Estate and Contingency and
increase the squad through 50, 60, 90 and 100 while nearby enemies are active.
Continue higher if that remains stable, then check autosave, checkpoint reload,
death and Remove Spawned Actors. If it fails, run Diagnose/CollectLogs so the
new DLL receipt and matching exception dump are retained.

The 256 live pool stays enabled; native historical shot rewind remains 32
records, with higher actors using their current server poses during shots.
This release repairs a reproduced corruption mechanism, not a claim of proven
256-actor Windows gameplay stability.
