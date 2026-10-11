# Current combined test11 owner checks

Install **BO1MW2-zombies-controller-test11.zip**: close MW2, extract outside the game folder,
run **Uninstall.cmd**, then **Install.cmd**, and select the folder containing
`iw4sp.exe`. Launch through Steam and **restart the mission once** for a clean
initial inventory. Saved FOV is preserved. Read
[zombies-controller-test11.md](zombies-controller-test11.md) for the full sequence;
it includes test10's weapon repairs. Install only the combined package.

```text
Build / map / resolution:
Original weapons/ammo before enabling Give:
Give ON -> bank2 -> checkpoint reload -> Left/Right bank labels/count:
Give OFF: which added guns remain, if any; original held gun and ammo:
ON/OFF repeated, then checkpoint reload with Give OFF:
Any ordinary pickup preserved while known grants are removed:
G18/glock Dual ON: status, both models, both triggers, reload, ammo:
G18/glock Dual OFF: original single/natural-dual state:
Another gun's Dual ON/OFF result and exact name:
Fixed-camera FPS: Dual OFF / ON / OFF, ten seconds each:
Give ON -> Dual ON -> Dual OFF -> Give OFF:
Dual ON -> Give ON -> Give OFF -> Dual OFF:
Checkpoint with both ON and subsequent OFF:
Reset weapons; God/health/FOV/Rapid Fire still work:
Any RESTART, WAIT, COMPAT, ON*, UNDO or LOCKED label and when:
Rehearsal round1: intro duration, six virtual actors, 2s pacing:
Clear virtual alive: remaining queue finishes, 10s BETWEEN, 2.5s INTRO, round2 eight:
Rehearsal round10: reaches 24 alive / 9 queue / health 1045; replacement after one kill:
Pause spawn queue / resume / Auto-clear behavior:
Controller page scroll, timers, close/reopen F4 and rehearsal HUD:
Campaign actors stay normal during rehearsal; no real zombie appears:
Game pause and Slow Motion effects on controller timers:
Checkpoint / death / menu / map transition: stopped reason and any automatic restart:
Start locked in front end; End clears virtual counts and preserves existing toggles:
Any crash, exact error/module/offset, and last action:
```

Keep Infinite Ammo/No Reload OFF for the first ammo tests; keep Rapid Fire and
Explosive Bullets OFF for the isolated FPS comparison. `COMPAT` means the gun
uses its normal animation data in both native hand slots. It is a generic
fallback, not an imported authentic akimbo animation set.

Close MW2 and run **Diagnose.cmd** before another launch replaces the log.
Attach its ZIP. Undo history is retained during this process across checkpoints
and banks; turn the modes OFF before saving/quitting. A previous process's
already modified checkpoint requires an unmodified mission restart for baseline
capture. No original game assets, scripts or complete returned logs/dumps are
committed or distributed.

The static/build/portable checks pass; retail weapon/animation/FPS and controller
integration await this run. The rehearsal uses simulated tickets in mod memory,
not campaign soldiers. Normal-solo scheduling is source-derived; debug auto-clear
is a test adapter. Native AI spawning, damage and playable Zombies remain locked.
After evidence-driven fixes, establish an IW4 engine-thread actor adapter.
