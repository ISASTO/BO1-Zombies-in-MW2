# Reverse-engineering ledger

Updated 2026-10-11. Evidence labels: **static confirmed** means verified in the
uploaded bytes; **runtime pending** means not yet observed in retail MW2. A
signature match is not a runtime test.

## Runtime iteration log

| Iteration | Observation | Confirmed scope / unresolved work |
| --- | --- | --- |
| `0.1.0-menu-test1`, 2026-10-11 | Owner: "the game does not launch"; later supplied three dumps | All fail during native SetRenderState private slot 134 after a 119-entry shadow was installed. Exact host/profile resolution and successful device/input setup confirmed; first frame not reached. |
| `0.1.1-startup-test2` | Owner: "launch worked both times" (unmodified and forwarding control) | Retail forwarding baseline passed. System/old-DLL/control probes all exit zero in both result bundles. Windows collector and installer executed successfully. |
| `0.1.2-menu-test3` | Owner: "It works!!! this is so cool!!!"; returned matching diagnostic bundle | EndScene/font/3440x1440 draw, 82 key actions, all six categories, close/back, and estate-to-no-server transition observed. No new crash evidence. Player readiness failed and three God actions were blocked. |
| `0.1.3-player-test4` | Owner confirms correct health, working God ON/OFF and retained God state through checkpoint reloads | Matching log shows complete slot-zero reads, five toggles preserving other flags, health 2/70/100, and God blocked back at main menu. No new crash evidence. Phase 1 core accepted in estate on the exact build. |
| `0.1.4-round-test5` | Owner: "Seems to be working fine." | Matching front-end log covers all 100 rounds, 344 inputs, 290 preview records and 3440x1440 rendering. No active mission or new crash evidence in this run. |
| `0.1.5-fov-test6` | Owner confirms FOV behavior; reports protection surviving a return/relaunch while God displays OFF | Matching logs show saved FOV 100, selection/save/reset, entity God OFF during the reported period, and health 2 after an ON/OFF cycle. Separate client guards were not logged; precise God mismatch cause is unconfirmed. Normal CSV values match the unchanged preview calculations. |
| `0.1.6-player-test7` | God session reconciliation/guard diagnostics; every current PLAYER row; smaller eight-row menu | Owner reports normal behavior. Matching receipt/logs cover native fields, God OFF/death/recovery and FOV; no individual toggle actions or new crash evidence in this log. |
| `0.1.7-world-weapons-test8` | Owner reports invisible/empty Give All guns and LOCKED Rapid Fire; matching estate log | Runtime correctly rejects three overlapping catalog matches; offline consuming regex missed overlap. Movement/world apply/restore observed. No new crash evidence; see runtime-test8-evidence.json. |
| `0.1.8-weapons-test9` | Owner reports working other controls, but checkpoint bank collapse/failed OFF and G18/dual FPS defects | Matching log confirms contaminated recapture, G18 veto and Rapid Fire; no new dump. |
| `0.1.9-weapons-test10` | Preserve baseline and introduced IDs; held-gun generic dual compatibility; cached metadata/performance diagnostics | Eight C++ suites, 12 Python tests, 57 signatures, clean AMD64 build and actual-test9 upgrade pass; retail acceptance pending. |
| `0.1.10-zombies-controller-test11` | Owner authorizes further work without current game access; combined weapon repairs plus BO1 controller rehearsal | Source-derived normal-solo lifecycle, simulated callbacks, source-only melee admission port and dependency/asset audit. New native/retail execution remains pending; no actor/damage/asset mutation APIs added. |

### Test11 controller and AI portability

The exact-profile player commandTime field at client+0, already confirmed for
checkpoint ownership, supplies a read-only translated rehearsal clock. Existing
Ready/client/map guards scope the model. A detected unavailable level/read,
death, map/client change or clock rewind invalidates the simulated session and
its tickets. Pause/Slow Motion and checkpoint detection require retail evidence;
this is not a newly confirmed BO1 GetTime ABI. No new native signatures were
introduced. Actor/VM calls from the rendering hook remain prohibited by design.

The actual patched scripts confirm 8.25s first intro, 2.5s later intro, 10s
inter-round sequence, one-repeat spawn selection, success-only budget spending,
enemy-count filters, last-zombie speed-up and guarded failsafe requeue. The
common base supplies previously missing animation utility sources. A lexical
60-script dependency graph still has 64 unresolved references in this input
set. Scoped ordinary-zombie/player melee admission is implemented in portable
source; no native attack or damage adapter is connected. Pain/death remain
researched dependencies, not generic health-zero/deletion substitutes.
See [bo1-controller-evidence.json](bo1-controller-evidence.json),
[bo1-portability.md](bo1-portability.md) and
[zombies-controller-test11.md](zombies-controller-test11.md).

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
chain, redacted input identities, correction, and next test. The successful menu
run is recorded in [runtime-test3-evidence.json](runtime-test3-evidence.json).

### Test 3 player-gate failure

The log resolves all five patterns/name anchors, reads sv_running/mapname, and
gets entity pointer main-module RVA `0xd9e070` during the estate mission. It still
returns Transition and blocks three God selections. The old `+0x00`, 32-bit
identity check was wrong: native entity allocation stores the array index as a
WORD at `+0x7e`, and native script handling reads it as a zero-extended WORD.
Native code uses `+0x00` as an entity-type byte. This field mistake is statically
confirmed; the actual rejected number was not included in the old log.

