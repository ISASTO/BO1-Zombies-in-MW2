# Current test12 owner checks

Install **BO1MW2-ai-test12.zip**: close MW2, extract outside the game folder,
run **Uninstall.cmd**, then **Install.cmd**, and select the folder containing
`iw4sp.exe`. Launch through Steam and **restart the mission once** for a clean
initial inventory. Saved FOV is preserved. Follow [ai-test12.md](ai-test12.md).

```text
Build / map / resolution:
Give All label before first toggle or arrow:
Give ON -> bank2 -> checkpoint -> label BEFORE any arrow:
All four banks reachable; Give OFF inventory/ammo result:
Beretta Dual ON/OFF: status, both models/fire inputs/reload/ammo:
G18/glock Dual ON/OFF: same checks:
Any WAIT: exact gun and selected-row footer reason:
Fixed-view FPS for 10s each: Dual OFF / ON / OFF:
Give/Dual both OFF orders and checkpoint behavior:
AI page: initial alive counts and enemy/friendly template counts:
Selected enemy classname / distance / approximate location:
One enemy spawn result / new NPC behavior / owned and alive counts:
Remove our spawned actors result; original campaign NPCs unaffected:
Selected friendly classname and same spawn/remove observations:
Pause AI spawning ON/OFF: new-spawn gate vs existing NPC behavior:
Linked Freeze AI ON/OFF:
F4 close/reopen and scrolling to final diagnostics row:
After removing debug NPCs: checkpoint/menu transition and stale queue/counts:
God/health/saved FOV/Rapid Fire/controller rehearsal regressions:
Crash or script error: exact text/module/offset and last action:
```

For AI, start with weapon modes, rehearsal, Freeze AI and spawn pause OFF.
Use a combat section with native NPCs; select an existing mission spawn point
and look away if normal visibility checks refuse. A NONE/LOCKED/refused result
is useful evidence: record it and the template/location, rather than assuming
the engine has a universal spawnable actor for every map.

Turn Give/Dual OFF and **remove debug actors before saving/reloading/quitting**.
Ownership/undo history is in-process and guarded by detected level lifetime.
Close MW2 and run **Diagnose.cmd** before another launch replaces its log; attach
the ZIP. Static/build/portable checks pass; new dual behavior and native AI
execution need this retail test. Native mission NPCs are not BO1 zombies.
