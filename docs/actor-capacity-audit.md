# Test17: coordinated 256-actor attempt

Exact retail AMD64 executable SHA256:
`fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777`.

**Result: storage/allocation/iteration prototype passes; complete expansion is
blocked. Test17's DLL still uses 32 active actor slots.** No partial pool patch or
ai_count=256 edit is installed in the game.

## Executed isolated native probe

`tools/run_capacity_probe.py` verifies the private executable's hash, creates a
temporary mapped-image fixture and builds a Linux/x86-64 MS-ABI harness. It runs
the exact native allocator, an actor iteration loop and the perception clear
index/write path. Actor defaults execute natively. The memset dependency is
substituted; the iteration predicate only records visits and native callbacks
are skipped. Perception clearing jumps to the original epilogue before callbacks.
This is not a Windows game or gameplay test.

The prototype relocates actor/sentient storage and widens four short immediate
bounds through complete-instruction relays. Native allocation returns all 256
distinct slots, refuses allocation 257, and preserves both storage guards. The
native iteration visits all 256 slots, reaching index 255.

The unchanged perception path preserves a guard when clearing sentient index
49, but **index 50 zeroes 48 bytes beginning at actor+0xca8**, inside existing actor
state. This independently reproduces the critical memory corruption that a
pool-only expansion would introduce. Results: [native probe](actor-capacity-test17-probe.json).

## Remaining coordinated changes

| Dependency | Exact native evidence | Required before activation |
| --- | --- | --- |
| Actor storage/indices | Pointer slot 0xd96ea0, original storage 0xf04030, stride 0x1560; 73 direct pointer references, 45 stride constants and 9 original-pool byte-length constants | Relocate 1,400,832 bytes for 256 actors; reconcile every iterator/index consumer and unrolled reset. The probe covers only allocator and one iteration path. |
| Sentient storage | Pointer slot 0xd96e98, original storage 0xd9c580, stride 0x88, native limit 50; 117 pointer references and 12 direct-storage references | Relocate and reset enough sentients, including non-actor users. Preserving the original 18-slot difference proposes 274 sentients; actual mission demand still needs measurement. |
| Perception | 48-byte records accessed at actor+0x348+index*48; native clear routine 0x1247e0 corrupts the actor tail at index 50 | Redirect every inline address/copy/clear/save access to sidecars, or increase actor stride and translate every later field offset, descriptor and stack temporary together. |
| Snapshot | 0x174e50 stores actor indices in a byte map and uses corpse indices 32..95; goal/team arrays have 32 entries | Widen/relocate the map and all consumers. 256 actors plus 64 corpses require 9 bits, with highest index 319. Relocate all goal/team/helper arrays. |
| Animation scratch | Four direct references to pointer array 0xf50040; lookup 0x12c770 uses the native actor index | Relocate 256 pointers and coordinate construction, free/reset and load paths. |
| Checkpoints | Writer 0x180cf0 and reader 0x17f8e0 use 32 actor flags and 50 sentient flags. Pointer fixups at 0x1ae7d8/0x1ae822 separately enforce those maxima | Update counts, reference fixups, related fixed groups and perception descriptors with a format discriminator and a legacy checkpoint reader. |

The actor save table has indexed perception pointer fixups at offsets
0x370..0xc70 plus a separate entry at 0x330. The tool records their exact coverage
instead of assuming a larger descriptor table follows a larger pool.

The storage estimate reserves 3,366,912 bytes of perception sidecars. An inline
layout preserving all 274 records instead changes actor stride from 0x1560 to
0x3f60. Both choices require transitive code and serialization changes.

`tools/audit_actor_capacity.py` produces a read-only relocation/consumer
inventory and activation blockers. [Preflight evidence](actor-capacity-test17-evidence.json)
contains addresses and decoded constants. It is not a complete proof that all
transitive consumers have been found, and it never writes the executable.

## Runtime boundary

Test17 keeps campaign slot reclamation at 32 to avoid refusing mission spawns.
Reclaimed living friendlies recover at their previous clear positions where
possible. Autosaves no longer invoke that reclamation/rebuild path: the normal
32-slot native save format persists tagged actors/helpers.

A full expanded build must still exercise mixed-team targeting, animation,
corpse/free/reuse, snapshots, autosaves, old/new checkpoint loads and death in
the retail game. No 256-actor gameplay success is claimed by this probe.