Those logged default zeros did not prove zero health; the guard could return
before reading health/flags. Test 4 reads the correct WORD and logs number and
read status. A failed read displays UNKNOWN health, distinct from a readable
dead-player value. Engine function calls, God-bit semantics, and the working
graphics/input hook implementation are unchanged. See [player-test4.md](player-test4.md).

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
| `G_Spawn` candidate / entity index assignment | `0x1a0953` | `0x1401a0953` | Native allocator derives array index then stores DX as a WORD at entity `+0x7e` |
| Native entity-number read | `0x19afb4` | `0x14019afb4` | Zero-extends WORD `[entity+0x7e]` into ECX before script/entity handling |
| `sv_running` dvar pointer slot | `0x13260c0` | `0x1413260c0` | Named registration, bool registration, RIP store |
| `mapname` dvar pointer slot | `0x19ada88` | `0x1419ada88` | Named string registration and RIP store |
| `Dvar_GetBool` candidate | `0x26bf90` | `0x14026bf90` | Type check at `+0xc`, bool current read at `+0x10` |
| `Dvar_GetString` candidate | `0x26c050` | `0x14026c050` | Current string pointer `+0x10`, enum handling at type 6 |
| `cg_fov` registration sequence | `0xa7177` | `0x1400a7177` | RIP name load, min/default/max floats, call and pointer store |
| `Dvar_RegisterFloat` assigned wrapper | `0x26cb90` | `0x14026cb90` | Sets type DL=1 at `0x26cbaa`, calls generic registration `0x26cf60` |
| `cg_fov` dvar pointer slot | `0x51e1a8` | `0x14051e1a8` | Registration and camera signatures resolve the same RIP-relative slot |
| Camera base-FOV read | `0xbc26d` | `0x1400bc26d` | MOVSS from `[dvar+0x10]`, then separate ADS interpolation/scale logic; enclosing signature starts `0xbc25d` |
| Alternate-view base-FOV read | `0x6a641` | `0x14006a641` | Independent same-slot load at `0x6a62a`, scalar read and projection/tangent calculations |

All rows above are static confirmed. The test 3 log additionally confirms live
signature resolution and server/map/entity-pointer reads. Test4 additionally
confirms slot-zero identity, validated health reads and God flag writes; the
owner confirms damage behavior and checkpoint persistence in the tested run.
Native function names are assigned from semantics, not recovered symbols. None
of the listed engine functions are called by our DLL. RCX/RDX/ECX observations
are Microsoft x64 ABI evidence; they do not authorize using an old x86 typedef.

The native God command toggles bit zero and writes player entity flags. Tests
4-6 used an explicit aligned XOR of that bit. Test7 replaces that operation
with an idempotent desired session state: ON sets bit zero and clears demigod;
OFF clears both bits. Compare/exchange preserves every unrelated flag. The
request starts OFF on a new process and is reapplied after checkpoint restores.
Independent protection is now recorded instead of inferring vulnerability from
that entity bit alone.

## Partial layouts used

| Structure / field | Offset or value | Evidence / limit |
| --- | --- | --- |
| MW2 entity stride | `0x2a0` | Client command indexing and God loop |
| Entity type | `+0x00`, 8-bit | Native allocation/type branches; not used as an identity guard. Former 32-bit entity-number interpretation was wrong. |
| Entity number | `+0x7e`, unsigned 16-bit | Native array-index WORD store and independent zero-extended WORD read; test4 runtime reads number 0 and passes gate |
| Entity flags | `+0x150`, 32-bit | Native XOR/store and damage test |
| God bit | `0x1` | Native command and damage branch |
| Entity health | `+0x164`, signed 32-bit | Native cheat-alive guard |
| Client pointer | `+0x108`, 64-bit | Independent native noclip/client-think/damage/script-guard paths; test7 reads only corroborated fields and validates connected state, movement type and health |
| Non-client damage pointer | `+0x110`, 64-bit | Native damage path uses this pointer; former client assignment was incorrect. Exact pointed-to type unresolved and unused. |
| Dvar name pointer | `+0x0`, 64-bit | Registration/getter conventions; validated against exact string at runtime |
| Dvar type | `+0xc`, 8-bit | Getter type comparisons |
| Dvar current | `+0x10` | Bool byte, four-byte float or string pointer, depending on type |
| Dvar bool type | `0` | Getter treats type zero as bool |
| Dvar float type | `1` | Native float registration wrapper and MOVSS camera read |
| Dvar integer type | `5` | Native registration wrapper sets DL=5; g_speed consumer reads a DWORD |
| Dvar string type | `7` | String registration/getter family; runtime name/type validation required |

Only individual fields are read; no complete `gentity_s`, `gclient_s`,
`playerState_s`, or `actor_s` is assumed. Test7 uses the partial client/reserve
fields below; complete inventory, weapon-definition and actor layouts remain
unresolved.
Player identification by slot zero is supported by the dispatcher; mission and
checkpoint behavior are still local tests, not a claim of universal reliability.
Test 3 established the slot-zero pointer; test4 confirmed identity/health reads.

### FOV registration, ABI and persistence

Static confirmed for this exact hash: name string cg_fov at RVA 0x327bb4,
minimum 1 at 0x385a54, maximum 160 at 0x31e6a4, default 65 at 0x32ae10.
Registration uses Microsoft x64 RCX=name, XMM1=default, XMM2=minimum,
XMM3=maximum, stack flags 0x44 at rsp+0x20 and description at rsp+0x28.
These are semantic assignments, not recovered symbols or a called DLL API.
The flags' meaning and complete dvar layout are not assumed.

Test6 uses optional registration and camera patterns separate from the core
gate. Both must identify the same in-image pointer slot and exact constants.
Live reads validate the dvar name/type and finite domain 1-160. Writes additionally
require Ready, a writable aligned field and a valid selector value 60-120 in
steps of 5. An atomic compare-and-exchange changes only the four-byte current
float; a concurrent change defers a retry. No native dvar call, domain/reset/
latched/flag write, cg_fovScale change or weapon-zoom patch occurs.

