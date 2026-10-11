# Current test6 owner checks

Build `0.1.5-fov-test6`, exact supplied AMD64 MW2 executable. Phase 1 health/God
passed in the owner's test4 estate session. Test5 preview navigation passed in
the front end; that diagnostic run contains no active mission. Test6 adds the
requested FOV selector and saved preference. No playable Zombies mode is added.

Follow [fov-test6.md](fov-test6.md) for installation, controls and expected values.
Extract all tools outside the game folder, run Uninstall.cmd then Install.cmd,
and launch SP through Steam normally. Only d3d9.dll runs beside iw4sp.exe.

## Return these observations

```
Launch and campaign mission:
F4 > PLAYER > FOV settings opens:
Base 65 to 90 changes the view; Live base FOV shows 90:
Save acknowledgement and Saved base FOV = 90:
Full exit/relaunch restores 90 without reselecting it:
ADS and scoped-weapon zoom still work; any jitter:
Checkpoint reload restores/keeps the selected FOV:
Health and God ON/OFF still work:
Restore default clears Saved and returns Live to 65:
Any clipping, input problem, crash or last action:
```

Exit MW2, run Diagnose.cmd and attach its results ZIP. It now includes the mod's
settings.ini if present, along with the existing logs/receipt/events/probes.
A screenshot helps if text clips. Latest.log is replaced each launch; collect
it after the restart/checkpoint/reset checks, before launching again.

## Remaining robustness checks

A different campaign map from estate is useful. Device reset, resolution changes,
cutscene camera overrides and Steam-overlay interoperability remain separate
coverage. F4 does not pause gameplay. Navigation keys are consumed while open;
releases reach the game, and WASD/mouse/fire pass through.

The preview has no engine writes. God flips only its native entity bit on an
explicit selection and reads the actual flag after reloads. Test4 checkpoint
persistence is not a universal mission guarantee. FOV writes require a verified
named float dvar and living player; selections made at the main menu are queued.
The stored value is base FOV, not the final ADS/aspect-scaled camera angle.

## Diagnostics after a fault

Copy `<MW2>\bo1mw2\logs\latest.log` before launching again. Logging can fall back
to `%LOCALAPPDATA%\BO1MW2`; saved FOV uses only the game folder's bo1mw2 directory.
Describe the last action and whether the badge/menu appeared before the fault.
Close MW2 and use Uninstall.cmd if rollback is needed. It keeps logs/settings.

The exception handler attempts a text crash report and minidump for unhandled
faults reaching it. Earlier loader errors or game handlers can prevent capture.
If no dump exists, return the last log and Application Error's faulting module
and offset. Full returned logs/dumps and original assets are not committed.

Historical steps/evidence remain in the versioned test2-5 documents and JSON.
