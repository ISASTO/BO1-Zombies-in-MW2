# BO1 Zombies in MW2

Incremental mod framework for **retail Modern Warfare 2 (2009), single player**.
Current build: **0.1.43-zombies-test44**.

**Test44 fixes what the first Test43 run showed:**

- **Favela and Terminal crashed while loading.** The error was "Exceeded
  limit of 1350 'loaded_sound' assets": the BO1 sounds pushed MW2's sound
  pool over its fixed size. The pool is now enlarged to 4096 from `DllMain`,
  before `DB_Init` links the pools. Every table entry and routine is checked
  first. A new exact-native probe runs the retail `DB_Init`, allocator and
  free routine on it.
- **Nobody reached the player in Gulag showers.** Zombies spawned in other
  wings of the prison. The mod now reads MW2's navigation graph: the path
  nodes and the currently connected links, so closed doors count. It spawns
  zombies only where a walking route leads to the player, chosen by walking
  distance. This also keeps them off Favela's roofs.
- **A "Missing tag [j_helmet]" error ended Gulag.** Zombies are no longer made
  from drone, riot shield or civilian templates.
- **Zombies swung from several metres away.** A swing now starts only within
  64 units (BO1's `meleeAttackDist`) and about the same height.
- **Zombies briefly ran like MW2 soldiers.** Turn anims are disabled. The
  stairs, sideways-walk and second CQB run anims now play BO1 zombie anims
  (76 slots). The two run attacks moved off MW2's running bullet reactions.

See [Test44](docs/zombies-test44.md) and the
[Test44 test instructions](docs/test44-testing-protocol.md).

**Test43 fixes the mangled zombie models and adds BO1's zombie sounds.**

- **The models.** Black Ops stores bone offsets packed, 12 bytes per bone, in a
  block sized for 16. The importer read them 16 bytes apart, which folded and
  stretched the limbs. They are now read packed, and every converted model
  must chain back to its own rest pose before it is used.
- **The sounds.** The sounds are built from the owner's Black Ops install
  into a cached MW2 sound fastfile:
  - Black Ops' sound bank in `common_zombie.ff`;
  - the round music from Kino and the `.iwd` archives.

  The xWMA sounds are decoded by a port of FFmpeg's WMA decoder
  (`third_party/wmadec`, LGPL). Zombies, hellhounds, powerups and rounds play
  them through MW2's own sound system:
  - zombies: moans, sprint moans, attack vocals, swing whooshes, the "behind
    you" moan, death vocals, rising dirt and per-surface zombie footsteps;
  - hellhounds: lightning, growls and explosions;
  - powerups: drop, grab and stingers;
  - rounds: round start, round over, dog round and game over music.

  MW2's soldier pain/death voices are dropped for zombies. The generated file
  loads through the retail MW2 loader code in a new exact-native probe.

See [Test43](docs/zombies-test43.md) and the
[Test43 test instructions](docs/test43-testing-protocol.md).

**Test42 fixes the freeze.** Test41's hang reports caught the game stuck
releasing `"zombie_rise"`/`"zombie_attack"`, script strings that had already
been freed.

MW2's `animscripted` releases its own arguments in the middle of the call
and keeps using the notify name. Script literals survive that; the mod's
strings did not. The mod now pins every string it passes to a builtin or
script thread, as a compiled script does.

The crash guard now skips the models for one run only. See
[Test42](docs/zombies-test42.md) and the
[Test42 test instructions](docs/test42-testing-protocol.md).

**Test41 fixes the first Test40 run.**

- **The freeze.** It came from three BO1 attack animations that carry the
  root-motion flag without root-motion data. MW2's sampler read the missing
  data when a zombie first swung. A new exact-native probe runs the retail
  sampler on every converted animation.
- **The models.** The BO1 model zone now loads before the level zone, so the
  models exist when the level scripts precache them.
- **Rooftop spawns.** A zombie the engine finds no path for is respawned, and
  that spawn point is dropped.
- **Freeze diagnostics.** A freeze now writes its own report: a hang
  watchdog plus a record-only fault recorder.

See [Test41](docs/zombies-test41.md) and the
[Test41 test instructions](docs/test41-testing-protocol.md).

**Test40 imports the BO1 zombie models.** Zombies now wear Kino der Toten's
zombie: the two honor-guard bodies and four zombie heads, picked at random as
BO1's character script does. The models are converted on the owner's
computer from the owner's own Black Ops files (`zombie_theater.ff` and the
`.iwd` textures) into an MW2 fastfile. That file is cached and loaded with
each zombies level. The generated file loads unchanged through the retail
MW2 loader code in an exact-native probe. A crash guard skips the models
after an abnormal exit. See [Test40](docs/zombies-test40.md) and the
[Test40 test instructions](docs/test40-testing-protocol.md).