The render callback checks an explicitly selected/saved desired FOV at most
every 250ms while Ready, reapplying only if current differs. No selection means
no override. Restore default queues 65 once and releases the override after
success. `bo1mw2/settings.ini` contains version=1 and fov=<integer>, with strict
128-byte bounded parsing. Save flushes an own temp file then replaces the own
settings file; only successful disk operations change the saved indication.
Uninstall preserves it; clear/reset deletes only that own file. Diagnostics
include that file when present. The owner confirms test6 FOV behavior; matching
logs show load/selection/save/reset. Broader alternate/cutscene/second-map
coverage remains; a static read is not proof of every final camera path.

### Test7 native PLAYER evidence and ABI

These rows were static findings at test7 publication; the owner now reports
normal behavior. Matching test7 diagnostics confirm valid native reads and
God OFF/death/recovery, without individual toggle actions in that run.
Function names are assigned from command branches,
method tables and behavior. None is invoked from the render callback.

| Finding / assigned function | RVA | Evidence |
| --- | --- | --- |
| G_Damage start | `0x167d30` | Entity take-damage gate, then client and entity protection checks |
| Take-damage gate | `0x167d53` | Byte at entity +0x139; zero bypasses ordinary damage |
| Client damage guard | `0x167e89` | Client +0x10 bit 1 skips damage independently of entity God bit |
| Noclip/UFO damage guard | `0x167e93` | Client +0xad54 mask 3 skips damage |
| EnableInvulnerability method | `0x164f70` | Method-table name at 0x33a4f8 points to this function; OR client +0x10 with 1 |
| DisableInvulnerability method | `0x164fd0` | Method-table name at 0x33a510; AND client +0x10 with ~1 |
| Cmd_Noclip | `0x165d60` | ClientCommand noclip branch; toggles client +0xad54 bit 1 |
| ClientThink readiness/read | `0x15be45` | Entity +0x108 pointer, connected value 2 at client +0xaca4, movement flags +0xad54 |
| Native movement type selection | `0x15beb2` | Noclip bit 1 chooses movement type 2; UFO bit 2 chooses type 3; native store at 0x15bf20 uses client +4 |
| Native client health zero | `0x165c7d` | Kill command writes client +0x14c; corroborates a signed health field |
| Reserve lookup | `0x7f190` | From PS +0x2d8, fifteen entries, stride eight; ID DWORD then count DWORD |
| Reserve set | `0x86b20` | Same table, ID/count stores, empty ID zero handling; used as layout evidence only |
| Clip consumption / BG_UseAmmo candidate | `0x84e80` | Reads player_sustainAmmo, normal guns bypass consumption; otherwise +0x350 table, fifteen twelve-byte ID/two-hand-count entries |
| player_sustainAmmo registration | `0x75414` | Default false, bool wrapper 0x26c930; registration/consumer agree on slot 0x3dd718 |
| g_speed registration | `0x172b8f` | Default integer 190, raw flags 0xc0; slot 0xd9c040; wrapper 0x26cc10 sets type 5 |
| g_speed player consumer | `0x15bf60` | Reads current DWORD +0x10, stores PS speed +0x5c; observed direct consumer is player movement |
| jump_height registration | `0x70833` | Float default 39, domain 0..1000, raw flags 0x84; slot 0x3d9658 |
| Native jump impulse | `0x703b0` | Multiplies gravity by fixed 78, then slowdown adjustment, sqrt and velocity-Z store at 0x70447 |
| Native velocity cap | `0x706a6` | Fixed 39 above jump origin; gravity calculation limits Z velocity |
| Native step/ceiling checks | `0x70738`, `0x70798` | Same fixed 39 reference in jump height checks |
| Shared height / twice-height data | `0x31f8cc`, `0x31f8d0` | Adjacent aligned floats 39/78; four scalar references to height (including registration) and one to impulse; no observed jump-height dvar consumer |

The command noclip routine takes RCX=entity under Microsoft x64 ABI. The
script invulnerability methods take an ECX packed entity reference: upper
16 bits checked as class/type, lower WORD indexed with entity stride 0x2a0.
BG_UseAmmo candidate receives RCX=PS, EDX=weapon selector, R8D=amount, R9D=hand;
this is inferred from the lookup/subtraction path, not an exposed API binding.
The mod never calls these functions or runs game script from a render thread.

| Partial field | Offset / width | Runtime use / limit |
| --- | --- | --- |
| Entity native damage enable | +0x139 / byte | Read only; accepts 0/1, never forced on |
| Client/PS command time | +0 / signed DWORD | Compared in native ClientThink; a rewind invalidates ammo ownership |
| Client/PS movement type | +4 / signed DWORD | Read only, accepted range 0..7; dead types 6/7 gate new actions |
| Client/PS other flags | +0x10 / DWORD | Independent invulnerability bit 1; one-time masked OFF/lifetime/reset cleanup |
| Client/PS health | +0x14c / signed DWORD | Additional living-player gate; not a health freeze |
| Client connection state | +0xaca4 / signed DWORD | Requires native connected value 2 |
| Client native cheat flags | +0xad54 / DWORD | Noclip bit 1 and UFO bit 2; masked reconciliation preserves other flags |
| Client/PS reserve ammo | +0x2d8 / 15 entries of 8 bytes | ID +0, signed count +4; occupied entries validated; pool IDs are never invented |
| Client/PS clip ammo | +0x350 / 15 entries of 12 bytes | Identified, not directly edited; No Reload uses the native dvar instead |

A rejected hypothesis treated PS +0x34 as a client number. Native kinematic
operations contradict that interpretation; it is not an identity guard. Entity
WORD slot zero remains the identity check. No BO1/x86 client layout was reused.

