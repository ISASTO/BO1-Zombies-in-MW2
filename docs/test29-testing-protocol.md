# Test29 testing protocol

Build: **0.1.28-squad-test29**. Goal: determine whether rendering stays stable
through the former 50-actor flicker range, whether the returned glass-physics
crash is prevented, and which resource fails if another limit remains.

The 256 slots are shared by campaign and spawned actors. This is a test build;
the native fixtures do not establish stable 256-actor Windows gameplay.

## Setup, once

1. Close MW2. Run the previous package's Uninstall.cmd, extract the complete
   Test29 ZIP, and run its Install.cmd against the folder containing iw4sp.exe.
   The installer backs up local player/save data. Keep that backup.
2. Launch normally. Start **Estate fresh through Mission Select** for the first
   run. Avoid loading a checkpoint produced during a previously corrupted run.
3. Use **Soldier**, World Speed **1.0x**, Freeze AI **OFF**, and Invincible Spawned
   Friendlies **ON**. Begin with Rapid Fire, Explosive Bullets and Hitmarkers
   **OFF**. Keep graphics settings and FOV constant through the first two runs.
   Other familiar options can remain off until the crowd test passes.
4. Before spawning, select **AI info to log** in the AI menu. Let the mission run
   for 20 seconds and check that campaign characters and weapons animate normally.

An expanded build should log AI_CAPACITY_EXPANSION enabled=1 and actors=256.
AI_RENDER_CROWD should report skin_limit_bytes=75497472 and
surface_limit_bytes=2097152. If it does not, collect the logs and stop this run.
You do not need to inspect logs between every step below.

## Run A: rendering without combat, Estate

Spawn one friendly at a time. Keep the crowd near you, and do not shoot or break
glass during this run. Use the menu's alive enemy/friendly counts as a guide;
the log records the exact total, including campaign actors.

| Approximate total actors | Hold and inspect |
| --- | --- |
| 16, 32 | 20 seconds each; face the crowd, then turn away and back. |
| 48, 64, 80, 96 | 30 seconds each; watch body and weapon visibility, walk around the crowd, then back away and approach again. |
| 128, 160, 192, 224 | 30 seconds each, provided the earlier stages stay stable. |
| Highest permitted total, up to 256 | 60 seconds; the game should refuse extra actors cleanly when the shared pool is full. |

At each stage, select **AI info to log**. Note the first count where something
changes. Check whether actors continue moving normally, whether only parts of
models disappear, and whether the behavior depends on looking at the crowd.

**At the first flicker, stop spawning.** Select AI info to log, look away for
five seconds, look back, and repeat after walking farther from the crowd. Wait
20 seconds, select AI info to log again, then collect the logs. Do not add another
50 actors just to obtain a crash; this stable observation is useful evidence.

If the run stays stable, remove all spawned actors, wait 20 seconds, and spawn
32 again. Confirm that removal completes and replacements remain visible.

## Run B: combat and glass, fresh Estate

Start a fresh mission again. Use the same settings and create **32**, then
**64**, then **96** total actors. At each stage:

1. Select AI info to log and wait 10 seconds.
2. Fire ordinary shots and break windows/glass near the crowd for 30 seconds.
   Keep Explosive Bullets off so the first test isolates ordinary impacts.
3. Select AI info to log again, then check for a crash, missing models, large
   stutters, or glass pieces behaving abnormally.

Only progress toward 128 and higher if both rendering and combat remain stable.
If something first fails after glass breaks, record that even if the game stays
running. The physics guard counters distinguish prevented invalid queries from
a run in which the bad physics state never occurred.

## Run C: second mission and lifecycle checks

1. Repeat Run A's 32, 48, 64 and 96 stages on **fresh Contingency**. Continue
   upward only while stable. This checks whether the threshold follows scene
   complexity rather than a fixed actor count.
2. At a known stable count, preferably 32 first and then 64, let the mission
   autosave or advance a stage. Check whether the crowd remains visible and
   retains normal movement.
3. Test death/checkpoint reload, then remove the spawned actors and replace
   them. Repeat removal/replacement three times at the stable count.
4. After these pass, restore your usual weapon options one at a time. Test MP
   hitmarkers and rapid fire during ordinary combat before adding explosives.

Keep checkpoint/reload results separate from the fresh-mission results. A
failure on reload should include the approximate crowd size saved and loaded.

## What to send back

After the affected run, use the package's **CollectLogs.cmd** or Diagnose.cmd.
If the game crashes, collect before launching again when practical. Each crash
also keeps its own log; old crashes in the ZIP are expected.

Include this short record:

    Mission and run: Estate A / Estate B / Contingency C
    First flicker count, if any:
    Highest stable count and time held:
    Crash count, if any:
    Looking away / moving farther changed flicker: yes / no
    Only surfaces disappear, or positions/animations also jump:
    Glass had been broken before the problem: yes / no
    Fresh start, autosave transition, or checkpoint reload:
    Weapon options enabled:

No screenshot or video is required. If you already have one, a short clip of
the first flickering stage would help distinguish missing surfaces from pose
or position changes.

## How the diagnostics will be interpreted

| Evidence | Meaning |
| --- | --- |
| Increasing gpu_failures or cpu_failures | The enlarged skin budget was reached; rejected reservations leave the allocation counter within bounds. |
| Increasing surface_failures | The separate animated-model record pool was exhausted. |
| Increasing exhausted or invalid_records in AI_RENDER_HANDLES | The animated handle table filled or a native record sequence failed validation. |
| Increasing native_surface_pool warnings | The unchanged rigid/packed record pool reached its own limit. |
| Increasing native_draw_list or another AI_RENDER_OVERFLOW / AI_RENDER_WARNING category | Another native drawing limit is active; the category identifies it. |
| nonfinite_glass_queries increases | Invalid glass geometry was blocked. The first origin/rotation and descriptor are retained. This is prevention evidence, not proof that the source of invalid physics is fixed. |
| invalid_features increases | An invalid collision feature was blocked before vertex-index arithmetic. |
| Flicker while these counters stay zero | Investigate animation/pose, visibility or another uninstrumented path; do not infer renderer exhaustion merely from the crowd size. |

Counts are cumulative within one launch. Render peaks are captured inside
allocations rather than sampled only every ten seconds. The AI info action
prints the current counters immediately; background reporting remains infrequent.
