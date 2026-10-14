# Test30 retest

Build: **0.1.29-squad-test30**. First verify that the Test29 startup regression
is gone; then resume crowd testing.

1. Close MW2. Run Test29's Uninstall.cmd. Extract the entire Test30 ZIP and run
   Install.cmd against the folder containing iw4sp.exe. Local player/save data
   is backed up before installation.
2. Start **Estate fresh through Mission Select**. Leave all mod toggles off and
   do not spawn actors yet. Walk around and play for two minutes. Check normal
   weapon and campaign NPC rendering. Press F4 and select AI info to log.
3. Start **Contingency fresh** and repeat the two-minute startup check. If
   either mission crashes, stop here and collect logs before launching again.
4. If both work, use Soldier, World Speed 1.0x, Freeze AI OFF and Invincible
   Spawned Friendlies ON. Keep rapid fire, explosives and hitmarkers OFF for
   the first crowd run. Add friendlies individually, holding around 16, 32,
   48 and 64 total actors for 20-30 seconds each. Select AI info to log at each
   stage. Counts include campaign actors.
5. At the first flicker, stop spawning. Log AI info, look away and back, then
   try moving farther away. Collect the results. If stable, continue through
   80 and 96, then the higher stages in test30-crowd-protocol.md.
6. Keep gunfire/glass and checkpoint/reload tests separate, as described in
   that full crowd protocol.

Run **CollectLogs.cmd** after the affected run. Send the resulting ZIP and:

    Mission:
    Mission started normally and ran for two minutes: yes / no
    Actors spawned before a crash:
    First flicker count, if any:
    Highest stable count and time held:
    Fresh Mission Select start or checkpoint reload:

Use Diagnose.cmd if the game itself fails to launch. Test30's DLL and log
version must be 0.1.29-squad-test30. The shared target is still 256 native
actors; passing the startup check alone does not establish crowd stability.