Session state and God fix: every 16ms while ready, reconcile entity mask 3 to
1 for ON or 0 for OFF. This prevents a checkpoint restoring a stale native
flag after the UI request became OFF. Native noclip/UFO mask 3 is reconciled
separately. At a new ready lifetime, OFF selection or Reset, the verified
independent invulnerability bit is cleared once if native take-damage is on
and normal movement type is 0/1. Later scripted protection is reported as
GUARDED; no continuous script-guard or take-damage override runs. Cleanup may
remove protection at that spawn/checkpoint, so mission sequencing needs tests.
A full restart resets requests OFF; checkpoint reloads retain session choices.
Only the explicit FOV preference is serialized.

The precise test6 mismatch cause remains **unconfirmed**. Its entity God bit
was zero during the sampled protected period. After a user ON/OFF cycle,
health 2 was logged. The log did not capture client guards, attempted damage,
or the exact return/restart boundary. Test7 records native guards and lifetime
changes. The new cleanup addresses a verified independent protection path;
it is not evidence that this path caused the earlier run.

Ammo writes use an aligned eight-byte compare/exchange of the ID/count pair,
keeping the ID identical. Only occupied, validated existing entries are
replenished. OFF restores a captured count only if the same pool still holds
the exact injected 999. Engine changes are preserved. Server gaps, map/client
changes and command-time rewinds discard ownership. Test8 corrects the former
frag/flash interpretation: equal-name branches bypass consumption to return,
so normal frag/flash share the sustain control. Other native special-weapon
branches remain unchanged. Its bool
current byte is edited with a byte atomic CAS; speed uses a DWORD CAS and
restores its captured pre-override value. Named/type checks run at each read.

Jump compatibility differs from the generic dvar approach. The verified native
jump code consumes pooled literals, so changing only jump_height would not
change these paths. Test7 edits only the adjacent data pair to 117/234 while
ready and requested; OFF or loss of readiness restores 39/78. A temporary data
page permission change is restored. Raise the cap before the impulse, lower
the impulse before the cap; conditional rollback handles a conflicting edit.
No instruction or disk executable patch occurs. Unexpected references,
non-default initial data or inconsistent values disable the jump feature.
This exact-build compatibility path still needs retail jump/landing evidence.

Menu: cap scale at 1.1, 500x410 logical panel, row text factor 0.8 and 26-pixel
row spacing, eight visible rows. At the owner's 3440x1440 it is 550x451 instead
of 912x810. Hook/vtable/font-atlas generation/render-state preservation remain
unchanged; native rendering and smaller text readability await the owner.

### Test8 WEAPONS / WORLD findings

Static evidence in the same full executable hash. The original offline claim
of 41 unique anchors was incorrect for the catalog initializer: consuming regex
missed overlapping matches. Current `docs/profile-checks.json` records 57
overlap-aware unique anchors and independent field/slot/reference checks.
These are exact-build assigned names and partial layouts, not a T5/IW4 cast.
The test8 log confirms the catalog rejection and several option apply/restore
paths; the owner reports Give All model/ammo faults. No native function is invoked.

| Finding / assigned name | RVA / slot RVA | Evidence / scope |
| --- | --- | --- |
| g_giveAll registration / bool current | `0x173df4` / `0xd9e068` | Default false, flags 0x2084; named native registration and inventory consumer agree |
| Loaded weapon access consumer | `0x8389a` | Bool bypasses owned-slot search; PS current weapon +0x2b4; command at 0x1657d0 enables this same bool for all/weapons |
| bg_forceExplosiveBullets | `0x750bb` / `0x3dd690` | Bool; consumer 0x86f30 uses native explosive-weapon flag OR global option; includes AI bullet weapons |
| g_ai registration / actor gate | `0x172bf7`, `0x12a699` / `0xd9c0b8` | Default true, flags 0x84; false schedules next think at level time +50 and returns before actor body; both teams |
| timescale registration / clock read | `0x1fc917`, `0x1fc269` / `0x13260b0` | Float current +0x10; distinguish com_timescale slot 0x13260b8 stored earlier in the same registration block; clock also consumes other factors |
| g_gravity registration / trajectory read | `0x75fbe`, `0x736f7` / `0x3dd5e8` | Float, native default 800 at 0x324340; changing this alone misses player gravity |
| Player gravity source / constant | `0x15bf24` / `0x338b0c` | Sole scalar consumer of float 800.5; calls positive floor helper 0x2f20a0, converts result and writes PS +0x54 at 0x15bf5c |
| Weapon catalog initialize | `0x85220` prologue; first block `0x85224` | Two 0x640-byte pointer tables: CompleteDef 0x3edce0; Def 0x3ee320; capacity 200, stride eight; old block-only signature also matched overlapping blocks 0x85238/0x8524c |
| Native table getters | `0x85840`, `0x85850` | ECX index, RAX pointer, Microsoft x64 ABI; no mod call; independent initializer/binding agree |
| Last loaded weapon index | `0x3edae4` | DWORD loaded in 0x8589d lookup; it is not adjacent cache index 0x3edae0 |
| CompleteDef to Def binding | `0x852a8` | CompleteDef +8 pointer copied into matching Def table entry |
| weaponfiretime script method | `0x18b4b0`, field read `0x18b4ed` | Name 0x3426c0 and method pointer 0x3c1ef8; native getter, int CompleteDef +0x3c, milliseconds multiplied by 0.001 |
| Native fire delay consumer | `0x83b68` | CompleteDef +0x3c added to PS weaponTime +0x1ec; offhand path also consumes field; no reload/equip edits |
| Frag/flash sustain branches | `0x84edc` | fraggrenade and flash_grenade equal-name branches both jump to 0x84fff epilogue, bypassing subtraction; corrects earlier exception interpretation |

