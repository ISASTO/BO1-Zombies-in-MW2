# Current test7 owner checks

Build `0.1.6-player-test7` for the exact supplied AMD64 MW2 executable. Test6
FOV behavior is owner-confirmed; its God OFF/protection mismatch is recorded in
[redacted evidence](runtime-test6-evidence.json). Test7 adds session God
reconciliation, every current PLAYER action, reset, and an eight-row compact
menu. No playable Zombies mode is added.

Follow [player-test7.md](player-test7.md) for the exact sequence. Extract outside
the game folder, fully close MW2, run **Uninstall.cmd**, then **Install.cmd**,
and launch Single Player through Steam normally. Saved FOV is preserved.

## Return these observations

```text
Build / campaign map / resolution:
Fresh launch, old protected checkpoint, God OFF + Noclip OFF: health drops without cycling God:
God ON protects, then OFF allows ordinary damage:
God ON checkpoint reload; God OFF checkpoint reload:
Full quit with God ON, relaunch: starts OFF and ordinary damage works:
Any GUARDED, OFF*, WAIT or LOCKED label, and when:
All eight PLAYER rows visible; text size/readability:
Infinite Ammo: reserves replenish, magazines reload; OFF stops replenishment:
No Reload: clip stable while firing; OFF consumes clip ammo:
Noclip: fly/vertical movement/pass through wall; OFF restores walking/collision:
Super Speed: faster while ON; normal/captured speed after OFF:
Super Jump: higher jump while ON; normal jump after landing and OFF:
Reset player toggles: all OFF and normal behavior returns:
Saved FOV/ADS behavior still works:
Any crash, exact error and last action:
```

Close MW2 and run **Diagnose.cmd**. Attach the resulting ZIP. Collect after the
relevant run before launching again, because `latest.log` is replaced each
launch. The collector includes settings, receipt, logs, events and probe
results. A screenshot helps if the smaller text clips or is difficult to read.

God and noclip start OFF on a fresh process and reconcile their native bits
while alive. The additional client invulnerability guard is cleared once at
a verified lifetime/OFF/reset, under the normal-movement/native-damage gate;
later scripted protection is reported as GUARDED. Noclip independently blocks
damage. Test them separately. Other new fields and the fixed-jump compatibility
edit have static/portable coverage, but require this retail run for acceptance.
No tests here claim that Windows or MW2 ran in the Linux build environment.

For rollback, close MW2 and run **Uninstall.cmd**. It removes the owned DLL and
keeps logs/settings. If a crash happens, preserve
`<MW2>\bo1mw2\logs\latest.log` immediately. Logging can fall back to
`%LOCALAPPDATA%\BO1MW2`. Return any generated crash text/minidump, or the
Application Error's faulting module/offset if no dump exists. The unhandled
exception handler may not see loader errors or faults handled by the game.

Second-map, cutscene, resolution/device-reset and Steam-overlay coverage remain
useful. F4 does not pause; WASD/mouse/fire pass through. Historical test2-6
instructions and JSON evidence remain in the repository. Complete returned
logs/dumps and original game assets are never committed or distributed.
