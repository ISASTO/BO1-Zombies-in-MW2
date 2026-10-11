# BO1 Zombies in MW2

Incremental mod framework for **retail Modern Warfare 2 (2009), single player**.
The first deliverable is an AMD64 D3D9 DLL that renders a keyboard menu in the
game. BO1 Zombies gameplay is a future mode. The current build adds a read-only
preview of source-derived BO1 round defaults.

**Status: Phase 1 core passed in the owner's test4 estate run.** The owner
confirms correct health, working God ON/OFF and retained God state through
checkpoint reloads. Matching diagnostics show complete player reads, five God
toggles preserving other flags, working menu rendering/input, and God blocked
back at the main menu. No new crash evidence is included. See
[test4 evidence](docs/runtime-test4-evidence.json).

`0.1.4-round-test5` adds **ZOMBIE MODE > Round rules preview**. It uses actual
local BO1 GSC fallback defaults and numeric semantics verified in the supplied
BlackOps.exe. The tuning CSV is still missing, so these are explicitly labeled
defaults, not final retail/Kino tuning. No Zombies actors or game are started.

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

## Current round test5

1. Close MW2 and extract the entire `BO1MW2-round-test5.zip` outside the game folder.
2. Run `Uninstall.cmd` to remove an installed earlier project build, then run
   `Install.cmd` and paste the folder containing the installed `iw4sp.exe`.
   Find it through Steam: MW2 Single Player > Properties > Installed Files > Browse.
3. Launch MW2 Single Player normally through Steam. Load a campaign mission and
   wait until you can move the player.
4. Press **F4**. Use **Up/Down**, **Enter**, and **Backspace/Esc**. F4 closes the menu.
   WASD, mouse look, and firing remain available; opening the menu does not pause.
5. Open ZOMBIE MODE > Round rules preview. With Preview round selected, use
   Left/Right or Enter to change round 1-100. Compare the values in
   [round-test5.md](docs/round-test5.md), test nested back/close, and briefly
   confirm the existing health/God behavior in a campaign mission.
6. Close the game and run `Diagnose.cmd`. Return its ZIP and the observations
   requested in [testing.md](docs/testing.md).

For manual installation, copy only the package's `d3d9.dll` beside `iw4sp.exe`.
Never overwrite an existing `d3d9.dll`; temporarily move a previous wrapper out
of the folder for this isolated test. Keep the original Bink DLL and all fastfiles.
For manual removal, close the game and remove that copied `d3d9.dll`.
`Uninstall.cmd` removes an installer-owned DLL after checking its receipt and hash.

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
| God Mode | Test4 owner confirms ON/OFF and tested checkpoint persistence; no continuous reapplication |
| Status HUD toggle and state-log action | Implemented |
| BO1 round-rules preview | Read-only normal-solo script defaults; portable fixtures pass, retail test5 pending |
| FOV selector and saved preference | Requested; deferred to a later focused iteration |
| Ammo, weapons, world, spawning, Zombies | Visible placeholders with no action |

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
callback. Only God Mode changes game data, using a single atomic flag-bit toggle.

Engine resolution requires the full executable SHA256, five unique `.text`
patterns, name anchors, in-image pointer slots, typed/named dvars, a running server,
a nonempty map name, a valid slot-zero entity, and positive player health.
The owner logs resolved all five signatures and name anchors in the live process.
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
- [Roadmap](docs/roadmap.md): incremental gates and persistent FOV request.
- [Reference projects](docs/references.md): upstream sources and limits of reuse.
- [Input hashes](docs/input-manifest.json) and [profile checks](docs/profile-checks.json).

The core foundation is accepted in the tested session. Next test the read-only
preview, recover final BO1 tuning, and broaden campaign-map/reset coverage.
AI and asset injection need separate compatibility and behavior tests; shared
engine ancestry does not imply compatible binary layouts or actor behavior.
