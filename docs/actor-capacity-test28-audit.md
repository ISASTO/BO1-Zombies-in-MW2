# Test28 capacity and resource audit

Test28 carries forward the [Test27 consumer inventory](actor-capacity-test27-audit.md)
and reruns its exact-native checks against the current generated runtime plan.
The [machine-readable audit](actor-capacity-test28-audit.json) records those
checks and both new resource dependencies.

| Domain | Current capacity/storage | Verification |
| --- | --- | --- |
| Live actors | 256, stride `0x48c0` | 45 stride constants, nine byte spans; native allocation/guarded iteration |
| Sentients/perception | 274; 274 appended 48-byte records per actor | 72 relocated sites; 52 sentient-index data-flow accesses; all actor slots in awareness tests |
| Threat/awareness scratch | 274 rows each | 24-byte and 16-byte rows; saved registers, exits, chained native unwind descriptions and ordered page probes |
| Snapshot | 16-bit map; 256 actor + 64 corpse indices | All 320 goal pairs and 256 client team cases; coordinated 17,412-byte producer/copy/readers |
| Animation trees | 256 actor, 64 corpse, 336 temporary | Unique leases, recycling, all actor/corpse ownership swaps and bounded corpse searches |
| **Client skeletons** | **9 MiB owned storage** | Both allocation paths and frame reset use matching storage/bounds; native DObj creation/cache and end checks |
| **Server skeletons** | **8 MiB owned storage** | Four frame/wrap base sites; both bounds; native generation/cache invalidation and explicit wrap test |
| **Glass pieces** | Original native pool and geometry budgets | Current/next free links checked against occupancy; inactive-link repair; duplicate/out-of-range free rejection; 100,000 native lifetime operations |
| Native checkpoints | 256 actors / 274 sentients, version 1 | Population save/load, 70,144 pointer fixups and stock 32/50 migration |
| Shot history | 32 historical records | Producer, validation and three restore/rewind spans agree; higher live actors use current poses |
| Resource guards/unwind | Nine arena guards + two skeleton guards; four registered relay entries | Disjoint arena layout, inaccessible pages in native fixtures, native prologue/unwind agreement |

The deeper binary sweep finds five direct skeleton-array reset/base references
and four skeleton allocation/wrap bounds. All are coordinated. The remaining
references use the native pointer/generation globals, which are preserved.
Other occurrences of `0x80000`, 32, 50 and similarly valued graphics/FX/team
constants are not blanket replacements.

Glass repair does not increase glass capacity, alter live transforms, remove
actors, suppress the entire effect, or skip native geometry checks. The precise
first retail corruption of the free chain remains unobserved; its invalid read
is reproduced and guarded, and future crash capture includes the missing
bitmap/metadata evidence.

The original stock/bootstrap/migration sizes and intentional fixed domains
listed in the Test27 audit remain unchanged. These local consumer checks do
not formally prove all transitive engine behavior or 256-actor Windows mission
stability. See [Test28 findings and test scope](squad-test28.md).
