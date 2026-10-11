# BO1 Zombies in MW2

Incremental mod framework for **retail Modern Warfare 2 (2009), single player**.
The first deliverable is an AMD64 D3D9 DLL that renders a keyboard menu in the
game. BO1 Zombies is a future mode, not implemented in this first test.

**Status: the owner reports that the in-game menu works in test 3.** The returned
log confirms rendering at 3440x1440, F4 input, all six categories, 82 keyboard
actions, and a return from `estate` to no active level. There are no new crash
files or crash events in that bundle. Test 1's graphics-table crash is resolved
in this observed run. Live gameplay/control recovery still needs explicit feedback.

`0.1.3-player-test4` corrects the next Phase 1 issue: the player identity guard
used an entity-type field instead of the native 16-bit entity number. Test 3
therefore kept God Mode locked and never confirmed player health. See
[player test 4](docs/player-test4.md) for the evidence and focused next test.

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

## Current player test 4

1. Close MW2 and extract the entire `BO1MW2-player-test4.zip` outside the game folder.
2. Run `Uninstall.cmd` to remove an installed earlier project build, then run
   `Install.cmd` and paste the folder containing the installed `iw4sp.exe`.
   Find it through Steam: MW2 Single Player > Properties > Installed Files > Browse.
3. Launch MW2 Single Player normally through Steam. Load a campaign mission and
   wait until you can move the player.
4. Press **F4**. Use **Up/Down**, **Enter**, and **Backspace/Esc**. F4 closes the menu.
   WASD, mouse look, and firing remain available; opening the menu does not pause.
5. Check that the mission reports an active, living player with positive health.
   Open PLAYER, turn God Mode on, and test ordinary enemy gunfire. Turn it off
   and confirm damage returns. Also check gameplay while the menu is open,
   checkpoint reload, and return to the main menu.
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
| Normal Steam launch; no external trainer | Owner confirms test 2 and menu test 3 work |
| In-game F4 menu and category navigation | Owner reports success; test 3 log confirms render/input and all categories |
| Render-state preservation and reset handling | Basic rendering observed; reset and overlay interoperability pending |
| Active-level / player-health display | Server/map reads observed; test 4 corrects identity gate, health test pending |
| God Mode | Test 3 blocked by identity guard; test 4 damage test pending |
| Status HUD toggle and state-log action | Implemented |
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
uses that 16-bit field. Portable tests reproduce the former failure and verify
unreadable/wrong-slot/implausible-health guards. Health and God behavior still
require mission observations. Failed field reads display UNKNOWN health.
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
- [Test 3 evidence](docs/runtime-test3-evidence.json) and [player test 4](docs/player-test4.md): working menu, corrected identity field, and next test.
- [Reference projects](docs/references.md): upstream sources and limits of reuse.
- [Input hashes](docs/input-manifest.json) and [profile checks](docs/profile-checks.json).

The render/input path now has an observed retail success. Next validate player
health, God-on/God-off damage, level gating, live gameplay, and checkpoint/mission
transitions. Renderer reset remains a separate robustness check. BO1 AI and asset
injection wait until the Phase 1 foundation is accepted.
