# Current test5 owner checks

Build `0.1.4-round-test5`, exact supplied AMD64 MW2 executable.
Phase 1 core passed in the owner's test4 estate session: correct health, God
ON/OFF and tested checkpoint persistence. This test adds a read-only BO1 round
preview using labeled script defaults. No Zombies gameplay is implemented.

Follow [round-test5.md](round-test5.md) for installation, keyboard controls,
expected numbers and the focused test sequence. Use the package's Uninstall.cmd
before Install.cmd; extract all tools outside the game folder. Launch SP through
Steam normally. Only d3d9.dll runs beside iw4sp.exe.

## Return these observations

```
Launch and mission:
F4 > ZOMBIE MODE > Round rules preview opens:
Displayed total/health for rounds 1, 7, 10 and 20:
Left/Right bounds at 1 and 100:
F4 close/reopen retains the preview round:
Back returns to ZOMBIE MODE, then the home category:
Preview text fits and values are readable:
Movement, mouse and firing continue with preview open:
Existing player health and God ON/OFF still work:
God is LOCKED after returning to the main menu:
Any crash, last action or rendering corruption:
```

Exit MW2, run Diagnose.cmd and attach the results ZIP. A screenshot is useful
if text clips. Please also provide the local BO1 common_zombie.ff candidate to
resolve the concrete missing mp/zombiemode.csv dependency. Original assets
remain private inputs, not repository/distribution content.

## Remaining robustness checks

A different campaign map from estate is useful in this iteration. Device reset,
resolution changes and Steam-overlay interoperability remain separate tests.
Opening F4 does not pause the game. Navigation keys are consumed while it is
open; releases reach the game, and WASD/mouse/fire pass through. The preview
has no engine writes. God still flips only its native entity bit on an explicit
selection, and its actual current state is read after reloads. Observed
checkpoint persistence is not promised for every mission or scripted failure.

## Diagnostics after a fault

Copy <MW2>\bo1mw2\logs\latest.log before launching again; each launch replaces
it. Logging can fall back to %LOCALAPPDATA%\BO1MW2. Diagnose.cmd collects logs,
receipt, available crash files, Windows events and loader-probe results.
Describe the last action and whether the badge/menu appeared before the fault.
If needed, close the game and run Uninstall.cmd to remove our receipt-owned DLL.

The exception handler attempts a text crash report and normal minidump for
unhandled faults reaching it. Earlier loader errors or other game/Steam handlers
can prevent capture. If no dump exists, return the last log and Application
Error's faulting module/offset. Full returned logs/dumps are not committed.

Historical failure/correction evidence and original test4 steps remain in
[player-test4.md](player-test4.md), [menu-test3.md](menu-test3.md) and the
versioned runtime/build evidence files. Those files describe their own build.
