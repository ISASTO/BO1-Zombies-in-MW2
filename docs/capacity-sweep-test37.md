# Test37 capacity sweep (32 → 256)

The owner asked for a full examination of every reference that raising the
actor limit from 32 to 256 can throw off, done in several passes. This is that
sweep.

It is a static inventory of the exact `iw4sp.exe` (SHA256 `fb7e8a98…`): a
linear sweep of all 821,614 instructions in `.text`, checked against both
generated patch headers. The automated passes are in
`tools/audit_capacity_sweep.py`, with output in
[capacity-sweep-test37.json](capacity-sweep-test37.json). The new findings
are reproduced with the engine's own code in
[script-vm-test37-probe.json](script-vm-test37-probe.json).

**Validation of the sweep itself.** Run against Test36's patch set, the sweep
fails on exactly the sites fixed below. On Test37 it passes.

## Findings

| # | Finding | Stock limit | Effect at 100–256 actors | Status |
| --- | --- | --- | --- | --- |
| 1 | Object-key variable names decoded as `< 0x19000` in 6 places | object ids < 0x9000 | Script threads waiting on a notify lose their reference once their id is ≥ 0x9000, and checkpoints skip them. Reused ids → VM resumes non-threads (crash at `0x227ddc`), double releases → false "exceeded child/parent" errors. **This is the Test35/36 failure.** | fixed |
| 2 | Array-index validity `index+0x7e7000 <= 0xfe6fff` in 4 places, built on the same constant | — | Int keys would collide with the new object-key range | fixed |
| 3 | Checkpoint loader reads saved object numbers as signed 16-bit (`0x2193b6`) | 32,767 saved objects | Loading a checkpoint holding ≥ 32,768 script objects reads far outside the save map and crashes. Stock MW2 never gets there; 200+ actors would. | fixed |
| 4 | Entity/sentient handle records | 4,095 (fatal "ENT_HANDLE_COUNT exceeded") | Every set handle on every actor holds one (10–15 fields per busy actor) | expanded to 32,767; logged, guarded, shed |
| 5 | Model lighting patches per frame | 4,096 (fatal) | About one per drawn model | logged, guarded, shed |

## Pass 1: script VM id space

**Derived constants.** Every immediate and displacement equal to a value
derived from the stock layout, or within ±2 of one, was collected. That
covers:

- 0x9000 parents, plus ×2, ×4, ×8 and ×24 of it
- the 0x9002 child base
- the 0x19000 object-key end
- the 0x7e7000/0xfe6fff array-index bounds
- pool bases and list ends
- the 0x12068 map offset

That gives **251 hits:**

- **227 patched.**
- **5 in the replaced stock `InitVariables`** (dead code).
- **19 in other subsystems**, each read in context: renderer and D3D sizes,
  a sound table, hash state, UI flags, the debug overlay's "num parent vars"
  display, and the client skeleton allocator, which the capacity rows
  already patch.

The 10 unpatched VM sites (findings 1–2) were the only real ones.

**Signed 16-bit reads in the VM and save code.**

- `0x2193b6` was the only one on an object number (finding 3).
- `0x218cb7` reads an entity class number.
- `0x218f0d` is a genuinely signed int key.
- The 0x228e–0x22bb block reads the script compiler's parse tables.

**Base-register accesses.** This is the Test36 class of bug: a register
holding `scrVarPub`, then `+disp`. It is traced across the whole `.text`; the
generator fails on any unhandled one.

**Other name-range boundaries.** All 86 uses of 0x10000, 0x800000 and
0xffffff in the VM were listed. Int-key names are built either from
validated indices (the 4 patched checks) or from small engine-internal ones
(entity numbers), so none is affected.

**Id allocation order.** `AllocObject` balances new objects by child-pool
load. Childless script threads therefore pile onto one free list, whose ids
rise in steps of 8. That is why ids ≥ 0x9000 appeared with only 24–30k
objects live.

## Pass 2: relocation completeness (actor capacity and VM)

For each stock address that the original bytes of a patch row reference and
the patched code no longer does, the sweep checked every instruction in
`.text` that reaches it: rip-relative, image base + displacement, or a
register loaded with `lea` (followed through register copies and across
calls for callee-saved registers).

**47 relocated targets: 0 unpatched references.** 12 references are
numerically equal but belong to other objects:

