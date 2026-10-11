# Test 2: isolate the launch failure

**Completed: the owner reports successful launch both without our DLL and with
this control.** Both uploaded bundles have passing factory probes. Three old
game dumps identify a native private-vtable call beyond test 1's copied table;
continue with [menu test 3](menu-test3.md). The following is the archived test 2
procedure and the evidence available when it was created.

The owner reports that menu test 1 prevents a successful game launch. No log,
Windows error, or failure stage has been supplied yet. The cause is **unknown**;
the F4 milestone has not passed. This is a diagnostic control, not a confirmed
fix or another menu build.

## Exact sequence

1. Close MW2. Extract **BO1MW2-startup-test2.zip** outside the game folder.
2. Run **Diagnose.cmd** and paste the installed MW2 folder containing iw4sp.exe.
   Do this before removing test 1. Keep the resulting startup-results ZIP.
3. Run **Uninstall.cmd** from this package, using the same game folder. It accepts
   test 1's installation receipt and removes only the receipt-matching project
   DLL. Logs remain. If you manually installed test 1 without a receipt, move
   only that project's d3d9.dll out of the game folder instead.
4. Launch Single Player through Steam with our DLL removed. Record whether it
   reaches the main menu and loads a campaign mission. Close the game.
5. If that normal launch works, run **InstallStartup.cmd**, using the same game
   folder. It checks the exact executable hash and refuses an existing wrapper.
6. Launch Single Player through Steam again; try reaching the main menu and
   loading the same campaign mission. **There is no F4 menu or status badge in
   this diagnostic build.** Normal gameplay is the observation we need here.
7. Close the game, run **Diagnose.cmd** again, and attach both result ZIPs.
   Run Uninstall.cmd afterward to return to the normal game while we review.

If step 4 also fails, stop before installing the startup control. Return the
first diagnostic ZIP and the exact error for the unmodified launch. This result
would not establish that our DLL alone caused the failure.

## What the collector does

- Preserves the old game/fallback logs and installation receipt before probing.
- Records executable/DLL hashes, OS/GPU metadata, and available MW2-related
  Windows Application Error, Hang, WER, and SideBySide events from the last day.
- Runs a separate AMD64 LoaderProbe.exe process against Windows System32 D3D9,
  a receipt-identified installed project DLL, and the packaged startup control.
- Each probe tests DLL loading, the ten exports, Direct3DCreate9, adapter count,
  and factory Release. It never loads iw4sp.exe or creates a game device.
- Stops only its own probe process after a 15-second timeout. It writes a ZIP
  beside the scripts even when no game log was created.

An old-DLL probe deliberately uses LoaderProbe.exe as the host. It exercises the
old DLL's dependency/static-runtime and forwarding paths; it does **not** run
the iw4sp-only engine scans or install its hooks. A passing probe is not proof
that the game, device hooks, menu, or God Mode works.

## Startup control architecture

`0.1.1-startup-test2` is a separate freestanding C target. Its built DLL imports
only KERNEL32.dll. It has no CRT entry point, C++ constructors, TLS directory,
engine scanner, COM vtable replacement, window subclass, renderer, or game-data
writes. DllMain records its module handle and disables thread notifications.
The first exported call performs initialization using Win32 InitOnce, then loads
the real D3D9 by an absolute System32 path and forwards calls/objects unchanged.
It rejects a returned self handle to prevent accidental forwarding recursion.
No background injection, executable replacement, or protection bypass is used.

The control logs each process under:

```
%LOCALAPPDATA%\BO1MW2\startup\startup-<PID in hex>.log
```

If LOCALAPPDATA is absent, it uses `%TEMP%\BO1MW2\startup`. The collector includes
both. Logs begin at the first export, not at DllMain. An absent log can mean the
export was never reached, a loader failure, or inability to create the log;
it does not by itself identify the cause.

The script and binary are only needed for this diagnostic test. Keep
LoaderProbe.exe, scripts, and package metadata outside the game directory.
The installer copies only d3d9.dll beside iw4sp.exe, plus its receipt/log folder.

## Report this

```
Launch after uninstalling test 1: main menu / mission works / fails
Launch with startup control: main menu / mission works / fails / not attempted
Failure appearance: no window / window then closes / hangs / error dialog
Exact error text (if any):
Does Steam return to the Play button? How quickly?
```

The two startup-results ZIPs supply the technical evidence. If neither includes
an event or crash, the last completed stage in a probe/control log still narrows
the investigation. Game-side execution of test 2 remains pending until this
feedback arrives.

## Build and verification

The normal CMake Windows build also produces `startup/d3d9.dll` and
`LoaderProbe.exe`. MinGW links both with explicit custom entry points and
`-nostdlib`; the PE verifier requires AMD64, ASLR/NX, zero TLS, exactly ten DLL
exports, and the exact small set of system imports:

```
python3 tools/verify_startup_build.py build-win64/startup/d3d9.dll \
  build-win64/LoaderProbe.exe --output docs/startup-build-verification.json
```

The compiler and PE checks do not substitute for a Windows loader test.
MSVC target definitions are supplied, but only the MinGW cross-build is verified
in this environment. Test 1's DLL is preserved separately for reproducibility.
All four PowerShell scripts parsed successfully under PowerShell 7.6.6 on Linux.
An isolated filesystem check verified collection when no old logs exist, capture
of failed probe launches, startup installation/hash/receipt, and uninstall with
game-input and log preservation. Windows event retrieval, CMD wrappers, Windows
DLL execution, and the retail game still require the owner's test.
