# Current test13 owner checks

Install **BO1MW2-spawn-test13.zip**: close MW2, extract outside the game folder,
run **Uninstall.cmd**, then **Install.cmd** and select the directory containing
`iw4sp.exe`. Launch through Steam and **restart the mission once**. Saved FOV
is preserved. Follow [spawn-test13.md](spawn-test13.md).

Start in live estate combat with existing NPCs, Freeze AI and spawn pause OFF.
Test weapons first, turn them OFF, then one enemy/cleanup and one friendly/cleanup
before increasing population. Keep the controller rehearsal ended for AI tests.

```text
Build / map / resolution:
Give All label before input and immediately after checkpoint:
All banks reachable; Give OFF restores inventory/ammo:
G18/glock Dual: BUILD/COMPAT/footer; both models/fire/reload/OFF:
Beretta and previously working rifle: same checks:
Other previously failed gun and exact selected-row footer:
Fixed-view FPS for 10s each: OFF / ON / OFF:
Give/Dual both OFF orders; reset weapon toggles:
AI initial enemy/friendly models, alive counts and Engine actor slots:
Enemy: selected model, nearest-point distance, created entity/distance/tries:
Enemy actual spawn location and whether it advances after you move:
Remove ours: owned count0 and original campaign NPCs remain:
Friendly in estate (CONVERTED if needed): near-player position/team/combat/follow:
Friendly on another active map: same observations:
Near-wall/occupied-point spawn: retries or NO_CLEAR_POSITION; exact collision error:
Repeated spawns past8: highest owned count and Engine actor slots/result:
Pause spawning ON/OFF; existing behavior vs new requests:
Freeze AI ON/OFF; queued work resumes after OFF:
After cleanup: checkpoint/menu transition, fresh counts/header, F4 scrolling:
God/health/saved FOV/Rapid Fire/controller rehearsal regressions:
Crash or script error: exact text/module/offset, last action and current gun/model:
```

The mod's eight-NPC cap is removed. The exact native pool still has **32 active
actor slots including campaign NPCs**; a full physical pool is a separate limit.
Ordinary loaded actor models are required; friendly placement no longer depends
on a friendly mission spawn point. This service still needs an existing unfrozen
actor callback. Pending work in an empty level is useful evidence, not acceptance
of universal asset-free spawning.

Turn Give/Dual OFF and **remove debug actors before saving/reloading/quitting**.
Ownership/undo history is session-local. Close MW2 and run **Diagnose.cmd before
another launch** replaces the log. Attach its ZIP and observations above.
The new native paths have static/build/portable checks; retail behavior remains
an acceptance gate. These campaign NPCs do not implement BO1 zombie behavior.
