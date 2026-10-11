# BO1 Zombies in MW2

Incremental mod framework for **retail Modern Warfare 2 (2009), single player**.
The first deliverable is an AMD64 D3D9 DLL that renders a keyboard menu in the
game. BO1 Zombies gameplay is a future mode. The current test10 build repairs
checkpoint bank recovery and inventory OFF restoration, adds held-gun dual
animation compatibility, and removes repeated weapon scans. The compact menu,
PLAYER/WORLD controls, saved FOV and BO1 round preview remain available.

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
Explosive Bullets, Slow Motion, actor-think Freeze AI and player/projectile
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
where assets are available. New retail animation/undo/FPS results are pending.

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

## Current WEAPONS test10

1. Close MW2 and extract the entire `BO1MW2-weapons-test10.zip` outside the game folder.
2. Run `Uninstall.cmd` to remove the earlier project build, then `Install.cmd`
   and select the folder containing `iw4sp.exe`. Find it through Steam:
   MW2 Single Player > Properties > Installed Files > Browse.
3. Launch normally through Steam and **restart the mission once** for this
   first test, so an older modified save is not captured as the baseline. Wait for player control,
   then press **F4**. Up/Down selects, Enter toggles/opens, Left/Right changes
   the Give All bank or speed/jump/time/gravity, Backspace/Esc goes back.
   F4 closes the menu.
4. Test bank selection after checkpoint reload, actual weapon removal on OFF,
   G18 Dual ON/OFF, fixed-camera FPS and both OFF orders. Follow
   [weapons-test10.md](docs/weapons-test10.md)
   for the sequence and exact scopes. Keep Infinite Ammo and No Reload OFF
   for initial ammo checks. Turn Give All and Dual Wield OFF before saving or
   quitting: their undo snapshots are in-process, with lifetime/identity guards.
5. Close MW2 and run `Diagnose.cmd` before another launch replaces the log.
   Return its ZIP and [testing.md](docs/testing.md) observations. Saved FOV
   should persist; other requests/selections reset on a full process restart.

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
| FOV selector and saved preference | Owner confirms test6 behavior; matching logs show load/selection/save/reset |
| WEAPONS | Test10 checkpoint/provenance-based undo, held-gun dual animation fallback, cached metadata/performance diagnostics; Rapid Fire and previous controls retained; new retail test pending |
| WORLD | Adjustable time/gravity, actor-think freeze, reset; test8 logs confirm several apply/restore paths; broader gameplay coverage pending |
| Kill All Enemies, AI spawning, Zombies gameplay | Planned; no action |

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
bypasses and the sole player gravity literal. The offline verifier and runtime
scanner both count overlapping matches. The corrected catalog pattern includes
the initializer prologue instead of matching repeated adjacent memset blocks.
Weapon banks use bounded data writes and conditional rollback; no native
grant/allocation routine is called. OFF uses the captured original inventory
only with a validated player/map/asset scope. Introduced weapon/pool IDs persist
through bank changes and same-process checkpoints; slot reorder and older banks
do not replace the original baseline. Invalid reads defer recovery. Native
normal animation names/arena definitions can be loaned to the two hand slots,
with conditional restoration; no native allocator or actor method is called.
Each new feature fails closed independently of the core patterns. Reserve
edits compare ID/count together; speed restores its captured baseline. The
verified 39/78 jump float pair uses bounded selected heights only in this process; its data
page protection is restored after the edit, and the pair is restored on OFF
or loss of player readiness. Instructions and the disk executable are unchanged.
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
- [BO1 tuning evidence](docs/bo1-normal-tuning-evidence.json): selected local CSV values and pointer-resolution limits.
- [Roadmap](docs/roadmap.md): incremental gates and persistent FOV request.
- [Reference projects](docs/references.md): upstream sources and limits of reuse.
- [Input hashes](docs/input-manifest.json) and [profile checks](docs/profile-checks.json).

The core foundation and test5 front-end preview are accepted in their observed
sessions. PLAYER test7 is owner-accepted following the God-state fix. Next test
test10 checkpoint/undo/dual/FPS acceptance, then prioritize the BO1 round/spawn
controller and IW4 actor adapter while auditing remaining
BO1 overrides/dependencies and broaden campaign-map/reset coverage.
AI and asset injection need separate compatibility and behavior tests; shared
engine ancestry does not imply compatible binary layouts or actor behavior.
