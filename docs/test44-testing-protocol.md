# Test44 install and test

Build **0.1.43-zombies-test44**. It fixes the crash on Favela and Terminal,
the empty Gulag showers and the swings from afar.

## Install

1. Close MW2 and extract the complete Test44 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test44 folder. This removes Test43.
3. Run **Install.cmd** from the Test44 folder. It also clears the crash
   guard, so the first start uses the BO1 models and sounds again.

## Run A: Favela

1. Start MW2 Single Player and wait about 10 seconds on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies, pick **Favela** and press
   ENTER on Start.

What should happen:

- **No crash.** Favela loads with no "Exceeded limit of 1350 'loaded_sound'
  assets" error.
- **The menu row.** **BO1 anims / models / sounds** shows `A76/76 M6 S52 SO3`.
- **The models.** Zombies rise wearing BO1 bodies and heads, with normal
  proportions.
- **The sounds.** Moans, footsteps, attack vocals, swing whooshes, death
  vocals, the round start music.
- **The attacks.** Zombies swing only when they are next to you.
- **The spawns.** Zombies come from places they can walk from; none get stuck
  on roofs.

## Run B: Terminal, Gulag showers, Contingency

- **Terminal:** it should load. Play two rounds.
- **Gulag showers:** zombies should reach you in the showers.
- **Contingency:** watch them run around trees and up slopes. Note any moment
  they look like MW2 soldiers (how they move, what they were doing).

Select **Zombies info to log** once on each map.

## If something goes wrong

- **A freeze.** Wait about 10 seconds before closing MW2.
- **A crash.** The next start skips the models and sounds once; start once
  more to retry them.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots:** the zombies, close up.
- **Look:** whether the bodies, heads and anims look right.
- **Sound:**
  - whether the sounds play;
  - whether they are too loud or too quiet next to the guns;
  - whether the round music plays.
- **Spawns:** any map where zombies take very long to arrive, or never do.
- **Errors:** any error text.

## Useful log lines

- `ASSET_POOLS attempted=1 applied=1 reason=OK loaded_sound=4096` at startup.
- `ASSET_POOLS level=... loaded_sound_in_use=N capacity=4096` on each map.
- `BO1_SOUNDS level=... available=52 ... playsound_hook=1`.
- `ZOMBIE_NAV build=1 nodes=... usable=... links=...`.
- `ZOMBIE_NAV player_node=... reachable_points=R/P`.
- `ZOMBIE_SPAWN ... distance=... path=...`.
- `ZOMBIE_REACH ... by_path=... by_line=...` (`by_line` should stay 0).
