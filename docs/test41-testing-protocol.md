# Test41 install and test

Build **0.1.40-zombies-test41**. It fixes the Test40 freeze, makes the BO1
zombie models load, and handles rooftop spawns.

## Install

1. Close MW2 and extract the complete Test41 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test41 folder. This removes Test40.
3. Run **Install.cmd** from the Test41 folder. It should print
   `Black Ops found: …` and `Kino der Toten found: …`.

## Run A: Favela

1. Start MW2 Single Player and wait about 10 seconds on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies, pick **Favela** and press
   ENTER on Start.

What should happen:

- **The menu row.** **BO1 anims / models** shows `A68/68 M6 SO3` once the
  map has loaded.
- **The models.** Zombies rise in German honor-guard uniforms with BO1
  zombie heads.
- **Attacks.** Let several zombies reach you and swing at you. Back away and
  let them follow, for at least two minutes. The game should not freeze.
- **Rooftops.** A zombie stuck on a rooftop should vanish within a few
  seconds and come back somewhere reachable. That rooftop should not be used
  again.

Then:

1. Take screenshots (F12 in Steam) of zombies close up and far away.
2. Select **Zombies info to log** once.

## Run B: Arcadia and Gulag showers

Play a few rounds on each, so you also see runners and sprinters.

## If it freezes again

Wait about 10 seconds before closing MW2. In that time the mod writes
`bo1mw2\logs\hang-<time>.txt` and `.dmp`. If an error box appears, write
down its text.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots** of the zombies;
- **Look:** whether the bodies, heads and textures look right;
- **Freezes and errors:** any freeze or error text;
- **Rooftops:** whether stuck zombies still appeared.

## Useful log lines

**Map load:**

- `BO1_MODELS level_zone=<zone> append=1 …`
- `BO1_MODELS precache=1 bodies=4 heads=4 … helmet_swap=1`
- `LEVEL_PREP … bo1_models=8 zone_loaded=1`

**In play:**

- `BO1_MODELS confirmed=1` (after 45 s of zombies wearing the models)
- `ZOMBIE_MODELS … dressed=… dress_failures=0`
- `ZOMBIE_UNREACHABLE entity=… point=… blacklisted=1` (a rooftop spawn)
- `ZOMBIE_REACH unreachable_recycled=… bad_points=…`

**Diagnostics:**

- `HANG_WATCH started=1 fault_recorder=1`
- `FIRST_CHANCE …` and `HANG_…` lines, if anything goes wrong
