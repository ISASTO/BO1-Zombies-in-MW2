# Combined weapon repairs and BO1 controller rehearsal: test11

Build `0.1.10-zombies-controller-test11` contains every test10 weapon repair and
a bounded, in-game BO1 normal-solo round controller. The owner authorized this
next implementation while unable to test test10. Neither iteration has new
retail acceptance yet. **This is a controller rehearsal, not playable Zombies.**

The simulated adapter creates up to 24 tracked tickets in mod-owned memory.
It never spawns or modifies a campaign actor, changes mission scripts, applies
damage, imports an asset, or takes over campaign objectives. Each UI/HUD/log
labels simulation. The existing PLAYER/WEAPONS/WORLD controls keep their scopes.

## New behavior and source boundaries

- Recovered BO1 normal-solo count/health/pacing, first intro 8.25s, later intro
  2.5s, 10s between rounds, 100ms capacity polling and 1s completion polling.
- Separate unspawned queue, live tickets and completed obligations. A creation
  request reserves capacity; only a successful acknowledgement spends budget.
  Failed creation still incurs the spawn delay. Stale/duplicate callbacks fail.
- Explicit recycling returns a live obligation to the queue. The automatic
  30s movement/barrier failsafe is researched but needs an actor adapter.
- Start/End, starting round 1–100, simulated one/all kills, an optional debug
  auto-clear at one virtual actor per game second, spawn gate, scrolling status
  and event logs. Auto-clear starts OFF and has no BO1 gameplay counterpart.
  Starting above round 1 is a debug shortcut to an already progressed normal
  round (first_round=false), not a claim about BO1 map restart overrides.
- Four coordinate-free virtual spawner IDs exercise the source's one retry
  when the previous spawner repeats. They are not real map spawn locations.
- A portable ordinary-zombie/player melee admission rule is implemented and
  tested in source. It is disconnected from the DLL's actor/damage paths.
- A lexical local-script dependency/asset-reference auditor emits hashes,
  function line ranges and references without original script bodies.

The existing confirmed IW4 `client+0x0` commandTime drives the rehearsal. A
stalled game clock stalls the controller; rendering wall time is not used.
Pause/Slow Motion behavior must be observed in retail. Loading/death/invalid
reads, changed map/client or a clock rewind stop the rehearsal when detected.
It does not resume across checkpoint reloads. No native BO1/IW4 structure,
script thread, or actor function is assumed compatible. Millisecond rounding
and the render callback are compatibility scheduling; exact BO1 VM/network
frame quantization is not claimed. Slow rendering emits one request at a time,
never a backlog burst. Round 100 terminates the bounded test.

Special rounds, mixed dogs, map callback overrides, intro voice/chalk rendering,
perks, points, real pursuit, attacks, pain/death and asset loading remain pending.
The research and A/B/C division are in [bo1-portability.md](bo1-portability.md)
and [bo1-controller-evidence.json](bo1-controller-evidence.json).

## Install the one combined package

1. Close MW2. Extract **BO1MW2-zombies-controller-test11.zip** outside the game
   folder. Run its **Uninstall.cmd**, then **Install.cmd**, selecting the folder
   containing `iw4sp.exe`. Install only this newest package; test10 is included.
2. Launch through Steam and **restart the mission once** before the first Give
   All test. An old modified checkpoint cannot supply a trustworthy original
   inventory. Saved FOV stays intact; undo history is in-process.
3. Press **F4**. Up/Down selects or scrolls, Enter acts, Left/Right adjusts a
   bank/value/starting round, Backspace/Esc returns, F4 closes.

## Tomorrow's consolidated test

First verify ordinary gameplay, F4 navigation, health/God ON/OFF and saved FOV.
Keep ammo/reload and other weapon overrides OFF for the initial weapon checks.

1. **Give All:** ON, select bank 2, reload checkpoint, cycle all banks with
   Left/Right, then OFF. Check that added guns disappear and original loadout/
   ammo return. Repeat an ON/OFF cycle. If practical, test a regular pickup.
2. **Dual:** with G18/glock and one other gun, ON/OFF; check models, both triggers,
   reload/ammo and original state. Compare FPS OFF/ON/OFF for ten seconds each
   at the same camera position. `COMPAT` is a generic normal-animation fallback;
   authentic paired assets and FPS still need this test.
3. Check both OFF orders: Give ON → Dual ON → Dual OFF → Give OFF, then Dual ON
   → Give ON → Give OFF → Dual OFF. Detailed weapon scope remains in
   [weapons-test10.md](weapons-test10.md).
4. **ZOMBIE MODE → Controller rehearsal:** start at round 1 with Auto-clear OFF.
   Intro should last about 8.25 game seconds, then six virtual actors should
   appear in the counters about two game seconds apart. No real zombie appears.
   Clear simulated alive, leave the queue to finish, then clear any later ones.
   After the final kill, completion polling can add up to one game second;
   expect BETWEEN for 10s, round 2 INTRO for 2.5s, then eight virtual actors.
5. End rehearsal, set Starting round to **10**, restart, leave Auto-clear OFF.
   The live count should reach **24/24**, with **9 queued** and health **1045**.
   It must stop requesting at the cap. Simulate one kill; a replacement should
   occupy the free slot on the next eligible poll. No soldier should change.
6. Pause spawn queue: live remains, queue does not drain. Resume it. Enable
   Auto-clear and watch progression, close F4 and check the rehearsal HUD, then
   reopen. Navigate down to Phase/timer, health and log; the page should scroll.
7. Observe the controller through game pause and WORLD Slow Motion; report
   whether command-clock timers stall/slow. Freeze AI should retain its native
   actor-thinking scope, not be confused with the rehearsal spawn gate.
8. While rehearsal runs, reload checkpoint and return/re-enter a mission. It
   should stop when the lifetime guard detects the transition/rewind. Start is
   LOCKED without an active valid player. Rehearsal End must clear its virtual
   population and leave PLAYER/WEAPONS/WORLD selections alone.

Turn Give All and Dual OFF before saving/quitting. Close MW2 and run
**Diagnose.cmd** before another launch replaces `latest.log`. Return its ZIP and
the observations in [testing.md](testing.md). There is no need to upload assets
to test the rehearsal. This build's native game/animation/FPS behavior has not
been executed in the Linux build environment.
