# Current test10 owner checks

Install **BO1MW2-weapons-test10.zip**: close MW2, extract outside the game folder,
run **Uninstall.cmd**, then **Install.cmd**, and select the folder containing
`iw4sp.exe`. Launch through Steam and **restart the mission once** for a clean
initial inventory. Saved FOV is preserved. Read [weapons-test10.md](weapons-test10.md)
for compatibility details and the focused sequence.

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

The static/build/portable checks pass; retail animation behavior and FPS await
this run. After acceptance, move to the BO1 round/spawn controller and IW4 actor
compatibility work. AI spawning and playable Zombies are not enabled yet.
