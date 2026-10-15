# Test34: script variable pools and the other fatal native limits

Version: **0.1.33-squad-test34**. Actor capacity remains 256 / 274 sentients.
All Test31–Test33 guards remain.

## Returned Test33 result

What the owner saw:

- **Estate** froze at **179 actors**, with the dialog "exceeded maximum
  number of child1 script variables".
- **Contingency** showed the same dialog at **about 111 actors**.

Both runs got well past the old walls: ~160 on Estate in Test32, ~100 on
Contingency.

What the log shows (`latest.log`, Contingency, installed DLL `699561c3…`,
[evidence](runtime-test34-evidence.json)):

- The last spawn line was `AI_RESULT result=CREATED owned=110`.
- `AI_ANIM_INFO` peaked at **2,445 / 16,383**, so the Test33 animation pool
  has plenty of headroom.
- `AI_GLASS_STATE missing_place_shatters=0`.
- `AI_COMPASS rejected_actor_slots` kept climbing. That is expected: the
  compass fix is working.
- `AI_STATIC_WATCH_HIT` fired four times, all at startup from `0x1eedd3` and
  `0x1eede1`. That is native command registration storing addresses inside
  the image. It is not corruption.

## Cause

The engine's script VM keeps every script value in one 24-byte variable list.
The list has two parts:

- **Parents.** Objects, arrays, structs and threads take entries 1–0x9000,
  spread over interleaved free lists.
- **Children.** Fields and array elements live in **two hashed pools of
  0xc801 entries**. An object's children go into pool `(object id & 1)`.

Relative child ids are 16-bit, so a pool cannot simply grow. The 64-bit port
split children into two pools for that reason. `AllocObject` places each new
object in the less-used pool.

Every actor adds script children: entity fields, `self.a` structs, notify
lists and thread state. The stock capacity is 2 × 51,199 = 102,383 children.
Estate hit that at 179 actors, which is at most about 570 per actor (the
level's own variables are included in that figure).

The native fixture reproduces this with the exact native code. Stock
`InitVariables`, `AllocObject` and `GetNewVariable` stop at **102,383
children** and raise exactly the returned error: "exceeded maximum number of
child1 script variables".

## Changes (applied in DllMain, before the game starts)

The proxy is a static import of `iw4sp.exe`, so its `DllMain` runs before any
game code. All edits below are checked against the exact original bytes
first. If any byte differs, nothing is written and the game runs stock.

1. **Eight child pools: 409,592 children (4×).**
   - The variable list moves to an owned 10.7 MB region with a guard page.
   - All 291 native references to the list are retargeted.
   - All 111 native `parent → pool` selectors and the 3 parent free-list
     heads change from `& 1` to `& 7`.
   - Parents use eight interleaved free lists.
   - Each pool has its own counter. `AllocObject` places new objects in the
     least-used pool that still has free objects.
   - Pools 0 and 1 stay at their stock positions, so every native offset
     folded against them stays valid.
   - Stock code only named pools 0 and 1, so exhausting any other pool would
     have continued with no error. Exhaustion of any pool now reports the
     native error, so it can no longer corrupt memory silently.
2. **The script memory tree is reserved for strings.**
   - The native tree is 1 MiB (65,536 nodes). Script string ids are node
     indices that must stay below 0x10000, so the tree cannot grow.
   - Waiting threads' saved stacks, arrays and savegame buffers use the
     pointer allocator `MT_Alloc`, and their share grows with actor count.
     These now come from an owned heap: 512 MiB reserved, committed in 8 MiB
     steps, with the same power-of-two size classes. That leaves the whole
     native tree to strings.
   - `MT_Free` routes each block by its address.
   - `MT_Init` resets the owned heap together with the native tree.
   - Each block has a header. A stale free (after `MT_Init`), a double free
     or a foreign pointer is rejected instead of being put back on a free
     list.
3. **Checkpoint slots: 1 MiB → 32 MiB each.** Overflow was the fatal error
   "MemFile_StartSegment: Out of memory". A crowd of 256 actors at roughly
   17 KB each would not fit in 1 MiB.
   - The two static buffers are replaced by guarded owned buffers.
   - The six native `lea` sites become loads from owned cells.
   - All seven size constants change, including the loader's save-file
     limit.
4. **Dobj transfer records: 1,024 → 8,192.** The per-frame
   "exceeded maximum number of dobj transfer info" error was `ERR_DROP`. Its
   seven references are retargeted and the bound raised.
5. **Spawn resource guard.** Before every owned spawn the mod checks headroom
   in each fatal native pool. If any pool is too close, the spawn is refused
   with `AI_RESULT result=RESOURCE_GUARD`, and `AI_RESOURCE_GUARD
   refused=<pool>` names it. Spawning one more actor can no longer trigger a
   fatal engine error. The pools checked:
   - all child pools: 8,192 entries headroom
   - the fullest child pool: 2,048
   - parents: 1,024
   - string nodes: 4,096
   - entities: 128 free of 2,046. The entity table cannot grow: entity
     numbers are 11 bits throughout the engine.
6. **Diagnostics.** Every 10 seconds, and in each crash report, the log
   records children per pool, parents, string-tree nodes, pointer-heap bytes,
   entities in use, checkpoint slot use and dobj transfers:
   `SCRIPT_VM_USAGE`, `SCRIPT_MEMORY`, `ENGINE_LIMITS` and a `script_vm`
   crash line.

## Validation

- **[Native fixture](script-vm-test34-probe.json).** It runs the exact native
  `InitVariables`, `AllocObject`, `GetNewVariable`, `FindArrayVariable`,
  `RemoveVariable`, `RemoveRefToObject`, `MT_Init`, `MT_Alloc`,
  `MT_AllocIndex`, `MT_Free` and checkpoint-slot init.
  - **Stock:** 102,383 children, then "child1".
  - **Test34:** 409,487 children across 25,593 objects, all 409,487 found
    again by native lookup.
  - After `RemoveVariable` on 204,744 of them, the counters agree.
  - After releasing every object, all 36,856 parents are back on their
    correct free lists, and a refill reaches the same 409,487.
  - A single object in pool 5 takes 51,199 children, then the native error
    is reported.
  - `MT_Alloc` and `MT_Free` route to the owned heap.
  - Native index blocks return to the native bitmap.
  - Checkpoint slots publish 32 MiB buffers.
  - The stock list stays untouched, and no guard page is hit.
- **The generated C++ header was applied the way the DLL applies it,** with
  the region at a different address. It produced results identical to the
  manifest.
- **The generator checks every edit:**
  - all 291 list references are relocated (the code-wide scan matches)
  - the selector inventory accounts for all 110 pool `imul` sites
  - no data pointers or relocations point into the list
  - the edits do not overlap any of the 508 actor-capacity rows
  - all 99 runtime signatures the DLL scans for still match the same places
- **Existing suites:** all eleven earlier native suites and both audits pass.
  20 C++ and 35 Python tests pass, including new native tests for the heap
  and the region layout.

## Limits

- No Windows gameplay test.
- The fixture cannot run real level scripts, so how many objects (parents)
  each actor uses is unmeasured. `SCRIPT_VM_USAGE parents=` will show it,
  and the resource guard refuses spawns before the limit.
- The entity table (2,046) is a hard engine limit. Contingency logged entity
  numbers up to 1,697 at ~110 actors. A dense map may therefore stop below 256
  with `refused=ENTITIES` instead of crashing.
