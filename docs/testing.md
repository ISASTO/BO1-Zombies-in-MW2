# Current test8 owner checks

Build `0.1.7-world-weapons-test8` for the exact supplied AMD64 MW2 executable.
The owner accepts test7's expanded PLAYER behavior. Its matching diagnostics
confirm valid fields, God OFF/death/recovery, compact rendering/navigation and
saved FOV, but contain no individual cheat toggle actions. See
[redacted evidence](runtime-test7-evidence.json).

Test8 adds adjustable movement and bounded WEAPONS/WORLD controls. Follow
[world-weapons-test8.md](world-weapons-test8.md) for installation, exact scopes
and the sequence. Fully close MW2, extract outside the game folder, run
**Uninstall.cmd**, then **Install.cmd**, and launch Single Player through Steam.
Saved FOV is preserved. No playable Zombies mode is added.

## Return these observations

```text
Build / campaign map / resolution:
God OFF + Noclip OFF: ordinary damage still works:
Speed: Left/Right while OFF; two strengths while ON; OFF returns base speed:
Jump: Left/Right while OFF; two heights while ON; land then OFF returns normal:
Give All loaded: available weapons cycle; OFF returns ordinary access:
Infinite Grenades: frag/flash counts stable; linked No Reload row/gun clips; OFF consumes:
Rapid Fire: weapon names/timings from info log; faster; switch/back/OFF restores:
Semi-auto still needs presses; reload/equip behavior:
Explosive Bullets: player and AI effects; OFF returns ordinary bullets:
Slow Motion: 0.5x/0.25x; OFF returns captured speed:
Freeze AI: friendly/enemy thinking; OFF resumes; any animation/scripts continuing:
Gravity: 400/1200 with Super Jump OFF; subsequent jumps after OFF normal:
Weapon reset clears linked No Reload; world reset restores its controls:
Checkpoint and mission exit/re-entry while one control enabled:
Full quit with controls ON, relaunch: menu OFF and ordinary gameplay:
Saved FOV still persists:
Any LOCKED, WAIT, NATIVE, AT MIN or GUARDED label, and when:
Any clipping, crash, exact error and last action:
```

Test new controls separately. For the linked grenade test, keep Infinite Ammo
OFF. Use God ON only for the initial isolated explosive-bullet test, and turn
Noclip OFF when assessing damage. Kill All Enemies is a PLANNED row with no
handler. Give All only exposes the current map's loaded catalog. Native global
explosive bullets, shared Rapid Fire weapon timing and the actor-think gate can
affect AI/friendlies as described in the focused test document.

Close MW2 and run **Diagnose.cmd** before another launch replaces `latest.log`.
Attach the resulting ZIP. The collector includes settings, receipt, logs,
events and probe results. Record before/after weapon info with its menu action.
A screenshot helps if text clips. Stop at an unexpected result and preserve
`<MW2>\bo1mw2\logs\latest.log`; fallback is `%LOCALAPPDATA%\BO1MW2`.

For rollback, close MW2 and run **Uninstall.cmd**. It removes the owned DLL,
keeps logs/settings and leaves original game files intact. Return any generated
crash text/minidump, or Application Error faulting module/offset if no dump
exists. The unhandled exception handler may not see loader errors or faults
handled by the game.

The Windows build, portable ownership/adjustment tests, exact-file verifier and
isolated PowerShell upgrade checks passed here. No retail game or Windows
probe is executed in this Linux environment. New test8 gameplay requires the
owner run. Second-map, cutscene, resolution/device-reset and Steam-overlay
coverage remain useful. F4 does not pause; WASD/mouse/fire pass through.
Historical test2–7 instructions and evidence remain available. Complete user
logs/dumps and original assets are never committed or distributed.