**Test39 moves zombies onto the Spec Ops maps and imports the first BO1
animations.** The map list is now 19 Spec Ops missions, which are small,
bounded play areas. They load with the Spec Ops menu's own command sequence,
including from the main menu. The missions' end-of-level paths are disabled
while the level's scripts load, so objectives, timers and leaving the area no
longer end a game. Dying loads the map again for a new game.

The animations come from the owner's own Black Ops install (located by the
installer; nothing BO1 is shipped). Zombies:

- climb out of the ground with BO1's rise animations;
- walk, run, sprint and idle with BO1's animations;
- swing with BO1's full attack set (walker and runner variants), one hit per
  swing in the animation, and break off to chase when you back away;
- die with BO1's death animations, and can die while rising or swinging.

These animations are converted into MW2's own XAnimParts at run time. Each
zombie is also set up the way BO1's `zombie_spawn_init` does it, through
MW2's own script functions: no rifle, no pain or battle chatter, and real
sprinters. See [Test39](docs/zombies-test39.md) and the
[Test39 test instructions](docs/test39-testing-protocol.md).

**Test38 added the first playable zombies mode.** Open it with F4 → ZOMBIE MODE
→ Playable zombies, on the current level or on any campaign map (that also
works from the main menu). It uses BO1 solo rules ported from the owner's
local BO1 scripts: rounds, spawning at campaign enemy spawn points, walk/run
speeds, pursuit, the melee rule, points, the four powerups and optional
hellhounds. The player starts with only a USP .45.

Test38 also fixes spawned friendlies standing in clumps. They now follow the
player as their native goal entity, with a crowd- and enemy-scaled goal
radius, and up to 32 actors are serviced per server frame.

The zombies are still the map's soldiers, with no BO1 animations or models
yet. See [Test38](docs/zombies-test38.md) and the
[Test38 test instructions](docs/test38-testing-protocol.md).

Test37 fixes the cause of the false "child1/parent exceeded" errors and the VM
crash seen in Test35/36: object ids above 0x8fff were not recognised as
object keys (notify/endon wait lists), so waiting threads lost their
references. It also makes saved object numbers unsigned (checkpoints with
32,768+ objects), expands entity handle records 4,095 → 32,767, adds
telemetry for the last unmeasured fatal limits, and records the full 32 → 256
[capacity sweep](docs/capacity-sweep-test37.md). See [Test37](docs/squad-test37.md).

Test36 fixes Test35's checkpoint corruption (false "child0/child1 exceeded"
errors and a black screen after loading a checkpoint): the savegame marker
reached the relocated object maps through a VM-globals base register that
Test35 did not relocate, so every save cleared live script objects. See
[Test36](docs/squad-test36.md). Test35's capacity changes are kept:

Test34's eight child pools held (121k of 409k used at 200 actors). Estate then
stopped at ~160 and Contingency at ~200 with "exceeded maximum number of parent
script variables": each actor keeps ~120 script objects (mostly threads), and
combat swings the total by thousands. Test35 gives parents the whole 16-bit id
space (36,856 → 65,526) by moving the child pools above it, shares one helper
spawner per actor class instead of one per actor (an entity each), sizes the
spawn guard to those swings, sheds owned actors before any fatal pool runs out,
and keeps previous runs' logs. See [Test35 diagnosis](docs/squad-test35.md);
Test34's pool and memory changes are in [Test34](docs/squad-test34.md).