- **`0xd9c4f8`:** a qword global that sits where `sentients[-1]` would be.
- **`0xf4e1a0`:** the animation-globals root. Only its per-actor tree,
  temporary-tree, flag and cursor sub-arrays are relocated. The neighbouring
  64-corpse records (`+0x1fa0`) and the level-start clear (`0x171593`) are
  correctly untouched, and `0x170b20` re-creates and clears the relocated
  arrays (verified: 256 trees, 336 temporary trees, 336 flag bytes).

**Snapshot frames.** All 10 direct references to the stock server and client
snapshot frames are patched.

**Client actor-slot consumers.** `0x12c6a0` is the source of 0–255 actor
slots in cgame. Its only callers are the compass (`0x92c32`, Test33) and the
compass slot lookup (`0x92ded`), which the Test33 relay covers. The goal
readers `0x15cec0`/`0x15cee0` return pointers into the expanded tables.

**Static arrays sized for 32 actors or 50 sentients.** Every indexed static
array was sized by the gap to the next referenced address (89 candidates).
The ones in game code are the 32-slot dropped-weapon ring (not per actor),
per-entity and per-model tables, enum/string tables and a state-transition
table. None is indexed by actor or sentient.

**Loops and bounds of 32/33/50** in functions the capacity patches touch were
all classified:

- stack adjustments
- 32 config-string slots in checkpoints
- the FX save groups (kept stock by design)
- 50 ms time deltas in shot history
- the dropped-weapon clear

## Pass 3: per-actor bit masks

The sweep covers 113 variable shifts and register bit tests in the AI range.
The only 3 memory bit tests in `.text` are outside game code. Each count
comes from one of these:

- team fields or team-name lookups
- entity-type masks
- script option tables (3 and 6 entries)
- entity attachment slots (31)
- player client numbers (0–1)
- a 20-entry name table scan
- byte packing

**None is indexed by an actor or sentient**, so nothing wraps at 32.

## Pass 4: native capacity limits

The census covers every fatal or drop error string and every renderer
"not drawing" warning.

| Limit | Stock | Test37 | Status |
| --- | --- | --- | --- |
| Script parent objects | 36,856 | 65,526 | expanded (Test35); guard + shedding |
| Script child variables | 102,404 | 409,592 | expanded (Test34) |
| Object-key names / saved object numbers | ids < 0x9000 / signed | full 16-bit | **fixed in Test37** |
| Entity handle records | 4,095 | 32,767 | **expanded in Test37**; telemetry, guard, shedding |
| Model lighting patches per frame | 4,096 | 4,096 | about one per drawn model; **telemetry, guard, shedding added** |
| Entities | 2,046 | 2,046 | shared helper spawners; guard + shedding |
| Script strings | 65,536 | 65,536 | strings only (pointers on the owned heap); 22k used at 150 actors |
| Anim info / anim transfer / dobj transfer | 4,095 / 2,048 / 1,024 | 16,383 / 16,384 / 8,192 | expanded (Test33/34) |
| Checkpoint buffers | 1 MiB | 32 MiB | expanded (Test34) |
| Client/server skeleton memory | 576/512 KiB | 9/8 MiB | expanded (Test28) |
| DObjs | 2,047 | — | one per entity, so bounded by entities |
| Renderer scene/model/surface limits | various | — | counted by `AI_RENDER_OVERFLOW`: all 0 at 150 actors |
| Dropped weapons | 32 | — | oldest recycled, not per actor |
| Script FX ents | 768 | — | created by map scripts |
| Snapshot archive message | 16 KiB | — | carries glass/dynent changes, not actor states |
| Sound restore buffer | 16 KiB | — | bounded by sound channels |

## Pass 5: the mod's own code

`src/` and `include/` were reviewed for fixed sizes and bounds:

- The remaining 32s and 50s are stock-layout migration sizes, bootstrap
  checks, font/texture sizes and bit widths.
- The AI service uses `NativeActorCapacity()` (256 when expanded).
- Owned-actor storage is dynamic.
- The VM usage scan uses the generated parent bound.

## Not covered

- Behaviour inside campaign GSC scripts beyond engine limits.
- Performance at 256 actors.
- Windows gameplay. The fixtures run the exact native code on Linux, with
  substitutes only for the error reporters and the memory-file byte stream.
