# BO1 Zombies in MW2

Incremental mod framework for **retail Modern Warfare 2 (2009), single player**.
The first deliverable is an AMD64 D3D9 DLL that renders a keyboard menu in the
game. BO1 Zombies gameplay is a future mode. The current build fills out the
PLAYER controls, fixes God-state reconciliation and shrinks the menu. The saved
FOV selector remains available; the BO1 round preview remains read-only.

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
all eight PLAYER rows at once. New retail behavior awaits the owner test.
Player cheats start OFF on a full restart; explicitly saved FOV persists.

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

## Current PLAYER test7

1. Close MW2 and extract the entire `BO1MW2-player-test7.zip` outside the game folder.
2. Run `Uninstall.cmd` to remove an installed earlier project build, then run
   `Install.cmd` and paste the folder containing the installed `iw4sp.exe`.
   Find it through Steam: MW2 Single Player > Properties > Installed Files > Browse.
3. Launch MW2 Single Player normally through Steam. Load a campaign mission and
   wait until you can move the player.
4. Press **F4**. Use **Up/Down**, **Enter**, and **Backspace/Esc**. F4 closes the menu.
   WASD, mouse look, and firing remain available; opening the menu does not pause.
5. First resume the checkpoint that stayed protected. With God and Noclip OFF,
   ordinary enemy gunfire should reduce health without cycling God ON/OFF.
   Check God ON/OFF, checkpoint loads with each choice, and a full restart with
   God left ON before quitting. A new process starts God OFF.
6. Test the remaining PLAYER controls one at a time and then Reset player
   toggles. Check that all eight rows fit and the text is smaller. Saved FOV
   should still restore. Follow [player-test7.md](docs/player-test7.md) for the
   exact sequence, native ammo exceptions, and GUARDED/LOCKED labels.
7. Close the game and run `Diagnose.cmd`. Return its ZIP and the observations
   requested in [testing.md](docs/testing.md).

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
| God Mode | Test4 accepted; test6 desync reported. Test7 reconciles session state and exposes other protection; owner regression test pending |
| Infinite Ammo | Test7 replenishes existing reserve pools to 999, with guarded cleanup; owner test pending |
| No Reload | Test7 uses native clip-sustain bool and native weapon exceptions; owner test pending |
| Noclip | Test7 uses native movement flags/type; owner flight/collision test pending |
| Super Speed | Test7 preset 380 vs native default 190, with baseline restore; owner test pending |
| Super Jump | Test7 native fixed-height data compatibility, 39 to 117; owner test pending |
| Reset player toggles | Requests every player cheat OFF and keeps saved FOV |
| Status HUD toggle and state-log action | Implemented |
| BO1 round-rules preview | Owner test5 front-end navigation passed; normal CSV values now recovered locally and match the preview |
| FOV selector and saved preference | Owner confirms test6 behavior; matching logs show load/selection/save/reset |
| Weapons, world, spawning, Zombies gameplay | Visible placeholders with no action |

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
Each new feature fails closed independently of the core patterns. Reserve
edits compare ID/count together; speed restores its captured baseline. The
verified 39/78 jump float pair becomes 117/234 only in this process; its data
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
- [BO1 tuning evidence](docs/bo1-normal-tuning-evidence.json): selected local CSV values and pointer-resolution limits.
- [Roadmap](docs/roadmap.md): incremental gates and persistent FOV request.
- [Reference projects](docs/references.md): upstream sources and limits of reuse.
- [Input hashes](docs/input-manifest.json) and [profile checks](docs/profile-checks.json).

The core foundation and test5 front-end preview are accepted in their observed
sessions. FOV test6 is owner-confirmed, with a God-state regression. Next test
the expanded PLAYER controls and restart/checkpoint fix, audit remaining BO1
overrides/dependencies, and broaden campaign-map/reset coverage.
AI and asset injection need separate compatibility and behavior tests; shared
engine ancestry does not imply compatible binary layouts or actor behavior.
