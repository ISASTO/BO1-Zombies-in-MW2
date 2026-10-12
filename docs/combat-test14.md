# Native combat and lifecycle, test14

Build **0.1.13-combat-test14** for the exact AMD64 executable used by test13.
New native calls/hooks are checked against that executable and portable fixtures.
This environment cannot run the Windows game. Combat and crash outcomes remain
unverified until the owner returns a retail run.

## Install and start

Close MW2. Extract the full ZIP outside its game directory. Run Uninstall.cmd,
then Install.cmd, selecting the folder containing iw4sp.exe. Launch through Steam
and **restart the mission once** instead of loading NPCs saved with test13.
Use a live Contingency or Estate encounter. Press F4; arrows select/adjust, Enter
acts, Backspace/Esc returns, and F4 closes. Every page now displays all rows.

Infinite Ammo and No Reload are in WEAPONS. Infinite Grenades shares No Reload.
Reset Weapons includes both ammo options; Reset Player affects movement and
protection. G18 dual-wield rendering is deferred.

## Check the AI first

1. Spawn one enemy. It should use the nearest eligible human mission point,
   offset away from occupied bodies, approach a living opponent and fight.
2. Spawn a **Soldier** friendly. It should appear near you, remain on your team,
   target local enemies and follow when you move away. Move at normal speed for
   this first check. God can be ON while observing combat.
3. Remove our spawned actors. Check both the NPCs and the ownership count.
   Repeat after killing a spawned NPC so a corpse is included in removal.
4. Try Ghost or Price if the selector says the character is loaded. Appearance
   includes the body, head and equipment attachment slots from that real NPC;
   its mission VM state is not cloned. This path needs visual testing.
5. In WORLD, use Kill All Enemies. It applies the native damage routine to
   hostile actor entities, excluding client-bearing, allied and protected ones.
   It does not stop future campaign spawning.
6. With debug NPCs present, enable Freeze AI, then remove them. Cleanup now runs
   on the server clock and can execute while actor thinking is frozen.
7. With God OFF, die with spawned NPCs present. Reload a checkpoint manually in
   a separate attempt. Normal checkpoint reads/writes and detected death remove this build's NPCs.
   A nested checkpoint during a native NPC call defers cleanup until the next
   server frame so an NPC is not freed while the engine is using it. NPC
   persistence across saves is not a supported feature.
8. Increase population, then trigger the next campaign enemy wave. If the native
   actor budget is full, campaign initialization can remove an owned mod NPC to
   make room. Record any crash, error message or unexpected removal.

The last three steps need particular attention: test13 had reproducible native
animation crashes, and this pass changes the source/lifetime paths that could
leave stale NPC VM or spawner references. Passing static tests is not proof that
all campaign script interactions are resolved.

## Current limits

**The physical 32-actor ceiling remains.** Removing it is still requested work,
not a completed feature. The audit found fixed actor storage, snapshot arrays,
actor-index scratch data and save/load consumers, plus 50-entry perception data.
See [actor-capacity-audit.md](actor-capacity-audit.md). The mod refuses its own
overflow request and adds native campaign slot reclamation; neither expands
simultaneous capacity.

Character choices are consistent across missions, but Ghost, Price, Soap and
Roach are available only when that character is present with loaded assets.
Soldier falls back to a real human spawner of either side, with ally conversion.
Dogs and live actor VM state are excluded. A mission with no eligible human
spawner reports NO_SPAWNER; universal human/character asset loading is unfinished.

These are normal MW2 NPCs. BO1 behavior, sounds, animations and live round adapter
remain separate from the accepted simulated controller rehearsal.

## Return the result

Close the game and run Diagnose.cmd before launching again. Attach its ZIP and
briefly report: map/encounter, selected character, enemy targeting, friendly
fighting/following, removal including corpses, kill-all result, and the last action
before any death/reload/save/capacity failure. Important log records are
AI_SERVICE, AI_COMBAT, AI_MODEL, AI_DELETE_BEGIN, AI_CLEANUP and AI_KILL_ALL.

The service first publishes the clock CALL redirect, then installs the lifecycle
and actor-init hooks from the next server-clock callback. Logs should show
clock_installed=1 followed by installed=1 and scheduler=SERVER_CLOCK before a
native spawn is accepted.
