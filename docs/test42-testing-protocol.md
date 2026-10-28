# Test42 install and test

Build **0.1.41-zombies-test42**. It fixes the freeze that the Test41 hang
reports traced to a script string being freed too early.

## Install

1. Close MW2 and extract the complete Test42 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test42 folder. This removes Test41.
3. Run **Install.cmd** from the Test42 folder. It should print
   `Kino der Toten found: …`.

## Run A: Favela

1. Start MW2 Single Player and wait about 10 seconds on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies, pick **Favela** and press
   ENTER on Start.

What should happen:

- **The menu row.** **BO1 anims / models** shows `A68/68 M6 SO3`.
- **Rising.** Zombies climb out of the ground wearing the BO1 honor-guard
  bodies and zombie heads. **The game keeps running when the first ones
  finish rising** (this is where Test40 and Test41 froze).
- **Moving and attacking.** Let them walk to you and attack for a few
  minutes. Kill some, and play to round 3 or 4.
- **Screenshots.** Take some (F12 in Steam) close up and far away.
- **Logging.** Select **Zombies info to log** once.

## Run B: Arcadia and Gulag showers

Play a couple of rounds on each.

## If it freezes again

Wait about 10 seconds before closing MW2, so the mod can write
`bo1mw2\logs\hang-*.txt/.dmp`.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots** of the zombies.
- **Look:** whether the bodies, heads and textures look right.
- **Freezes:** any freeze, or error text.

## Useful log lines

- `BO1_MODELS precache=1 bodies=4 heads=4 …`
- `ZOMBIE_SPAWN … rise=1` and `ZOMBIE_PREP … rises=…`
- `ZOMBIE_STRINGS pinned_level=… pinned_total=…`
- `ZOMBIE_MODELS … dressed=… dress_failures=0`
- `BO1_MODELS confirmed=1` (after 45 s with zombies wearing the models)