| Partial member | Width | Guard / use |
| --- | --- | --- |
| Client/PS current weapon +0x2b4 | DWORD | Index 1..last-loaded, last-loaded <200; index zero invalid for actions |
| Client/PS gravity +0x54 | signed DWORD | Read only; source result corroborates literal compatibility |
| CompleteDef name +0 | pointer | Readable, nonempty bounded ASCII identifier |
| CompleteDef definition +8 | pointer | Must equal the Def table entry for this index |
| CompleteDef fireTime +0x3c | signed DWORD | Read 1..10000; Rapid Fire only bullet gun type 0 and timing 20..4000 |
| WeaponDef weapon type +0x54 | signed DWORD | Native ammo consumer corroborates; accepted 0..6, never a whole structure assumption |

Rapid Fire halves the current gun's captured delay with a 20ms floor. It edits
one aligned data DWORD with CAS. Restoration requires index, CompleteDef/Def
pointers, type, name and exact injected timing to agree. Restore the previous
asset on weapon switch, OFF/reset, death, lifetime rewind or server gap; skip
unloaded/reused assets. A shared asset can affect AI using that gun. A native
20ms gun needs no edit and shows AT MIN. This does not turn semi-auto into auto.

Other new native controls use named/type-checked current values, atomics and
captured baselines. Strength changes retain the original baseline while the
current value is ours; an intervening engine value becomes a new baseline.
OFF/loss of readiness restores only a matching last write. Selected speed
190..950 uses increments of 95; selected jump height 39..312 uses increments
of 39, with its twice-height partner. Session requests and selections reset
on process launch; saved FOV is the only serialized mod preference.

Gravity compatibility changes only the sole verified player literal to
selected gravity +0.5, alongside native projectile g_gravity. The positive
floor helper makes PS gravity equal the selected 100..1600 value. OFF restores
800.5 plus the captured native dvar baseline. Jump/gravity edits temporarily
change data-page permissions and restore them; instructions and disk bytes
are unchanged. Unexpected references/defaults/values fail closed. Zero time
and gravity are unavailable. T5 movement/actor structures are not involved.

Give All means access to assets already loaded by this campaign map; no new
weapon assets, arbitrary inventory allocation or cross-map import occurs.
Infinite Grenades is an explicitly linked UI entry for existing No Reload.
Only normal frag/flash bypasses are corroborated; it is not an independent
grenade-pool implementation. Explosive bullets and actor thinking are global
native options. Scripts, animation and physics may continue when AI is frozen.

Unresolved: hostile team filtering and a safe engine-thread native damage/death
call for Kill All Enemies. Actor iteration, size guesses and health-zero writes
are not enabled. BO1 actor state, targeting, animations and death remain future
compatibility work in the separate A/B/C research document.

### Test9 native inventory, ammo and dual-wield evidence

All rows are **static confirmed** in the exact supplied AMD64 hash. Gameplay is
**runtime pending**. Names are assigned from native semantics; no engine method
below is invoked by the DLL, and no whole asset/player structure is cast.

| Finding / assigned name | RVA | ABI / evidence |
| --- | --- | --- |
| Owned-slot allocator/grant | `0x86160`, writes `0x861d3` | Native 15-slot scan; metadata DWORD/byte cleared before publishing owned ID |
| Owned metadata lookup | `0x1a0ce0`, stride calculation `0x1a0d2b` | Microsoft x64 RCX=PS, EDX=index; returns PS +0x25c + slot*5; g_giveAll bypass uses slot14 |
| HasWeapon candidate | `0x74230` | Native grant/alternate-chain membership check; reference only |
| G_GivePlayerWeapon candidate | `0x1a1d00` | Microsoft x64 RCX=PS, EDX=index, R8B=model selector, R9D=akimbo; filters weapon class/inventory/model, reads noDual and recursively follows alternates |
| Script giveweapon / ammo init | `0x15fba0`, calls `0x15fcae` / `0x15fcc3` | Native script path calls grant then `0x163da0`; scheduling/allocation/side effects are not exposed by this mod |
| Ammo-pool index helper | `0x84280` | Calls Def getter `0x85850`; reads +0x348 |
| Clip-pool index read | `0x16d2c0`, read `0x16d2cb` | Native AddAmmo path calls Def getter; reads +0x358 |
| Magazine-size helper | `0x86290` | Calls CompleteDef getter `0x85840`; reads +0x34 |
| Clip AddAmmo / SetAmmo candidates | `0x84210` / `0x84a00` | Same 15-entry, 12-byte pool scan; add/store distinguish unique anchors. Native full-table fallback can replace slot zero; our compatibility skips full tables |
| Primary/model gate | `0x1a1d41` | Reads Def class/inventory and nonnull player gun model; our subset requires primary bullet guns, model0 and no alternate chain |
| Alternate-chain read | `0x1a1e34` | Calls CompleteDef getter then reads +0x58; new grants with nonzero alternate index are excluded |
| bg_forceDualWield registration | `0x746ac`, slot `0x3d99a8` | Named bool, default false, flags 0x0c; registration and consumer resolve the same slot |
| Native dual hand selection | `0x82a67` | Reads bool current +0x10, tests Def noDual, writes PS last-hand and metadata dual byte |

| Partial field | Width / layout | Mod use / limit |
| --- | --- | --- |
| PS owned weapon IDs +0x220 | 15 DWORDs, zero empty | Preserve originals; publish initialized added IDs last |
| PS owned metadata +0x25c | 15 entries, stride5 | +1 dual bool; +2 model selector. Other bytes preserved for originals, zeroed for new grants as native allocator does |
| PS last weapon hand +0x2d4 | DWORD 0/1 | Enable compatible held gun's second hand; OFF derives natural hand from restored metadata |
| PS reserve +0x2d8 | 15 entries, stride8 | ID/count compared together in aligned QWORD CAS |
| PS clips +0x350 | 15 entries, stride12 | DWORD ID, right count, left count. Publish new ID after counts; retain foreign pool identities |
| WeaponDef gun models +8 | pointer to indexed XModel pointer array | Verify model0 and readable model name; no opaque asset edits |
| WeaponDef class +0x58 / inventory +0x60 | DWORDs | New grants require class0..7 and inventory0 |
| WeaponDef ammo ID +0x348 / clip ID +0x358 | DWORDs | Require nonzero IDs below200; never substitute weapon index for pool ID |
| WeaponDef noDualWield +0x873 | byte0/1 | Do not force incompatible gun metadata or manufacture akimbo animation assets |
| CompleteDef clipSize +0x34 / alternate +0x58 | DWORDs | Magazine1..1000; exclude unsupported new alternate chains |