**The live target remains 256 actors / 274 sentients.** The Test30 renderer
bridge, 72 MiB GPU/CPU skinning buffers, guarded animated-record storage and
16-bit handles remain active. Rendering and actor regressions are rerun.
Windows missions and stable 256-actor retail gameplay require owner testing.

Install the complete Test44 package (run its Uninstall.cmd first to remove
Test43). Follow the [Test44 instructions](docs/test44-testing-protocol.md):
Favela, Terminal, Gulag showers and Contingency. The
[Test43 instructions](docs/test43-testing-protocol.md) describe the sounds in
more detail; the [Test42](docs/test42-testing-protocol.md) and
[Test41 instructions](docs/test41-testing-protocol.md) cover the same maps:
Favela with zombies attacking for a few minutes, then Arcadia and Gulag
showers, with screenshots. The [Test40 instructions](docs/test40-testing-protocol.md)
describe the models in more detail. The
[Test39 instructions](docs/test39-testing-protocol.md) still cover the BO1
rise, attacks and deaths and the GAME OVER reload. The
[Test38 instructions](docs/test38-testing-protocol.md) still cover the
squad-follow checks, and the [Test37 retest instructions](docs/test37-testing-protocol.md) still
cover the 256-actor and checkpoint checks. The
[full crowd protocol](docs/test31-crowd-protocol.md) covers later stages.

