# Test27 actor-capacity consumer audit

This audit covers the source under `src`/`include`, the existing native
relocation plan, direct pool/stride/span references, actor/sentient iterator
callers and an additional sentient-index data-flow scan of the owned executable.
The machine-readable inventory is [here](actor-capacity-test27-audit.json).
It is a reviewed consumer inventory, not a formal proof of every engine path.

## Coordinated live representation

| Resource or consumer | Test27 representation | Checks |
| --- | --- | --- |
| Actor allocation/traversal | 256 slots, stride `0x48c0` | All 45 inventoried stride constants and nine traversal spans checked. The three shot-restore spans deliberately stop at 32 records. Native allocation refuses slot 257; guarded iterator tests cover both signs of the pool pointer's low DWORD. |
| Sentients | 274 slots, stride `0x88` | 12 direct native storage references moved; team traversal, allocation, free/reference cleanup and linked handles exercised at high indices. The extra 18 slots retain the original non-actor reserve. |
| Perception | 274 appended 48-byte rows per actor | All 72 declared field sites and the common accessor checked. A separate data-flow scan finds 52 sentient-index-derived accesses, all relocated. Five late turret/script consumers were missed in Test26 and are corrected here. |
| Nearby awareness | 274 stack rows of 16 bytes | The old 50-row array independently reproduces an XMM8 pointer overwrite at candidate 51. Frame `0x3a0` becomes `0x11a0`; 13 RSP fields, the shared exit, five unwind descriptions and seven function ranges agree. |
| Threat selection | 274 stack rows of 24 bytes | Test26's separate repair retained and retested: frame `0x568` becomes `0x1a68`, with three exits, eight unwind descriptions and 13 ranges. |
| Snapshot actor/corpse mapping | 16-bit indices, 256 actors + 64 corpses | All 320 combined indices, goal IDs/state and all 256 client-team cases tested. Server/client frame copies use 17,412 bytes. |
| Animation ownership | 256 actor trees, 64 corpse trees, 336 temporary trees | 658 total native tree lifetimes including two other client trees; 336 distinct leases; release/reuse, all 256 actor/corpse swaps and bounded corpse searches retested. |
| Population checkpoints | 256 actor records / 274 sentients, appended pointer descriptors | Full native G_Save/G_Load population loops, 70,144 perception-node fixups, stock 32/50 migration and layout-marker rejection tested. Other checkpoint domains use isolated dependencies. |
| `ai_count` | Default 256, minimum 0, maximum 256 | The native registration still used 32 despite the relocated pools. The exact argument sequence now registers the expanded range; unrelated flags and description are checked. |
| Arena ownership | Eight disjoint data blocks and eight inaccessible pages | Guards follow the actor, sentient, server/client snapshot, animation, reference-head and count blocks. Native population and awareness fixtures execute with those pages inaccessible. Fast checked reads reject guards and wrapped spans. |

## Iterator caller review

The seven first/next-sentient caller groups have first-call sites at
`0x13e808`, `0x15215c`, `0x152f3e` (spawn), `0x156019`, `0x157043`,
`0x1b229c` and `0x1b5444`.
Two accumulate per-sentient stack records: awareness and threat selection.
Both now have coordinated expanded frames. The others stream a candidate,
update a perception record, copy knowledge or retain one selected candidate.

The 17 actor first/next call sites and their paired next-iteration sites were
reviewed in the bad-place/event dispatch, script array/notification, knowledge
copy, nearest-target, entity-reference cleanup and player-support paths. Their
actor loops use the expanded strides and bounds. The bad-place table itself is
an independent fixed table; it does not allocate one record for each actor.

## Remaining 32/50 values

| Retained value | Why changing it would be unsafe or unrelated |
| --- | --- |
| Original 32 actor / 50 sentient checkpoint counts | They decode legacy streams. Reading them as 256/274 would misalign the rest of a stock checkpoint. Expanded streams use their own versioned counts. |
| Initial 32 actor-tree and 112 temporary-tree copies/clears | These refer to the original fixed arrays before native expanded initialization. Copying 256 entries from those old arrays would consume adjacent globals and duplicate ownership. |
| Native 32-record historical shot buffers | Writer, interpolation, generation flags, caller scratch and three restore loops remain consistent. Actors above 31 retain current server poses. A full historical expansion is a separate coordinated format/stack change, not a prerequisite for live actors. |
| Fixed FX/vehicle/save groups | Their storage remains fixed and bounded. Actor/sentient pointer fixups have separate expanded bounds. Native population tests retain 32 records in unrelated groups. |
| 32-bit team masks | Bits identify teams, not individual actors. The existing teams fit; extending actor capacity does not require a 256-bit team mask. |
| 31 attachment slots, 32-bit graphics/hash values, 32-byte headers/VM structures, ASCII boundary, string buffers and placement distances | These are independent layouts, byte widths or presentation values. They do not cap actor allocation. |
| Original profile byte patterns | They identify the unmodified exact executable before patch publication. Altering the patterns to resemble patched bytes would break resolution. |
| `kAiNativeActorCapacity=32` status default and disabled-expansion fallback | This initializes an unresolved status and the safe stock fallback. Live admission, recovery, campaign reclamation and the displayed pool count use `NativeActorCapacity()`, which publishes 256 only after the complete transaction succeeds. |

References biased from `g_entities - 0x2a0` deliberately keep their original
addresses even where those addresses overlap the old sentient storage range.
They resolve entity handles, not sentient handles. Actual sentient biases and
reference heads use relocated storage; their 274-handle cases pass.

No recurring background scanner was added. Data-flow scanning and native
fixtures are offline tools; runtime guard pages add no polling workload.

## Validation boundary

The actual native routines execute in Linux x86-64 fixtures using the Windows
calling convention and the checked external-arena plan. Awareness additionally
applies PE loader relocations and executes native qsort and its comparator.
World visibility/geometry, skeleton evaluation, full mission scripts, Windows
stack growth and `RtlVirtualUnwind` remain outside these fixtures. A live stress
test in Estate and Contingency is still needed before claiming 256-actor
Windows gameplay is stable or error-free.