The runtime uses 51 centralized optional/core patterns and resolved relative
targets; the offline verifier independently decodes these layouts and checks
getter calls, named dvar slots, clip/metadata strides and capacity. Test8's old
two-memset signature matched 0x85224/0x85238/0x8524c. Adding the real initializer
prologue makes a single 0x85220 match. All offline byte scans now use lookahead
so overlapping starts count exactly like runtime FindPattern. C++ and Python
fixtures reproduce the former blind spot.

Give All is a data compatibility mode: original equipment stays in place;
eligible loaded primary guns occupy free slots in banks. Right clips receive
native clipSize and reserves min(999,clipSize*5). Original natural dual guns
also receive left ammo. Native global g_giveAll is held OFF to avoid slot14
bypass and nonexistent inventory/ammo semantics. Full ammo tables skip a gun.
No engine allocation, extra fastfile, model import or native grant call occurs.

Inventory transactions compare before values, stage metadata/ammo and publish
owned IDs last; failure attempts conditional reverse rollback. OFF requires the
same player/map/time and owned IDs, restoring the original snapshot and held gun.
Foreign ammo identities survive. Same-process checkpoints revalidate live IDs
and CompleteDef/Def/name identity before rebasing an undo snapshot. Changed
lifetime/inventory discards it; full-process restart does not retain snapshots.
An actual engine-thread inventory adapter and save/checkpoint recovery remain
future work, contingent on the owner's test9 evidence.

Dual Wield captures the native bool, individual edited dual flags, left counts,
and last-hand ownership. Bank capture/restore first removes current dual edits,
waits for pending restores, then reapplies requested dual against the new
inventory. Give All's neutral original snapshot is never rewritten with bank
ammo; an unchanged full left magazine creates no dual ammo ownership record.
This preserves either OFF order and naturally dual original guns. Native eligibility is necessary but
does not prove every view model/animation renders correctly; require firing,
reload, switch/back, OFF and both-order tests. Rapid Fire refreshes held state
after bank changes before selecting the timing asset to modify.

### Test10 checkpoint provenance, animation compatibility and performance

Test9's returned estate run resolves its profiles and exercises bank cycling
and Rapid Fire. Checkpoint recovery then discards the baseline and captures a
full prior bank as original: four banks become one with zero grants, and OFF
restores those introduced guns. G18/`glock` has native noDualWield=1. See
[runtime-test9-evidence.json](runtime-test9-evidence.json) for redacted evidence.

Test10 keeps the original snapshot and introduced weapon/reserve/clip IDs for
the campaign scope, across bank changes and OFF. Earlier-bank checkpoints and
slot reorder are identified by IDs. If only original/introduced weapons remain,
restore the original loadout; if foreign pickups exist, remove introduced IDs
and preserve foreign weapons/pools. Timing-free identity reads allow equipment
and launcher entries to remain preserved without treating them as bullet guns.
Unreadable non-grantable equipment identity does not veto all recovery; readable
original and introduced asset identities remain pinned. Temporary invalid reads
defer reconciliation instead of replacing the baseline. PLAYER/WORLD gates
continue independently when inventory reconciliation is pending.

| Native finding / assigned name | RVA / layout | Evidence and use |
| --- | --- | --- |
| Right-handed animation support | `0x86740` | Def +0x18, array index1, nonempty string test; reference-only function |
| Handed/default animation selector | `0x86e80`, anchor `0x86eac` | PS lastHand +0x2d4 selects Def +0x18/+0x20; fallback CompleteDef +0x20; eight-byte name-pointer indexing |
| Native per-weapon animation definitions | `0x43f470` | Three QWORD slots per weapon: normal/right/left; independent cache read and reset resolve same address |
| Cache grouping calculation | `0xc4b38` | weapon*2 + weapon, then *8 = stride24 |
| Cached definition read / playback creation | `0xc66f5`, `0xc675e` | Cached definition passed to `0x1fe600`; returned playback object stored independently; nearby `0x1fe610` handles playback teardown |
| Native cache reset | `0xa6392` and independent `0xa93f2` | Clears 0x12c0 bytes =200*3*8, after per-hand client playback teardown |
| Definition creation / name loop | `0xc2c70`, loop anchor `0xc2ccf` | Processes 37 names; missing optional names use idle; canonical normal names must be readable |
| Animation allocation callback | `0xa9730` -> `0x2689a0` | Callback tail-calls alignment4 arena allocator; native bump/commit counters corroborate arena allocation. No mod allocation/free or function call |
| Hand-definition registration veto | `0xc4c84` | noDual +0x873 controls paired registration, independently of the native PM veto |

These are **static confirmed** in the supplied hash. Assigned names describe
observed semantics; no complete XAnim/IW4/T5 structure is assumed. Six new
central patterns bring the profile to57; the verifier decodes name fields,
three-slot stride, capacity, selector and 37-name count independently.

The compatibility adapter touches only the held primary gun. If authored paired
names/definitions are available, keep them. Otherwise require validated normal
names and a registered normal definition, alias the normal name table and
arena-owned definition into the two hand slots, then clear the native veto and
publish dual state. Native code creates independent playback instances from
the shared definition. It is a generic animation fallback, not imported/authentic
akimbo assets. An absent/unregistered prerequisite defers the operation.

