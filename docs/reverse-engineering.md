# Reverse-engineering ledger

Updated 2026-10-11. Evidence labels: **static confirmed** means verified in the
uploaded bytes; **runtime pending** means not yet observed in retail MW2. A
signature match is not a runtime test.

## Executable identity

| Property | Uploaded MW2 SP | Uploaded BO1 |
| --- | --- | --- |
| SHA256 | `fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777` | `2ba4a04e81de2b60582da0e4489efde21e6cb07904a223bae633b6ccaebbf759` |
| File size | 4,480,056 bytes | 8,099,608 bytes |
| Machine | `0x8664`, AMD64 | `0x14c`, I386 |
| COFF timestamp UTC | 2026-09-16 14:06:15 | 2011-09-01 23:06:57 |
| Preferred image base | `0x140000000` | `0x400000` |
| Entry RVA | `0x4908310` | `0x56da57` |
| SizeOfImage | `0x4942000` | `0x4277000` |

MW2 has ASLR, high-entropy VA, NX, x64 unwind data, and a `.bind` section. Its
CodeView path ends in `iw4sp_steam.pdb`; imports include `steam_api64.dll`,
`bink2w64.dll`, and D3D9 `Direct3DCreate9`. The supplied Bink DLL is also AMD64.
No usable product/file-version resource was found, so no old retail version
number is assigned. A PE timestamp is build metadata, not proof of installation
or publication date. The uploaded filename's `(1)` is irrelevant to its hash.

Historical x86 IW4/SP and IW4x addresses cannot be reused. BO1 remains x86 and
its offsets cannot be cast into this x64 MW2 runtime. No executable or Steam
wrapper is unpacked, replaced, or patched by this iteration.

## Address ledger: exact MW2 build

Every address below is relative to the loaded main module. The preferred VA
column is for static analysis only; ASLR changes runtime addresses.

| Assigned symbol / finding | RVA | Preferred VA | Evidence |
| --- | --- | --- | --- |
| `ClientCommand` candidate | `0x166710` | `0x140166710` | Command token comparisons include god/noclip; ECX client index, RDX text; stride `0x2a0` |
| `Cmd_God` | `0x165b20` | `0x140165b20` | ClientCommand god branch calls it; XOR 1 then stores entity flags |
| `CheatsAllowedAndAlive` candidate | `0x165680` | `0x140165680` | Checks cheat dvar boolean and `ent+0x164 > 0` |
| `G_Damage` candidate / god test | `0x168210` | `0x140168210` | Test bit 1 at `ent+0x150`, conditional jump bypasses damage path |
| `g_entities` pointer slot | `0xd96e78` | `0x140d96e78` | RIP load in Cmd_God followed by entity flag store |
| Static player-array base referenced by dispatcher | `0xd9e070` | `0x140d9e070` | Entity indexed by client number times `0x2a0` |
| `sv_running` dvar pointer slot | `0x13260c0` | `0x1413260c0` | Named registration, bool registration, RIP store |
| `mapname` dvar pointer slot | `0x19ada88` | `0x1419ada88` | Named string registration and RIP store |
| `Dvar_GetBool` candidate | `0x26bf90` | `0x14026bf90` | Type check at `+0xc`, bool current read at `+0x10` |
| `Dvar_GetString` candidate | `0x26c050` | `0x14026c050` | Current string pointer `+0x10`, enum handling at type 6 |

All rows above are static confirmed; observable retail behavior remains pending.
Native function names are assigned from semantics, not recovered symbols. None
of the listed engine functions are called by our DLL. RCX/RDX/ECX observations
are Microsoft x64 ABI evidence; they do not authorize using an old x86 typedef.

The God command toggles bit 0 and writes it to player entities. The mod changes
only that bit on slot zero through an aligned atomic XOR. The damage check
independently corroborates its meaning. It leaves other entity flags intact.

## Partial layouts used

| Structure / field | Offset or value | Evidence / limit |
| --- | --- | --- |
| MW2 entity stride | `0x2a0` | Client command indexing and God loop |
| Entity number | `+0x0`, 32-bit | Slot-zero sanity guard; runtime confirmation pending |
| Entity flags | `+0x150`, 32-bit | Native XOR/store and damage test |
| God bit | `0x1` | Native command and damage branch |
| Entity health | `+0x164`, signed 32-bit | Native cheat-alive guard |
| Candidate client pointer | `+0x110`, 64-bit | Damage path dereferences it; not relied upon or declared as a complete client structure |
| Dvar name pointer | `+0x0`, 64-bit | Registration/getter conventions; validated against exact string at runtime |
| Dvar type | `+0xc`, 8-bit | Getter type comparisons |
| Dvar current | `+0x10` | Bool byte or string pointer, depending on type |
| Dvar bool type | `0` | Getter treats type zero as bool |
| Dvar string type | `7` | String registration/getter family; runtime name/type validation required |

