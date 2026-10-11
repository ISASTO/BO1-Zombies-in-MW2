# Round test5: read-only BO1 script-default preview

Build `0.1.4-round-test5`. Phase 1 core is accepted from the owner's test4 results.
This iteration adds a nested F4 page and a pure calculation module. It does not
start a Zombies game, spawn actors, change campaign AI, or change player health.
The tested engine/player/God implementation and graphics hook source are preserved.

## Source and limits

The rules come from the owner's `common_zombie_patch.ff` GSC sources, not memory:
`_zombiemode.gsc` round count/default callback, health recurrence, capacity and
spawn-delay mutation. `set_zombie_var` in `_zombiemode_utility.gsc` reads
`mp/zombiemode.csv`, so passed defaults may be replaced. Normal difficulty uses
column 2 for the main tuning values; between-round time uses default column 1.

No `zombiemode.csv` name is present in the decompressed streams of the five
supplied zones. This is a name scan, not a complete asset-directory parser.
The menu and diagnostic records therefore label the profile **SCRIPT DEFAULTS /
TUNING CSV MISSING**. Supply `common_zombie.ff` from the BO1 installation next;
it is the specific missing common-zone candidate to inspect for the table.
Do not treat these figures as final tuned retail Kino values.

The preview assumes consecutive normal solo rounds from a fresh round-one
start. Dog/quad rounds, mixed spawns, kill-counter overrides, map callbacks,
early starts, mutators, engine scheduling and full intermission timing are not
simulated. "Between rounds" shows the configured 10-second fallback, not a
measured interval between the last death and the next actor appearing.

The exact local BlackOps.exe confirms integer division produces binary32 float
and the int builtin truncates a float toward zero. Calculation order matters:
truncate the player modifier before early-round scaling, and truncate each
health increase separately. Concurrent limit 24 is distinct from round budget.
The spawn delay tests preserve the script's one-round undershoot below .08.
See [numeric evidence](bo1-round-rules-evidence.json) and the ledger. The rules
are an original implementation; no original script body is distributed.

## Install and test

1. Close MW2. Extract all of `BO1MW2-round-test5.zip` outside the game directory.
2. Run the package's `Uninstall.cmd` for our previous DLL, then `Install.cmd`.
   Select the Steam SP folder containing the tested `iw4sp.exe`. The only
   runtime DLL is `d3d9.dll` beside that executable; keep the tools outside it.
3. Launch normally through Steam. Press F4, open **ZOMBIE MODE**, then
   **Round rules preview**. It also works at the main menu because it reads
   local mathematical constants, not a live actor.
4. Keep **Preview round** selected. Left decreases and Right increases the
   round; holding an arrow repeats. Enter advances one round. Up/Down changes
   the selected row. Numeric output rows are read-only. Bounds are 1 through
   100; the selector clamps and never wraps to zero or overflows.
5. Compare rounds below. Simultaneous cap stays 24 and configured between-round
   time stays 10s. Output uses three decimal places for the spawn interval.

| Preview round | Total zombies | Health | Spawn interval |
| --- | --- | --- | --- |
| 1 | 6 | 150 | 2.000s |
| 2 | 8 | 250 | 1.900s |
| 5 | 24 | 550 | 1.629s |
| 7 | 28 | 750 | 1.470s |
| 10 | 33 | 1045 | 1.260s |
| 20 | 60 | 2701 | .755s |
| 64 | 392 | 178744 | .079s |
| 65 | 404 | 196618 | .080s |
| 100 | 924 | 5525295 | .080s |

6. F4 close/reopen should keep the selected preview round for this process.
   Backspace/Esc returns first to ZOMBIE MODE, then to the home category.
   This temporary preview selection is not a saved game setting.
7. Load a campaign mission, preferably a different map from estate. Check the
   preview while moving/firing, then briefly confirm health and God still work.
   Return to the main menu and verify God becomes LOCKED.
8. Exit and run `Diagnose.cmd`. Attach its ZIP with the displayed values, any
   clipping/input problems, the mission name, and any last action before a crash.

Portable rule fixtures were first run against an empty implementation and
failed, then passed after adding the source-derived calculations. Menu tests
cover nested back behavior, read-only actions, hidden input and both round
bounds. The Windows DLL was delivered for the owner's retail test5 observations.

## Owner result and subsequent tuning recovery

Owner: "Seems to be working fine." Matching test5 diagnostics show a front-end
session with 344 inputs and 290 preview records covering every round 1-100,
native font rendering at 3440x1440, nested back/close, and God blocked without
an active level. No new crash event or dump is included. That run does not prove
mission regression/reset behavior. See [test5 evidence](runtime-test5-evidence.json).

The subsequently supplied common_zombie.ff contains mp/zombiemode.csv. The
original scoped tool recovers normal values matching this preview; test6 changes
its provenance label accordingly. This document's missing-CSV label describes
the delivered test5 build. See [tuning evidence](bo1-normal-tuning-evidence.json).
