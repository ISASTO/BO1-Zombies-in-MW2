# Performance and persistence test15

Build: **0.1.14-performance-test15**. Close MW2, run Uninstall.cmd then
Install.cmd from the new extracted package, and restart the mission once.

## Changes

- Normal log lines no longer call FlushFileBuffers. Crash reports still flush.
- World discovery runs only on the native server thread, every 500 ms or when
  a queued operation needs fresh templates. Buffers are reused. Menu snapshots
  refresh every 50 ms; drawing no longer reads the full player state every frame.
- Native support updates handle at most four owned NPCs per tick. Menu polling
  does not wait for the AI service lock. Reasserting an unchanged enemy handle
  increments a counter rather than producing another per-NPC log line.
- Rapid Fire uses LEFT/RIGHT for 1x-10x, starts at 2x, and uses ENTER for ON/OFF.
  A 1 ms timing floor replaces the old 20 ms floor. The held weapon definition
  is shared by AI using that weapon. OFF/weapon reset restores original timing.
- No Recoil uses the native player_recoilscaleon(0) fields verified against the
  native recoil consumer. OFF restores owned scale/flag values and preserves
  unrelated weapon flags. Normal saves suspend this override while serializing.
- Enemy/friendly model names remain visible on their selector rows.
- Appearance capture stops at the first empty attachment model, as the native
  DObj builder does. Unused tag slots cannot reject Ghost. Attachment strings
  retain native references across delayed model application and live recovery.
- Before a normal autosave write, live mod NPCs are recorded and their temporary
  native entities are removed safely. The same-session recovery queue rebuilds
  them with their health and requested appearance, one per server tick when
  placement/slots permit. Friendly recovery has priority. Allies return nearby;
  enemies return through the normal nearest-point placement path. This may
  produce a brief recovery interval after saving, rather than permanent loss.
- Mission actor initialization raises a reset soft budget back to the verified
  physical 32 slots while mod NPCs exist. If all physical slots are occupied,
  corpses and mod enemies are reclaimed before allies. A displaced live ally
  waits in the recovery queue until a slot becomes available.
- Remove Our Spawned Actors also cancels pending recovery. Kill All Enemies
  cancels hostile recovery, so it cannot bring killed enemies back later.

## Focused retail checks

1. Check ordinary movement and aiming for 30-60 seconds before spawning actors.
   Pay particular attention to the old roughly three-per-second stutter.
2. Spawn 5, then 10-20 Soldier allies. Check steady movement with the menu closed
   and with AI open. Record whether periodic hitching changes with population.
3. In WEAPONS, select Rapid Fire: try 2x, 5x and 10x. Adjust while OFF and ON,
   switch weapons, then turn OFF and reset weapons. Normal timing should return.
4. Toggle No Recoil on a rifle. Fire while aiming and from the hip; compare OFF,
   ON and OFF again. Confirm that Reset Weapon Toggles clears it.
5. With Ghost present in the mission, select Ghost and spawn one helper. Allow
   about a second for appearance initialization. Check body/head/attachments,
   movement and combat. Soldier should continue working. Price/Soap/Roach still
   require their appearances to be loaded in that mission.
6. Change Enemy Model several times. Its name should remain visible regardless
   of the last spawned actor. Spawn an enemy and recheck combat/Kill All Enemies.
7. Keep several allies through a "game saved" event and a mission stage change.
   The Recovering counter may briefly rise, then return to zero as allies return.
   At 32 occupied slots, recovery can wait until campaign slots become free.
8. Remove Our Spawned Actors during recovery; nothing should reappear afterward.
   Finally test death/checkpoint reload for stability. Those events clear the mod
   population; it is not serialized into the native checkpoint.

Close the game and run Diagnose.cmd before another launch overwrites latest.log.
Return that ZIP plus observations for checks 1, 2, 5 and 7. AI_PERF reports average
and peak service/world-scan times, recovery count and combat reassertions.

## Validation boundary

Portable suites, Windows cross-compilation and exact-retail instruction/field
checks run here. This Linux workspace cannot execute MW2. Frame pacing, native
appearance and recovery behavior remain pending this retail test. Expansion to
256/512 actor slots is deliberately deferred until this pass is stable. BO1
zombie behavior and G18 dual rendering are not changed in this pass.