OFF/switch/readiness loss conditionally restores the veto, name/cache pointers,
metadata flags, left ammo and natural hand state. Scalar ownership preserves
competing changes; identity mismatch abandons stale asset writes. Cache entries
are not allocated or freed by the mod, and the native reset clears the entire
definition cache. Appearance, firing, reload and lifetime behavior of the loan
remain **runtime pending**. Extra view models have an actual rendering cost.

Immutable candidate metadata is cached per catalog/lifetime. The recurring dual
path reads only the held gun and current cache group; repeated all-slot model
reads and extra full snapshots are removed. WEAPON_PERF records cumulative
update count, average/peak microseconds, reads/update, cache hits/misses and
compatibility/baseline state. Test9 heartbeat differences support the owner's
FPS observation, but scene/weapon changes confound attribution; final FPS must
be measured at a fixed camera with other weapon effects OFF.

A fresh process cannot reconstruct the original inventory of an already modded
external save. The first test10 run therefore restarts the mission once. A full
initial inventory without a known baseline is rejected as RESTART rather than
silently making a zero-grant one-bank mode. History survives subsequent
checkpoints in this process; it is not persisted on disk. Saved FOV remains the
only saved preference. After owner acceptance, prioritize the BO1 normal
round/spawn controller and verified IW4 actor adapter.

## Resolution and level gate

The five core, two optional FOV and sixteen optional player patterns are
centralized in `include/core.hpp`, with the static check
report in `profile-checks.json`. Each final pattern has exactly one match in the
uploaded `.text`. Registration name anchors and RIP-relative stores resolve
slots. Addresses are not copied from another client, platform, or build.
`verify_profile.py` also uniquely matches the native number store/read and
compares decoded displacements against `include/player_fields.hpp`; these
additional static evidence patterns do not install hooks or call engine code.

The supported hash must match before resolution. Unknown hash, zero/multiple
matches, wrong anchors, malformed PE data, unreadable memory, wrong dvar names or
types, or out-of-image entity pointers disable game-data operations.

Level state uses `sv_running`, nonempty `mapname`, slot-zero entity, and health.
It distinguishes no server, loading/unresolved fields, a dead player, and a
live player. God writes require the live-player state and writable aligned flags.
No renderer callback alone is treated as proof that a level is active.
No game-state flag is modified to make the level gate pass.
Test 4 corrects only the identity member/width and exposes each failed field-read
stage in the log. The old Transition report could reflect the wrong guard rather
than actual loading. Test4 now supplies successful health/God observations and
confirms writes are blocked after the estate-to-main-menu transition.

Remaining level-gate questions: initialization/unload ordering; whether any
front-end map can leave a running server and a live slot-zero entity; effects of
cutscene/player-entity swaps; and invalid/dead states during checkpoint reloads.
Test7 adds bounded recurring player-state reconciliation with additional
exact-build gates; broad mission coverage is still pending. AI operations need
separate scheduling/actor evidence. Test5's
preview uses only local mathematical constants and does not require a live actor.

## T5 round-rule numeric evidence: exact uploaded BlackOps.exe

These are private-input research RVAs, not functions called by the MW2 DLL.
The verification tool checks the complete T5 hash and unique instruction
patterns, and decodes builtin-table name/function pointers and relative calls.

| Assigned function / finding | RVA | Evidence / calling convention |
| --- | --- | --- |
| `Scr_EvalDivide` candidate | `0x1bde60` | x86 stack arguments `(instance, value1, value2)`; plain RET, caller cleanup, consistent with cdecl; inferred name confirmed by matching operator semantics and divide-by-zero references |
| Integer divide branch | `0x1bde9b` | Type 6 dispatch; sets result type 5, CVTSI2SS operands, DIVSS, MOVSS. Integer operands therefore yield binary32 float |
| `int` builtin, VM selector 0 | `0x3f8c50` | Table at `0x7767bc` names `int`; float case gets float then calls truncation helper at `0x3f8caa`. No-argument builtin wrapper; instance selector supplied internally |
| `int` builtin, VM selector 1 | `0x386ca0` | Table at `0x771f9c` names `int`; corresponding float conversion call `0x386cf3` |
| Float-to-int helper candidate | `0x566a80` | x87 ST(0) input; SSE path at `0x566a92` stores qword then CVTTSD2SI to EAX, truncating toward zero. Legacy fallback also present; no runtime call made |
| Partial T5 VM value | union `+0`, type `+4`, stride 8 | Native float getter at `0x73380` indexes stack by 8 and distinguishes tags 5/6; research only, never cast into IW4 |

Normal GSC uses column 2 for main round tuning, whereas the between-round value
omits a column and uses utility default 1. The initial five zone streams lacked
the table name. The later common_zombie.ff resolves its normal column; selected
values match the preview. Test5 retains its historical fallback label, whereas
test6 reports local CSV plus GSC fallback. A full final-map override audit remains.

OpenAssetTools' pinned T5 schema describes StringTable name, row/column counts,
cell strings/hashes and a cell-index array. For T5 x86 the serialized structure
uses 32-bit pointers; cells contain string pointer plus hash. Parse its zone
pointer/stream fixups before using this as an asset importer. A string-name scan
does not implement that parser, and this format must not be cast into x64 IW4.

### Scoped StringTable recovery from common_zombie.ff

