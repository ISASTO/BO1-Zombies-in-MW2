# Current test9 owner checks

Build **0.1.8-weapons-test9** for the exact supplied AMD64 executable.
Test8's matching log identified Rapid Fire's ambiguous catalog signature;
the owner reported invisible/empty Give All weapons. See
[redacted evidence](runtime-test8-evidence.json). Test9 replaces that global
bypass with filtered, reversible inventory banks, adds reversible Force Dual
Wield, and corrects the catalog signature and overlap-aware verifier.

Fully close MW2, extract **BO1MW2-weapons-test9.zip** outside the game folder,
run **Uninstall.cmd**, then **Install.cmd**, and launch Single Player through
Steam. Enter a mission, press F4, and open WEAPONS. Saved FOV is preserved.
Follow [weapons-test9.md](weapons-test9.md) for the exact scopes and sequence.

```text
Build / map / resolution:
Original weapon names and ammo before Give All:
Give All ON: visible models, ammo, firing and reloads; failing weapon names:
Give All Left/Right: banks shown and cycling behavior:
Give All OFF: original inventory, held weapon and ammo restored:
Dual Wield ON: held weapon name, ON/ON*, both models/hands/firing/reload:
Dual Wield OFF: original single/natural-dual state and left ammo:
Give ON -> Dual ON -> Dual OFF -> Give OFF:
Dual ON -> Give ON -> Give OFF -> Dual OFF:
Rapid Fire: unlock, firing difference, before/ON/OFF logged timing:
Rapid ON: switching/back and bank switching; OFF restores timing:
Checkpoint and mission exit/re-entry results:
Weapon reset clears Give, Dual, Rapid and linked No Reload:
God/health/F4 navigation/saved FOV still work:
Any LOCKED, WAIT, UNDO, ON*, NATIVE or AT MIN label and when:
Any invisible weapon, clipping, crash, exact error and last action:
```

Keep Infinite Ammo and No Reload OFF for the initial ammo/undo tests. Turn Give
All and Dual Wield OFF before saving/exiting: their captured undo snapshots are
in-process. Changed native inventory or map/asset identity blocks stale restores;
record that result. Cross-process save recovery is not implemented in test9.

Close MW2 and run **Diagnose.cmd** before another launch replaces the log.
Attach its ZIP, which includes receipt, settings, logs, events and probes.
Use the **Weapon info to log** row before and after toggles. A screenshot helps
with model/menu issues. Return any new crash text/dump or faulting module/offset.

Eight C++ suites, 12 Python fixtures, 51 overlap-aware exact patterns, the AMD64
build and isolated actual-test8-to-test9 PowerShell upgrade pass here. The retail
game and Windows probes cannot run in this Linux workspace. New gameplay and
model/animation behavior await your run. AI spawning and playable Zombies remain
separate gates; complete returned logs/dumps and original assets remain private.
