# Test33 install and retest

Build: **0.1.32-squad-test33**. Goal: stable to 256 actors on Estate and
Contingency, including checkpoint reloads.

## Install

1. Close MW2. Extract the complete Test33 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test33 folder (removes Test32).
3. Run **Install.cmd** from the Test33 folder.

## Run A: Estate to 256

Fresh Estate through Mission Select. Soldier, World Speed 1.0x, Freeze AI OFF,
Invincible Spawned Friendlies ON, explosives/rapid fire OFF.

Spawn to 96, 128, 160 (the old wall), 192, 224 and 256. Hold about 30 seconds
at each and select **AI info to log** at each stage.

## Run B: Contingency to 256

Same settings on fresh Contingency. Same stages. This map uses far more
animation per actor, so it is the real test of the new animation pool.

## Run C: checkpoint reload with a crowd

At a stable high count on either map, cause a checkpoint reload (friendly
fire, or die). It should reload cleanly. Repeat once more at the highest
count.

## Send back

Run **CollectLogs.cmd** after any crash, or after finishing the runs. Useful
lines in the log:

- `AI_ANIM_INFO used=… peak=…` — animation pool use (capacity 16,383).
- `AI_COMPASS rejected_actor_slots=…` — actors beyond the 32 compass slots
  (expected to climb; it is the fix working).
- `AI_STATIC_WATCH_HIT` — should not appear at all now.
- `AI_GLASS_STATE missing_place_shatters` — should stay at 0 now.

Note: with more than 32 actors only the first 32 appear on the minimap.