Original MP hitmarker image/sound/fade, menu options, prior animation ownership
fixes and version-1 expanded checkpoints remain included. Historical shot rewind
keeps its coordinated 32-record representation; higher actors use current
server poses during shots. Stock checkpoints retain migration support. The
installer extracts the original MP image from local main/*.iwd and backs up
player/save data before installation.

Test20 fixes a native string-reference bug in squad ownership labels. Repeated
user-1 intern lookups return a borrowed id, but the old code released it after
assigning each entity's label. The native regression creates 32 labeled fields
with only one remaining reference, frees their shared slot on the first cleanup,
then shows stale releases decrementing a reused model/VM code pointer. Corrected
assignments retain all 32 field references, survive full cleanup and repeated
replacement cycles. This is a concrete pool-corruption fix; the returned Test20 crowded-squad run
and owner report now provide positive retail evidence. See [Test20 evidence](docs/squad-test20.md).

Test19 addresses the returned large-squad script-stack underflow with a full
idle VM work context, caller frame/local-state restoration, fresh target
validation and enemy-change notification deduplication. The dump's last
population was 26 active actors and no slot reclamation occurred in that run.
The corrected VM pointers and explicit memory ranges make subsequent dumps
more useful. The Windows crash reproduction still requires owner testing.
See [Test19 findings and checks](docs/squad-test19.md).

Test18 fixes the World help-text overflow that crashes menu navigation and
isolates the script parameters of a campaign spawn before reclaiming a mod NPC.
An isolated run of the exact native notification routine reproduces the old
parameter/reference consumption and verifies the fix. **Allow Friendly Fire**
now removes player attribution from allied hits, preserving native damage and
reactions while bypassing the player friendly-fire penalty. OFF restores ordinary
attribution. Each crash retains its own log and recent native operations.
The crowded-squad crash and mission-failure bypass still require an in-game
retest; its returned dump alone cannot establish the entire corruption chain.
See [Test18 findings and checks](docs/crash-test18.md).

Test17's native autosaves, saved ownership tags, validated actor/helper adoption
after load, and recovery placement remain in place. The later returned run
retained and restored ten spawned actors, then removed all ten successfully.

The Test21 prototype remains documented in [its native results](docs/expanded-lifecycle-test21-probe.json).
Test22's [runtime-plan fixture](docs/expanded-runtime-test22-probe.json) closes
snapshot and native population stream coverage and ships the experimental
activation code. This is a test build, not a claim of proven 256-AI retail stability.
The 1x-10x Rapid Fire, No Recoil and named model rows remain included.
The owner confirms No Recoil and Kill All Enemies work.
Ghost/Price/Soap/Roach require a matching loaded character in the current mission;
Soldier uses a loaded human spawner. Universal character asset loading is pending.

Test16's returned logs now capture/apply the loaded Ghost appearance, including
the native empty attachment tag. Model application in a log is not visual
acceptance of the character's rendering.
G18's overlapping dual-wield models remain deferred. BO1 gameplay remains a
simulated controller rehearsal.

The following paragraphs record earlier iterations and their original test state.

**Status: Phase 1 core passed in the owner's test4 estate run.** The owner
confirms correct health, working God ON/OFF and retained God state through
checkpoint reloads. Matching diagnostics show complete player reads, five God
toggles preserving other flags, working menu rendering/input, and God blocked
back at the main menu. No new crash evidence is included. See
[test4 evidence](docs/runtime-test4-evidence.json).

The owner also confirms test5 works. Its matching front-end diagnostics cover
all 100 preview rounds and navigation, with no new crash evidence. That run did
not enter a mission. See [test5 evidence](docs/runtime-test5-evidence.json).

The owner confirms test6 FOV behavior, but reports God protection surviving a
return/restart while its menu said OFF. Matching logs show health 2 after an
ON/OFF cycle; the old diagnostics did not record other protection flags. See
[test6 evidence](docs/runtime-test6-evidence.json).

`0.1.6-player-test7` adds explicit session God state and checkpoint reconciliation,
independent protection diagnostics/cleanup, Infinite Ammo, No Reload, native
Noclip, Super Speed, Super Jump, and Reset player toggles. The smaller menu shows
all eight PLAYER rows at once. The owner now reports normal behavior; matching
test7 logs show death/recovery with God OFF, valid player reads and saved FOV.
Those logs contain no per-feature toggle actions, so detailed test coverage
is owner-reported. See [test7 evidence](docs/runtime-test7-evidence.json).
Player cheats start OFF on a full restart; explicitly saved FOV persists.

`0.1.7-world-weapons-test8` adds Left/Right strengths for speed and jump, loaded
weapon access, linked frag/flash sustain, current-gun Rapid Fire, native global
Explosive Bullets, World Speed, actor-think Freeze AI and player/projectile
Gravity. Kill All Enemies remains planned. See [test8 checks](docs/world-weapons-test8.md).
The returned test8 run confirms adjustable movement and several world toggles,
but the owner reports invisible/empty Give All weapons and LOCKED Rapid Fire.
The runtime scanner correctly rejected three overlapping catalog matches;
the old offline verifier incorrectly consumed matches and missed the overlap.
See [test8 evidence](docs/runtime-test8-evidence.json).

`0.1.8-weapons-test9` corrects the unique catalog signature and verifier, grants
only eligible loaded primary guns with initialized native ammo pools, and adds
Force Dual Wield for verified compatible guns. Both toggles capture undo state
and coordinate OFF order. The returned estate run confirms Rapid Fire and bank
switching, but reports checkpoint bank collapse, ineffective OFF after recovery,
G18 dual failure and reduced FPS. See [test9 evidence](docs/runtime-test9-evidence.json).

`0.1.9-weapons-test10` retains original inventory plus introduced IDs across
checkpoints/banks, removes introduced guns by identity, validates equipment
without firing-time assumptions, caches metadata and updates only the held gun.
A reversible normal-animation fallback bypasses the verified campaign veto
where assets are available. Test11 feedback confirms checkpoint-bank recovery;
its dual validator was still rejecting native animation arrays before applying.

`0.1.10-zombies-controller-test11` adds source-derived normal-solo round lifecycle,
budget/capacity accounting, paced request/confirmation callbacks, lifecycle
guards and a simulated adapter inside the menu. A scoped BO1 player-melee
admission rule and local dependency/asset-reference auditor are tested in source.
The owner now confirms the rehearsal behaves correctly. Matching estate logs
show rounds 1, 2, 3 and the debug round 10 profile with valid accounting. No live
BO1 zombie behavior, damage or asset adapter is enabled.
See [test11 instructions](docs/zombies-controller-test11.md) and
[portability audit](docs/bo1-portability.md).

`0.1.11-ai-test12` computes bank counts before enabling/cycling Give All, and
validates the animation entries actually consumed by the native builder:
1..36, a required idle, and optional empty names using the idle fallback.
Dual failures now report their specific gate. A separately verified native
actor-think callback services bounded spawn/delete requests on the engine's
own callback thread; normal MW2 NPCs retain native initialization and behavior.
See [test12 checks](docs/ai-test12.md), [test11 feedback](docs/runtime-test11-evidence.json)
and [exact AI binding evidence](docs/ai-test12-evidence.json).

`0.1.12-spawn-test13` follows the owner's test12 enemy-spawn and working-dual
evidence. It removes the mod's eight-actor cap, places enemies at the nearest
eligible point with clearance retries, creates friendlies near the player using
loaded templates/team conversion, and requests native advance/follow goals.
For remaining G18/other dual failures it builds distinct native hand definitions
from loaded normal animation names on the engine callback thread. These new
native paths need owner testing. The physical actor pool remains 32 slots.
See [test13 instructions](docs/spawn-test13.md), [test12 runtime evidence](docs/runtime-test12-evidence.json)
and [BO1 mode design](docs/bo1-mode-design.md).

The supplied BO1 `common_zombie.ff` resolves the normal tuning CSV. Recovered
values match the preview, so its numbers are unchanged. Special rounds and
final map/runtime overrides remain outside that preview.

## Exact supported executable

The supplied `iw4sp(1).exe` is the September 16, 2026 **64-bit** Steam build,
not the historical x86 executable. Use its normal installed name, `iw4sp.exe`.

```
SHA256 fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777
PE     AMD64 / PE32+ / ASLR
```

An executable hash mismatch disables game-state access and memory writes.
The installer rejects a mismatched build. A manually installed proxy can still
show its diagnostic menu in an unknown AMD64 `iw4sp.exe`.

## Current squad/checkpoint test17

1. Close MW2 and extract the entire `BO1MW2-squad-test17.zip` outside the game folder.
2. Run `Uninstall.cmd`, then `Install.cmd`, selecting the folder containing
   `iw4sp.exe`. Saved FOV and logs are preserved.
3. Launch through Steam and **restart the mission once for this upgrade**.
   Avoid starting with NPCs saved by test13. Wait for player control, press F4,
   and test an enemy plus a Soldier helper before increasing population.
4. Follow [squad-test17.md](docs/squad-test17.md): confirm allies stay in place at
   an autosave, test death/reload and relaunch ownership, and check friendly fire
   ON/OFF. The installer backs up a local `players` folder if present. Saves
   stored elsewhere are outside that automatic backup.
   Campaign slot reclamation can still occur at the unchanged 32-slot limit.
5. Close MW2 and run `Diagnose.cmd` before another launch replaces `latest.log`.
   Return its ZIP and the checklist observations. Saved FOV persists; other
   toggles and character/model selection are session-only.

For manual installation, copy only the package's `d3d9.dll` beside `iw4sp.exe`.
Never overwrite an existing `d3d9.dll`; temporarily move a previous wrapper out
of the folder for this isolated test. Keep the original Bink DLL and all fastfiles.
For manual removal, close the game and remove that copied `d3d9.dll`.
`Uninstall.cmd` removes an installer-owned DLL after checking its receipt and hash.
It preserves `<MW2>\bo1mw2\settings.ini` and logs. Reinstalling/upgrading keeps
the saved preference. Use the FOV menu's Restore default action to clear it.

The status HUD should appear near the upper left. Logs are written to
`<MW2>\bo1mw2\logs\latest.log`; the fallback is `%LOCALAPPDATA%\BO1MW2`.
Copy the log immediately after a failing run: `latest.log` is replaced on launch.

## Implemented in the menu build

| Feature | State |
| --- | --- |
| Normal Steam launch; no external trainer | Owner confirms tests 2-4 work |
| In-game F4 menu and category navigation | Owner-confirmed working; matching logs confirm render/input |
| Render-state preservation and reset handling | Basic rendering observed; reset and overlay interoperability pending |
| Active-level / player-health display | Test4 complete player reads and owner-confirmed correct health |
| God Mode | Test7 owner-accepted after session reconciliation/guard handling; matching OFF/death/recovery evidence |
| Infinite Ammo | Existing reserve pools to 999 with guarded cleanup; test7 owner-accepted |
| No Reload | Native ammo sustain; normal gun clips and frag/flash grenades are linked; test7 owner-accepted |
| Noclip | Native movement flags/type; test7 owner-accepted |
| Super Speed | Test8 Left/Right 1x–5x in 0.5x steps; returned log confirms adjustment/apply/restore |
| Super Jump | Test8 Left/Right 1x–8x height in 1x steps; returned log confirms adjustment/apply/restore |
| Reset player toggles | Requests every player cheat OFF and keeps saved FOV |
| Status HUD toggle and state-log action | Implemented |
| BO1 round-rules preview | Owner test5 front-end navigation passed; normal CSV values now recovered locally and match the preview |
| BO1 controller rehearsal | Test11 owner-accepted; simulated actors only, source-derived normal solo lifecycle and game-clock guards |
| FOV selector and saved preference | Owner confirms test6 behavior; matching logs show load/selection/save/reset |
| WEAPONS | Hitmarkers and held-fire semi-auto conversion added in Test21. No Recoil and 1x-10x Rapid Fire retained. G18 dual appearance is deferred. |
| WORLD | Test18 fixes help-text bounds and adds player friendly-fire penalty bypass; native damage/mission behavior needs retail retest. |
| AI | Test20 crowded-squad retail run is stable. Test21 adds invincible spawned allies and advances native 256/274 cleanup/population serialization. Test25 retains native 256 capacity and corrects animation-tree ownership/corpse lookup; corrected retail animation and stress testing is pending. |
| Kill All Enemies | Owner-confirmed working native damage call for hostile actors; allies, clients and protected entities excluded. |
| BO1 Zombies gameplay | Planned; normal MW2 AI is not a BO1 behavior/animation/assets port. |

## Architecture

The executable imports `Direct3DCreate9` from `d3d9.dll`. An application-directory
proxy loads the real DLL by absolute System32 path and forwards the factory call.
It patches only the existing table's `CreateDevice` slot, then the device table's
`EndScene`, `Present`, `Reset`, and `Release` slots. Native object vptrs and all
unselected public/private slots remain intact. Originals are saved per shared
table; untracked instances forward normally. Per-device records own the menu.
Patches use compare-and-swap and conditional rollback to preserve competing edits.

The first build instead copied only the documented public table entries. The
owner's Windows runtime calls private device slot 134 during `SetRenderState`;
that exceeded the 119-entry copy and caused an execute access violation. Merely
extending a guessed copy is insufficient. Test 3 preserves the complete native
table without assuming its private size. This finding does not change any IW4
or T5 game structure.

EndScene draws only on the backbuffer; Present provides a fallback scene if the
engine ended on an offscreen target. A state block and explicit transform/viewport
restoration protect the engine's rendering state. A local Windows font becomes
a managed texture atlas; no game fonts or third-party UI library are bundled.
The window procedure queues input; menu actions and rendering run on the frame
callback. Player preferences are process-wide and session-only. God/native
noclip flags are reconciled with compare/exchange every 16ms while ready.
God OFF clears God/demigod bits; independent invulnerability is cleared once
at a verified new lifetime or OFF/reset request, with later guards reported
separately. No health freeze loop runs. FOV uses an
atomic four-byte compare-and-exchange on the verified float current field,
only with a living player in a supported active level. The desired value is
checked at most every 250ms to recover level/checkpoint resets. No FOV override
is active on a clean install until the user selects or saves a value.

Engine resolution requires the full executable SHA256, five unique `.text`
patterns, name anchors, in-image pointer slots, typed/named dvars, a running server,
a nonempty map name, a valid slot-zero entity, and positive player health.
Two optional FOV signatures additionally corroborate its registration constants,
name and camera pointer slot. Sixteen optional player signatures validate
partial client/ammo fields, native dvar consumers, noclip and fixed jump data.
Thirty-four optional weapon/world/inventory/animation signatures corroborate native dvar
consumers, weapon tables/timing, owned slots/model/ammo/dual fields, grenade
bypasses and the sole player gravity literal. Forty additional actor/spawn/
placement/team/goal/hand-builder anchors bring the current total to 97 unique
signatures. The offline verifier and runtime scanner both count overlapping
matches. The corrected catalog pattern includes
the initializer prologue instead of matching repeated adjacent memset blocks.
Weapon banks use bounded data writes and conditional rollback; no native
grant/allocation routine is called. OFF uses the captured original inventory
only with a validated player/map/asset scope. Introduced weapon/pool IDs persist
through bank changes and same-process checkpoints; slot reorder and older banks
do not replace the original baseline. Invalid reads defer recovery. Native
normal animation names/arena definitions can initially be loaned to the two hand
slots with conditional restoration. Test13 queues separate native hand-definition
construction on the verified server-clock thread and publishes results through
the same ownership baseline. Actor allocation/copy/team/goal/removal/damage calls also
run only on that thread. Test14 atomically redirects three verified native CALL
sites through nearby relays; the complete non-relative actor-init prologue is
replaced on the server thread. Save and load boundaries remove owned NPCs;
rendering does not execute those native actor methods.
Each new feature fails closed independently of the core patterns. Reserve
edits compare ID/count together; speed restores its captured baseline. The
verified 39/78 jump float pair uses bounded selected heights only in this process; its data
page protection is restored after the edit, and the pair is restored on OFF
or loss of player readiness. The installed disk executable remains unchanged;
test14 applies the documented in-memory lifecycle hooks.
Settings use the mod's own versioned text file; the
DLL does not call a native dvar API or change archive/latched values. The native
camera applies ADS and scaling separately; broader cutscene/map coverage remains.
The owner logs resolved all five core signatures and name anchors in the live process.
Test 3 observed server/map transitions, but our incorrect `+0x00` integer identity
guard rejected the player. The exact executable's allocator assigns the entity
index with a WORD store at `+0x7e`; a separate native read corroborates it. Test 4
uses that 16-bit field, now confirmed by test4 live reads and owner observations.
Portable tests reproduce the former failure and verify
unreadable/wrong-slot/implausible-health guards. Failed reads display UNKNOWN.
No historical IW4 address is used. No T5 structure is cast as an IW4 structure.
Hooks are process-lifetime objects; DLL hot-unload is intentionally unsupported.
This proxy targets the supplied game's regular D3D9 device, not a general D3D9Ex
or arbitrary-game overlay. `Direct3DCreate9Ex` is forwarded without hooking.

## Build

Windows: Visual Studio C++ desktop tools, Windows SDK, and CMake.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The DLL is `build\RelWithDebInfo\d3d9.dll`. Copy it to `dist\d3d9.dll` to use the
source checkout's installer. MSVC links the C++ runtime statically.
The separate startup control and probe are described in
[startup-test2.md](docs/startup-test2.md); their delivered build uses MinGW.

Linux cross-build: MinGW-w64 x64, CMake, and make or Ninja.

```sh
cmake -S . -B build-win64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
cmake --build build-win64 -j
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native
ctest --test-dir build-native --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

For input metadata and signature verification, install `requirements-dev.txt`:

```sh
python3 tools/inspect_inputs.py /path/to/iw4sp.exe /path/to/BlackOps.exe
python3 tools/verify_profile.py /path/to/iw4sp.exe
python3 tools/verify_bo1_round_rules.py /path/to/BlackOps.exe \
  --zone /path/to/common_zombie_patch.ff
python3 tools/recover_bo1_tuning.py /path/to/common_zombie.ff
python3 tools/extract_bo1_scripts.py /path/to/zombie_theater.ff \
  /path/to/common_zombie_patch.ff /path/to/zombie_theater_patch.ff
```

Extracted sources live under ignored `local-data/bo1/<zone>/<asset path>`. Patch
sources must take precedence over base zones when resolving a final script set.
The extractor validates inline compressed records but is not a full XAsset parser.
Original games remain user-supplied inputs and must never be added to this repo.

## Research and next gates

- [Reverse-engineering ledger](docs/reverse-engineering.md): addresses, evidence,
  partial layouts, unresolved hypotheses, and test results.
- [BO1 research](docs/bo1-research.md): actual uploaded scripts, dependencies,
  and the A/B/C division for future translation.
- [Testing](docs/testing.md): exact observations needed to accept the milestone.
- [Menu test 3](docs/menu-test3.md) and [test 2 evidence](docs/runtime-test2-evidence.json): diagnosed launch failure and correction.
- [Test4 evidence](docs/runtime-test4-evidence.json) and [player test4](docs/player-test4.md): accepted health/God foundation.
- [Round test5](docs/round-test5.md) and [numeric evidence](docs/bo1-round-rules-evidence.json): current preview and provenance.
- [FOV test6](docs/fov-test6.md): selector, persistence and owner checks.
- [PLAYER test7](docs/player-test7.md): God regression, every player row and compact menu checks.
- [WEAPONS/WORLD test8](docs/world-weapons-test8.md): adjustable controls, precise scopes and owner checks.
- [WEAPONS test9](docs/weapons-test9.md): historical first bank/dual prototype.
- [WEAPONS test10](docs/weapons-test10.md): checkpoint/undo repair, animation compatibility, performance and final owner checks.
- [Native AI test12](docs/ai-test12.md): historical first native spawn/cleanup iteration.
- [Spawn test13](docs/spawn-test13.md): current placement, friendly fallback, goals, native hand setup and owner tests.
- [Test12 evidence](docs/runtime-test12-evidence.json): confirmed native enemy lifecycle, working generic dual and remaining failures.
- [BO1 mode design](docs/bo1-mode-design.md): clear/USP/round-one requirements, source-derived drops/revive, purchases and box-last order.
- [Test11 evidence](docs/runtime-test11-evidence.json): accepted checkpoint recovery/rehearsal, remaining UI and dual failures.
- [BO1 tuning evidence](docs/bo1-normal-tuning-evidence.json): selected local CSV values and pointer-resolution limits.
- [Roadmap](docs/roadmap.md): incremental gates and persistent FOV request.
- [Reference projects](docs/references.md): upstream sources and limits of reuse.
- [Input hashes](docs/input-manifest.json) and [profile checks](docs/profile-checks.json).

The core foundation, PLAYER and BO1 controller rehearsal are accepted in their
observed sessions. Test13 needs retail evidence for placement, friendly team/goal,
collision retries and distinct hand definitions. Verify independent game-thread
scheduling before clearing all actors, then validate one BO1 pursuit/melee/
animation fixture before connecting live round population. Broader campaign-
map/reset coverage and asset import remain separate checks.
AI and asset injection need separate compatibility and behavior tests; shared
engine ancestry does not imply compatible binary layouts or actor behavior.

## Third-party code

`d3d9.dll` compiles in two third-party libraries:

- **miniz** (`third_party/miniz`, tinfl inflate only, MIT) inflates Black Ops
  fastfiles and `.iwd` entries.
- **wmadec** (`third_party/wmadec`) is a C++ port of FFmpeg's WMA v2 decoder,
  LGPL 2.1 or later (`third_party/wmadec/COPYING.LGPLv2.1`). It decodes Black
  Ops' xWMA sounds on the owner's machine. The rest of the project is MIT.
  Every test package carries `docs/THIRD-PARTY-NOTICES.txt` and the LGPL text.
