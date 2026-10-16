# Test36 install and retest

Build: **0.1.35-squad-test36**. Goal: stable to 256 actors on Estate and
Contingency, including checkpoint reloads, with no engine error dialogs.

## Install

1. Close MW2. Extract the complete Test36 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test36 folder (removes Test35).
3. Run **Install.cmd** from the Test36 folder.

Start each mission fresh from Mission Select, not Resume Game: checkpoints
saved by Test35 are damaged.

## Run A: Estate to 256

Soldier, World Speed 1.0x, Freeze AI OFF, Invincible Spawned Friendlies ON,
explosives/rapid fire OFF. Spawn to 128, 160 (the Test34 wall), 192, 224 and
256. Hold about 30 seconds at each, let them fight, and select **AI info to
log** at each stage.

## Run B: Contingency to 256

Same settings and stages (200 was the Test34 wall).

## Run C: checkpoint saves and reloads (most important this time)

1. With nobody spawned, play until a checkpoint saves, then die or
   friendly-fire to reload it. Gameplay should resume normally.
2. Spawn 100+ actors, let a checkpoint save, keep playing a minute, then
   reload. Repeat at the highest stable count.

## What to expect

- If spawning pauses before 256, that is the guard (`AI_RESOURCE_GUARD
  refused=…` in the log). Spawning resumes when the fighting settles.
- If a big fight pushes a pool to its edge, the mod removes some of its own
  actors (`AI_RESOURCE_SHED`) instead of letting the game fail. You may see
  the count drop a little.

## Send back

Run **CollectLogs.cmd** once at the end. Previous runs' logs are now kept
(`session-*.log`), so one collection covers both maps. Write down any
dialog text exactly. Useful lines:

- `SCRIPT_VM enabled=1 … parents_capacity=65526` near the top.
- `SCRIPT_VM_USAGE children=… parents=…/65526`
- `ENGINE_LIMITS entities=…/2046 …`
- `AI_SPAWN_BEGIN … helper_reused=1` (helpers are being shared)

Diagnose.cmd is not part of this package; it was only for the separate
startup-diagnostic build.
