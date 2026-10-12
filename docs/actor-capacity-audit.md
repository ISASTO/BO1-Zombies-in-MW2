# Actor capacity expansion: outstanding native work

Authority: the supplied AMD64 executable SHA256
fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777.
This is a static dependency audit. Test14 does not enable pool expansion.

The 32 ceiling is part of the compiled actor allocation and indexing system.
Raising ai_count alone cannot create additional storage. The three returned
test13 crashes occur in animation-tree access at RVA 0x29a713 during script
execution; the dumps do not by themselves establish actor overflow as their
cause. Ownership loss and live-actor VM copying are separate failures addressed
by test14's cleanup and template changes.

| Dependency | Derived native evidence | Expansion work required |
| --- | --- | --- |
| Actor pool | Pointer slot 0xd96ea0; initialization selects static storage at 0xf04030; stride 0x1560; 32 slots = 0x2ac00 bytes | Relocate and initialize sufficient storage; audit every address/index consumer. |
| Allocation and iteration | Allocator 0x123950 and iteration/cleanup methods contain 32 bounds or 0x2ac00 end offsets | Change all relevant bounds together; preserve generation/free semantics. |
| Actor reset | Function 0x12c840 explicitly resets 32 active flags | Replace or extend all reset coverage before using additional slots. |
| Snapshot data | Function 0x174e50 iterates 32 and writes actor-index goal, team and helper-state arrays | Relocate or expand arrays and audit all readers; changing its loop alone overwrites adjacent storage. |
| Animation scratch | Actor-index lookup at 0x12c770 selects from 0xf50040 | Audit construction, allocation, every index reader and checkpoint persistence. |
| Saves | 0x17f8e0 and 0x180cf0 serialize/restore actor and related state; config-string groups also have fixed boundaries | Preserve a compatible save format or provide an explicitly validated versioned format and migration path. |
| Sentients/perception | Native sentient iteration bound 50; actor awareness records use 48-byte entries starting at actor+0x348 | Additional capacity needs compatible sentient storage and per-actor awareness layout, not only a larger actor pool. |

Before activating an expanded population, reproduce allocation past slot 31,
targeting in both directions, corpse/free/reuse, snapshot construction and
death/reload/save behavior on the exact retail executable. The current Linux
environment can check instruction and data relationships but cannot perform
that retail stress test.

Test14's capacity guard reclaims a mod-owned NPC when an ordinary campaign actor
initializes at an exhausted native budget. It leaves original mission NPCs out
of mod-owned cleanup. It is a protection for the existing budget and must not be
described as removal of the 32-AI limit.