SHA256 f812069ef2ff446b97051e75d6a5ee49687a8085218b8f79dc2d8237e543117e,
51,325,344 bytes; inflated stream 79,661,349 bytes. Target record header at raw
0x2a5b17f: five 32-bit fields (inline name, columns=5, rows=106, inline values,
inline index array). Name starts 0x2a5b193 and is 18 bytes including NUL. Cells
start 0x2a5b1a5: 530 entries, each a 32-bit XString pointer and 32-bit hash.
The 118 inline strings span 0x2a5c235 through 0x2a5c9fe. Hashes use lowercase
ASCII DJB2 with seed 5381. The following signed-16-bit index entries form a
permutation of 0-529; sortedness is not established.

Packed references subtract one and split into three high block bits / 29 low
offset bits. A virtual block 4 offset cannot be read as a raw stream offset.
Our original scoped reader maps unambiguous in-table aliases, then resolves
exact pointer-keyed strings and verifies each used hash. Thirteen structural
tables yield twelve mappings and 754 cached strings; the target delta is 0x52d41
with fourteen distinct alias values. Two earlier source tables have only one
alias anchor each. Ambiguous tables are skipped; conflicting mappings and
unresolved required cells fail. Hash-only guessing would encounter real short
numeric-text collisions, so it is not used. Unneeded weapon-name references
remain unresolved. This is not a complete XAsset pointer/fixup loader.

Normal column 2 (MEDIUM): health start 150, increase 100, multiplier .1,
spawn delay 2.0, max_ai base budget 24, per-player 6, runner interval 10,
movement multiplier 8. The between-round key is absent by all key hashes, so
10s remains the patched-script fallback. Concurrent cap 24 is a script value,
not inferred from CSV max_ai. Exact selected references and provenance are in
`bo1-normal-tuning-evidence.json`; no original table is distributed.

The portable preview is bounded to rounds 1-100 and only normal solo rounds.
The first-round flag remains independent in the calculation API; the UI sets it
only at round 1 in a fresh-start profile. Health truncates on every multiplicative
step, and pacing applies the prior-value branch (round64 undershoots .08,
round65 resets to .08). Dog/quad timing, map hooks, VM wait/thread semantics and
actual spawn capacity accounting remain unimplemented. See
[round test5](round-test5.md) and [numeric evidence](bo1-round-rules-evidence.json).

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
models, animation data and sound tables. Selected normal CSV values now have a
scoped independent reader; final configuration overrides still require auditing.

T5 XModel, XAnimParts, material/technique/shader, skeleton, and sound structures
are not accepted as IW4 structures. Nor does the new x64 engine prove a serialized
IW4 zone uses 64-bit pointer records. Asset conversion is unimplemented.

## Iteration log

| Iteration | Change | Verification | Retail result |
| --- | --- | --- | --- |
| 2026-10-11 / test 1 | Exact-build identity, D3D9 proxy/menu, level reads, single God-bit action, logs/crash dump attempt | AMD64 cross-build; portable signature/menu tests; extractor fixtures; five live signature matches | Failed before first frame: three execute AVs from truncated native device table |
| 2026-10-11 / test 2 | Forwarding-only startup control and loader/event collector | Import/export/TLS checks; PowerShell parsing/filesystem checks; owner probes all exit zero | Owner confirms normal launch with DLL removed and with forwarding control |
| 2026-10-11 / test 3 | Keep native vptr/table; patch selected public slots with conditional rollback | AMD64 cross-build; two native tests including private slot 134 and shared-table regression; PowerShell menu-upgrade/collector checks | Owner reports working menu; render/input/categories/estate-to-no-server observed, no new crash evidence; player guard failed |
| 2026-10-11 / test 4 | Correct WORD entity number; distinguish failed health reads; log read status | Regression reproduced former rejection and passes after fix; three native tests; exact-byte layout verification; AMD64 build | Owner confirms health/God/checkpoint behavior in estate with matching log |
| 2026-10-11 / test 5 | Read-only source-derived normal-solo BO1 round preview | Four native suites, T5 numeric evidence, Windows build and script checks | Owner confirms working; front-end run covers rounds 1-100, no new crash evidence |
| 2026-10-11 / test 6 | FOV selector/save/reset and derived normal CSV recovery | Five native suites, nine Python fixtures, seven static patterns; Windows/package checks recorded separately | Owner confirms FOV behavior; matching load/save/reset log. God OFF/protection mismatch reported; precise cause unconfirmed |

| 2026-10-11 / test 7 | Session God fix, expanded PLAYER and compact menu | Matching build/logs: valid fields, God OFF/death/recovery, saved FOV and navigation | Owner accepts overall behavior; returned run does not instrument each toggle |
| 2026-10-11 / test 8 | Adjustable movement; loaded weapons, linked grenades, Rapid Fire, explosive bullets, clock/AI/gravity | Returned log plus owner feedback; old 41-unique claim corrected after overlap discovery | Invisible/empty Give All and LOCKED Rapid; movement/world option paths observed; no new dump |
| 2026-10-11 / test 9 | First banks/dual prototype and corrected Rapid Fire | Matching estate run and owner feedback | Checkpoint baseline contamination; failed OFF cleanup; G18 veto/dual FPS loss. Other controls owner-accepted |
| 2026-10-11 / test 10 | Checkpoint provenance, selective undo, held-gun normal-animation fallback, caching and performance diagnostics | Eight C++ suites, 12 Python fixtures, 57 native anchors, AMD64 PE, actual-test9 upgrade | Owner retail acceptance pending |

Returned owner evidence is redacted into the versioned runtime JSON files; full
logs/dumps stay private. No retail game is executed in this Linux workspace.
The next acceptance gate is test10: banks after an earlier-bank checkpoint, actual
OFF removal, G18/other dual appearance/firing/reload, fixed-camera FPS and both
OFF orders. History is session-local; cross-process saved-checkpoint cleanup
requires an original baseline and is not implemented. After acceptance, move
to the BO1 controller and verified actor adapter.
Saved FOV remains a regression check. Second-map/reset
coverage and native BO1 actor/asset compatibility remain separate gates.
