# Test31 install and retest

Build: **0.1.30-squad-test31**. Focus: retain Test30's apparent rendering
improvement and pass the former crash near 97 actors. The actor target is 256;
it includes campaign actors and remains experimental.

## Install

1. Close MW2. Run the previous package's **Uninstall.cmd**.
2. Extract the complete Test31 ZIP into a new folder. Run **Install.cmd** and
   select the game folder containing iw4sp.exe. Keep the automatic save backup.
3. Start **Estate fresh through Mission Select**. Use Soldier, World Speed
   1.0x, Freeze AI OFF, Invincible Spawned Friendlies ON. Keep Explosive Bullets,
   Rapid Fire and Hitmarkers OFF for the first run.
4. Play normally for 30 seconds without spawning. Open the menu with **F4**;
   use arrows and Enter. Select **AI info to log** before starting the crowd.

## Run A: crowd without deliberate glass impacts

Spawn friendlies progressively. Use the approximate total count in the menu;
the log records the exact campaign-plus-spawned total. Keep graphics and FOV
the same as Test30. Do not intentionally break windows during this run.

| Total actors | Hold |
| --- | --- |
| 32, 64, 80 | 20 seconds each. |
| 96 | 60 seconds; move, turn away and look back at the crowd. |
| 112, 128 | 60 seconds each, if the preceding stages remain stable. |
| 160, 192, 224, highest allowed up to 256 | 30 seconds each; 60 seconds at the highest count. |

Select **AI info to log** at each stage. Stop adding actors at the first
flicker, pose jump or other visible failure; log it and collect diagnostics.
If stable, remove the spawned actors and create 32 replacements to check reuse.

## Run B: ordinary impacts and glass

Start Estate fresh again. Use the same settings. At 32, then 64, then 96
actors, fire ordinary shots and break windows near the crowd for 30 seconds.
Select **AI info to log** before and after each stage. Continue to 112 and 128
only while stable. Keep explosives OFF until these stages pass.

The new AI_GLASS_FRACTURE counters show when invalid or overfull fragments were
rejected. A prevented fragment may be omitted from that particular shatter;
ordinary valid pieces retain native construction and publication.

## Follow-up

If both runs pass, repeat the 64/96/128 stages on fresh Contingency, then test
checkpoint reload and removal/replacement at a stable count. Restore weapon
options one at a time after the basic crowd and glass tests pass. The
[full crowd protocol](test31-crowd-protocol.md) includes the extended lifecycle
and rendering observations.

## Send back

Use **CollectLogs.cmd** immediately after a failure, preferably before launching
again. Include the mission, highest stable count, crash count, whether glass
had broken, and whether flickering returned. If no crash occurs, collect after
the highest successful stage so the new guard counters can be checked.
