# Test17: friendly fire, native autosaves and capacity attempt

Version: `0.1.16-squad-test17`.

## What changed

WORLD now has **Allow Friendly Fire**. ON sets the native friendly aim-block
distance to zero and client-to-client damage scale to one. OFF and Reset world
restore owned native values. This permits firing at ordinary allies; campaign
hero invulnerability and mission-script consequences still follow native rules.

Autosaves no longer delete/rebuild spawned actors. Native actors and their
native helper spawners remain alive and are serialized through the existing
checkpoint format. Ownership markers use the registered targetname field,
which the native serializer writes as text. After native load, a marked living
actor is adopted only with a matching marked human helper, team, valid native
self links and current allocation identity. Living actors take priority over
copied corpse tags, and one helper cannot belong to two ownership tickets.
Marked corpses retain cleanup tickets. Model attachment references are reacquired
from loaded native fields rather than reusing old string IDs.
An independent checkpoint generation cancels active spawn/damage/model work and
rollback tickets at a nested load. Deferred cleanup discards old tracking data
without deleting restored entities that reused the old address/allocation number.

The DLL installs the lifecycle hook at the first ready level so a saved tagged
squad can be recognised after a process restart. Idle work returns immediately
once the one-time ownership scan has completed. The Test16 scan optimization
and four-actor follow batch remain intact.

Campaign population can still fill the **32-slot native pool**. A living friendly
reclaimed for a campaign spawn now tries its previous collision-checked position
within 2048 units; unavailable/distant positions fall back near the player.
This reduces clustering but does not remove capacity-related disappearances.

## 256-slot attempt

The isolated native-code prototype passes allocation/iteration through index
255, refuses the 257th allocation and preserves pool guards. It also reproduces
48 bytes of actor-tail corruption at sentient index 50. Shared byte snapshot
indices and fixed checkpoint bounds remain additional blockers. **The DLL does
not activate 256 actors.** See [capacity audit](actor-capacity-audit.md).

## Owner checks

Close MW2, extract `BO1MW2-squad-test17.zip`, run Uninstall then Install, and
restart the mission once. The installer copies a local `players` folder into
`bo1mw2/backups/before-test17-*` if that folder exists. Saves in external Steam or
other folders are outside that automatic backup. Saved FOV remains preserved.

1. Spawn one Soldier and an enemy. Check ally support, enemy engagement, and
   normal frame pacing. Then try a loaded Ghost appearance.
2. With living friendlies spread out, trigger an autosave. They should retain
   their entities, positions and animation without disappearing. Logs should
   say `AI_CHECKPOINT_WRITE ... mode=NATIVE_SAVE`, not checkpoint-write deletes.
3. Add another friendly after the save, then die/reload. The saved population
   should restore; the later addition should be absent. Check ally support and
   Remove our spawned actors. Original campaign actors must remain.
4. Save a small squad, quit MW2 completely, relaunch and load that checkpoint.
   Check ownership count, support behavior and removal again. Look for
   `AI_CHECKPOINT_ADOPT` and `AI_CHECKPOINT_OWNERSHIP` in the log.
5. Toggle WORLD > Allow Friendly Fire ON, aim at a spawned Soldier and fire.
   Compare OFF, then Reset world. Confirm the toggle does not change enemy
   targeting or the separate God/no-recoil options.
6. Increase population gradually, advance through campaign stages and check
   slot-recovery placement. The Engine actor slots row still reports `/ 32`.
   Do not interpret the isolated 256-slot tooling as game capacity.
7. Remove spawned actors, check death/reload and mission transition, then close
   the game and run Diagnose before the next launch replaces `latest.log`.

Report the first failing step, exact error, map and largest Engine actor slots
count, and return the diagnostic ZIP. Native saves, restored NPC behavior and
friendly fire are experimental until this Windows run verifies them.