Only individual fields are read; no complete `gentity_s`, `gclient_s`,
`playerState_s`, or `actor_s` is assumed. Ammo and weapon layouts remain unknown.
Player identification by slot zero is supported by the dispatcher; mission and
checkpoint behavior are still local tests, not a claim of universal reliability.

## Resolution and level gate

The five patterns are centralized in `include/core.hpp`, with the static check
report in `profile-checks.json`. Each final pattern has exactly one match in the
uploaded `.text`. Registration name anchors and RIP-relative stores resolve
slots. Addresses are not copied from another client, platform, or build.

The supported hash must match before resolution. Unknown hash, zero/multiple
matches, wrong anchors, malformed PE data, unreadable memory, wrong dvar names or
types, or out-of-image entity pointers disable game-data operations.

Level state uses `sv_running`, nonempty `mapname`, slot-zero entity, and health.
It distinguishes no server, loading/unresolved fields, a dead player, and a
live player. God writes require the live-player state and writable aligned flags.
No renderer callback alone is treated as proof that a level is active.
No game-state flag is modified to make the level gate pass.

Remaining level-gate questions: initialization/unload ordering; whether any
front-end map can leave a running server and a live slot-zero entity; effects of
cutscene/player-entity swaps; and invalid/dead states during checkpoint reloads.
Use the next test log to resolve these before adding recurring or AI operations.

## Render/input and loading

Regular D3D9 factory vtable has 17 entries; CreateDevice is entry 16. Regular
device vtable has 119 entries; Reset 16, Present 17, EndScene 42, Release 2.
These are Windows COM interfaces, not inferred IW4 engine-function offsets.
Other methods forward through their original entries. The device object is not
wrapped in a replacement game-facing pointer. Shadow tables and records have
process lifetime. Failed hooks log and leave the underlying object usable.

The proxy performs expensive initialization when its exported factory/performance
function is called, outside our DllMain. DllMain only requests notification
suppression. System D3D9 is loaded from its absolute Windows system path.
The exact executable statically imports only Direct3DCreate9 from D3D9. Ten
documented/common exports are supplied, including unhooked Ex forwarding;
arbitrary undocumented D3D9 exports and other wrappers are outside test 1 scope.

WndProc is subclassed for F4 and menu navigation. Alt-F4 retains the system path.
Navigation is consumed while the menu is open; releases reach the game to clear
previously held keys. WASD, mouse, and firing pass through. Events are queued
under a mutex for frame-thread processing. Esc closes the mod at its home page.
The mod never sets a pause dvar. Renderer reset releases and rebuilds the atlas.

## Fastfile and asset observations

All supplied fastfiles start with unsigned `IWffu100`. MW2 zones have version
276; BO1 zones have version 473. The uploaded T5 streams inflate from file
offset 12. MW2 input bytes show additional header data and a zlib stream beginning
at offset 21; their typed assets have not been parsed or modified.

T5 GSC/CSC RawFile payloads found after inline asset names contain little-endian
uncompressed-size and compressed-size words followed by zlib. Source lengths,
EOF, and trailing NUL are validated by our scoped extraction tool. This yields
actual readable source from the user's supplied files. No decompiler output
needs to be trusted for those scripts. A full XAsset loader is still needed for
models, animation data, sound tables, and the final CSV overrides.

T5 XModel, XAnimParts, material/technique/shader, skeleton, and sound structures
are not accepted as IW4 structures. Nor does the new x64 engine prove a serialized
IW4 zone uses 64-bit pointer records. Asset conversion is unimplemented.

## Iteration log

| Iteration | Change | Verification | Retail result |
| --- | --- | --- | --- |
| 2026-10-11 / test 1 | Exact-build identity, D3D9 proxy/menu, level reads, single God-bit action, logs/crash dump attempt | AMD64 cross-build; portable signature/menu tests; extractor fixtures; five unique supplied-binary signature matches; export/import inspection | Pending owner test |

Crash results: none collected yet. No game execution has been performed in this
Linux workspace. The first local run is the next acceptance gate. Update this
ledger with observed behavior, log/crash module RVAs, changes, and rerun results.
