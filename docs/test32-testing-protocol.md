# Test32 install and retest

Build: **0.1.31-squad-test32**. Focus: pass the former crash at 96–98 actors
on Estate, and capture where invalid glass shatter requests come from.

## Install

1. Close MW2.
2. Extract the complete Test32 ZIP into a new folder.
3. Run **Uninstall.cmd** from the Test32 folder and select the MW2 folder.
   It removes the Test31 DLL (it checks the Test31 install receipt).
4. Run **Install.cmd** from the same folder and select the MW2 folder. Keep the
   automatic save backup.

## Run A: the crowd that crashed

Repeat the Test31 runs as closely as possible.

1. Start **Estate fresh through Mission Select**. Soldier, World Speed 1.0x,
   Freeze AI OFF, Invincible Spawned Friendlies ON. Explosive Bullets, Rapid
   Fire and Hitmarkers OFF.
2. Play 30 seconds, then select **AI info to log** (F4 menu).
3. Spawn friendlies to 32, 64, 80, then **96** and hold 60 seconds.
4. If stable, continue to 112 and 128 (60 seconds each), then 160, 192 and the
   highest you can reach up to 256 (30 seconds each).
5. Select **AI info to log** at each stage.

## Run B: combat and glass

Start Estate fresh again with the same settings. Bring in some enemies and
let the fight break windows near the crowd at 32, 64 and 96 actors, then
higher while stable. Select **AI info to log** after each stage.

## Also useful

If you die during a run, note it. Dying with a large crowd led to the separate
command-list crash in Test31; Test32 watches for the write behind it.

## What the new log lines mean

- `AI_GLASS_STATE missing_place_shatters=N` — times Test32 skipped a stock
  fracture that would have run on garbage. A nonzero value is the old crash
  being prevented.
- `rejected_place_damage` with `last_rejected_caller_rva` — a glass damage
  request outside the loaded places, and which native code sent it.
- `AI_GLASS_IGNORED_FREE` — a glass release of a piece that was already free,
  and which native code did it.
- `AI_STATIC_WATCH armed=1` — the command-list watch is active.
  `AI_STATIC_WATCH_HIT` lines identify the code writing into that memory.

## Send back

Run **CollectLogs.cmd** right after any crash, before relaunching if possible.
If nothing crashes, collect after the highest stage so the counters above can
be checked. Mention the mission, highest stable actor count, whether glass
broke, whether you died, and any flicker or pose glitches.
