# Menu test 3: preserve the native graphics vtable

## Confirmed failure in test 1

The owner supplied two diagnostic ZIPs and confirmed that normal Steam launch
works both with our DLL removed and with forwarding-only test 2 installed.
All system/old-DLL/control factory probes returned exit code zero. The old game
log matched the exact executable SHA256, resolved all five engine patterns/name
anchors, and successfully created the graphics device and installed input.
It never reached the first EndScene/Present callback.

All three test 1 minidumps have an execute access violation (`0xc0000005`,
exception parameter 0 = 8), with an invalid heap target and the same saved caller
return address: **System32 D3D9 RVA `0x11fc03`**. The faulting thread's device
pointer matches the logged device in the last run; render-state argument is
`0xce` (206). MW2 calls public `SetRenderState` at engine RVA `0x1087f`, slot 57.

The exact Windows system DLL was retrieved from Microsoft's public symbol server
for analysis. Its PE timestamp (`0x492c61a9`), image size (`0x1d1000`), and PDB
RSDS GUID/age match the dump. In that binary, the native SetRenderState function
at RVA `0x11fb00` dispatches through **vtable byte offset `0x430`, slot 134**;
the next saved return address is exactly the one in all three dumps.

Test 1 substituted a 119-entry public-only device table. Native slot 134 therefore
read beyond the copy into unrelated record data and attempted to execute it.
The fault was caused by our hook. It was not a hash mismatch or a missing DLL.
No claim is made that a generally complete native table has 135 entries, or that
using an Ex-sized copy would solve it. Private sizes are implementation details.

Only redacted addresses/identities and hashes are in
[runtime-test2-evidence.json](runtime-test2-evidence.json). Uploaded logs/dumps
and Microsoft/game binaries are local analysis inputs, not repository assets.

## Small correction

The native COM objects retain their original vptrs and full original tables.
We patch only these documented public slots:

| Table | Slots |
| --- | --- |
| IDirect3D9 | CreateDevice 16 |
| IDirect3DDevice9 | Release 2, Reset 16, Present 17, EndScene 42 |

Original function pointers are stored per table, and menu state is stored per
device. Because tables can be shared, calls for untracked instances forward to
the saved original without drawing or input changes. Compare-and-swap prevents
overwriting a competing slot change; a failed transaction conditionally rolls
back previously changed slots. No engine address/structure or BO1 logic changes
in this iteration. `SetRenderState` and all private entries remain native.

The portable regression fixture reproduces the observed public SetRenderState
to private-slot-134 dispatch. It verifies that two objects sharing a table retain
their vptrs, that the private method remains callable, that other slots are
untouched, and that a competing hook survives rollback. The new DLL builds for
AMD64 and retains the same ten exports and system imports as test 1.
The package scripts also passed PowerShell parsing and an isolated startup-to-menu
upgrade/diagnostics/removal check on Linux, preserving the supplied game input
and old logs. The actual Windows menu run remains the next test.

## Install and test

1. Close MW2. Extract BO1MW2-menu-test3.zip outside the game folder.
2. Run Uninstall.cmd to remove the installed earlier project DLL. If already
   removed, it reports that no d3d9.dll is present.
3. Run Install.cmd and select the folder containing iw4sp.exe. Only d3d9.dll is
   copied beside the executable; scripts and LoaderProbe.exe stay outside it.
4. Launch SP normally through Steam. Record whether the upper-left F4 badge
   appears and the main menu works.
5. Load a campaign mission, wait for player control, and press F4. Use Up/Down,
   Enter to open categories, and Backspace/Esc/F4 to return or close.
6. Observe an NPC and try movement/mouse/fire while the menu is open. Confirm
   that gameplay continues and controls return normally after closing.
7. Close the game and run Diagnose.cmd. Attach its ZIP and report launch,
   badge/menu appearance, navigation, mission name, and any crash's last action.

Test the menu first; the existing God Mode proof of concept is a later Phase 1
check once launch/render/input are stable. The full checklist remains in
[testing.md](testing.md). If launch fails, collect diagnostics before removing
this project's DLL with Uninstall.cmd; the copied logs and dumps remain.

The correction is supported by the dumps and regression test, but retail
execution of this new build is **pending**. Shader/state restoration, font
creation, keyboard behavior, Steam-overlay interaction, reset/shutdown, and
active-level gating still require actual observations. BO1 AI/asset work remains
behind the menu acceptance gate.
