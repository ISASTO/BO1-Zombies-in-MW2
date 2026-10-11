# Reverse-engineering ledger

Updated 2026-10-11. Evidence labels: **static confirmed** means verified in the
uploaded bytes; **runtime pending** means not yet observed in retail MW2. A
signature match is not a runtime test.

## Runtime iteration log

| Iteration | Observation | Confirmed scope / unresolved work |
| --- | --- | --- |
| `0.1.0-menu-test1`, 2026-10-11 | Owner: "the game does not launch"; later supplied three dumps | All fail during native SetRenderState private slot 134 after a 119-entry shadow was installed. Exact host/profile resolution and successful device/input setup confirmed; first frame not reached. |
| `0.1.1-startup-test2` | Owner: "launch worked both times" (unmodified and forwarding control) | Retail forwarding baseline passed. System/old-DLL/control probes all exit zero in both result bundles. Windows collector and installer executed successfully. |
| `0.1.2-menu-test3` | Selected public slots patched in their existing tables; no vptr replacement | Two portable tests pass, including observed private dispatch and competing-edit rollback. AMD64 DLL built; owner menu/mission test pending. |

Test 1 initialized its C++ static runtime, then logging/crash handling, engine
hash/scanning, factory/device shadows, WndProc, and rendering across startup.
Potential failure stages include DLL dependency/runtime loading, System32
forwarding, scan initialization, hook interoperability, and the first draw.
These were initial hypotheses. The returned dumps now identify the device-table
truncation as the startup cause. Loader, hash resolution, and device creation
completed before that fault. Other menu/runtime behavior still needs testing.

The separate startup DLL has only Kernel32 imports and no CRT/TLS/hooks. Its
entry point does minimal bookkeeping; first-export initialization records host,
proxy, System32 module, and factory call completion. The loader probe verifies
the old DLL without its iw4sp-specific paths. Windows event evidence and the
no-DLL/forwarding-control launch comparison determine the next small change.
See [menu-test3.md](menu-test3.md) and
[runtime-test2-evidence.json](runtime-test2-evidence.json) for the confirmed call
chain, redacted input identities, correction, and next test.

### Graphics-runtime address ledger (owner crash environment)

| Finding | Module / RVA | Evidence / use |
| --- | --- | --- |
| Engine SetRenderState call | iw4sp.exe `0x1087f` | Public table byte offset `0x1c8` / slot 57, state `0xce`, value 1; static bytes and dump stack agree |
| Native SetRenderState | System32 d3d9.dll `0x11fb00` | Matching PE timestamp/image/PDB; x64 RCX=device, EDX=state, R8D=value |
| Private dispatch load | System32 d3d9.dll `0x11fbf6` | Reads native vtable byte offset `0x430` / slot 134; beyond the substituted 119 public entries |
| Saved return address at all three faults | System32 d3d9.dll `0x11fc03` | Execute AV in heap data, not a module instruction; confirms the dispatch location |

System DLL file version `6.2.19041.7725`, SHA256
`374fbaf5320373d4bc3370ba113ad4d9c0597ea34ad5bc1b78c2686978b0cada`.
These system RVAs are forensic observations, not addresses used by the mod.
The correction uses documented COM slots and preserves all unknown native tail
entries without resolving or calling a private method itself. This is separate
from IW4/T5 actor/player data layouts.

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

The regular public D3D9 factory interface describes 17 entries, CreateDevice 16;
the device interface describes 119, Reset 16, Present 17, EndScene 42, Release 2.
Those public counts do **not** bound a native implementation's full vtable.
Test 3 patches only those selected public entries in place and preserves each
native object vptr and every private entry. Originals are recorded per shared
table, including fallback for untracked instances. Failed compare-and-swap
transactions conditionally roll back without overwriting competing changes.
Records have process lifetime; hot-unload remains unsupported.

The proxy performs expensive initialization when its exported factory/performance
function is called, outside our DllMain. DllMain only requests notification
suppression. System D3D9 is loaded from its absolute Windows system path.
The exact executable statically imports only Direct3DCreate9 from D3D9. Ten
documented/common exports are supplied, including unhooked Ex forwarding;
arbitrary undocumented D3D9 exports and other wrappers are outside current scope.

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
| 2026-10-11 / test 1 | Exact-build identity, D3D9 proxy/menu, level reads, single God-bit action, logs/crash dump attempt | AMD64 cross-build; portable signature/menu tests; extractor fixtures; five live signature matches | Failed before first frame: three execute AVs from truncated native device table |
| 2026-10-11 / test 2 | Forwarding-only startup control and loader/event collector | Import/export/TLS checks; PowerShell parsing/filesystem checks; owner probes all exit zero | Owner confirms normal launch with DLL removed and with forwarding control |
| 2026-10-11 / test 3 | Keep native vptr/table; patch selected public slots with conditional rollback | AMD64 cross-build; two native tests including private slot 134 and shared-table regression; PowerShell menu-upgrade/collector checks | Pending owner F4/mission test |

The three owner dumps and two results ZIPs have been analyzed and their redacted
findings recorded above. No retail game is executed in this Linux workspace.
The next acceptance gate is the owner's actual menu test 3, followed by mission,
level/health, and God observations. Continue recording failures and successes.
