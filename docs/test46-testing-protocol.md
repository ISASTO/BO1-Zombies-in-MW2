# Test46 install and test

Build **0.1.45-zombies-test46**. It adds BO1's effects (powerup glow,
hellhound lightning, eyes and trails, the dirt of a zombie climbing out),
BO1 hellhounds on every map, and a quiet Spec Ops map (no Spec Ops HUD,
objectives or soldier talk). It also fixes the LOCKED weapon options and
zombies sinking into floors.

## Install

1. Close MW2 and extract the complete Test46 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test46 folder. This removes Test45.
3. Run **Install.cmd** from the Test46 folder.

The first start rebuilds the BO1 model and sound files, because both changed.
This takes a little longer than usual.

## Run A: Favela, rounds 1-5

1. Start MW2 Single Player and wait about 20 seconds on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies. Make sure **Hellhounds** is
   ON, pick **Favela** and press ENTER on Start.

What should happen:

- **The menu row.** **BO1 anims / models / sounds** shows
  `A88/88 M.. P4 G13 H1 F10 S..`. H1 means hellhounds are ready; F10 means
  all ten BO1 effects are registered.
- **A quiet map.**
  - No Spec Ops timer, kill counter, objective text or objective markers.
  - No radio lines and no soldier talk, from zombies or anyone else.
  - The zombies HUD (chalk marks, points) is still there.
- **Zombies climbing out.** A burst of dirt where a zombie starts to climb,
  then a cloud of dust.
- **Powerups.**
  - The model appears with no name text before it.
  - It glows green, hums, and keeps turning and tilting.
  - Picking it up: a flash, the grab sound and the announcer.
- **The F4 weapon rows.** Rapid fire and semi-auto → auto can be switched on
  with every weapon you pick up. They should never show LOCKED while you hold
  a gun.

## Run B: a dog round

Start at round **4** (Starting round) on **Gulag showers** (a map with no dog
spawners) and play until the dog round. It is round 5, 6 or 7: the announcer says
"Fetch me their souls!".

For each hellhound:

- an electric build-up on the ground near you, with a crackle;
- 1.5 s later a lightning strike, a bang and a short screen shake;
- then the hound: BO1's wolf, glowing eyes, and a fire or ash trail.

Then check:

- **Running and biting.** It runs at you and bites. The run should look like
  a dog, not a soldier.
- **Death.** It bursts into fire and ash and its body disappears.
- **The end of the round.** A max ammo drops where the last hound died.

## Run C: Terminal (airport), round 10 or later

Start at round 10 on Terminal.

- No zombie should stand inside a floor or a staircase.
- Note how smooth it is at round 10-15 compared with Test45.

## If something goes wrong

- **A freeze.** Wait about 10 seconds before closing MW2.
- **A crash.** The next start skips the models and sounds once; start once
  more to retry them.
- **A script error.** Take a screenshot of the text.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots or a short clip:**
  - a powerup with its glow;
  - a hellhound arriving (the lightning) and running;
  - the F4 zombies page (the BO1 row).
- **Say:**
  - whether any Spec Ops text, marker or voice is left;
  - whether the powerup keeps turning all the time;
  - how the hellhound looks and moves (skin, run, bite, eyes, trail);
  - whether any weapon option still shows LOCKED;
  - how smooth the game is at higher rounds.
- **Errors:** any error text.

## Useful log lines

- `BO1_MODELS state=READY ... hellhound=1 effects=10 effect_elems=83 ...`
- `BO1_MODELS precache=1 ... hellhound=1` and `BO1_FX precache=10/10`
- `BO1_ANIM swap=88/88`
- `LEVEL_PREP ... bo1_effects=10 hounds=1`
- `ZOMBIE_QUIET patches=1 active=13/13` and `ZOMBIE_QUIET dialogue_lists=N aliases_muted=N`
- `ZOMBIE_DOG_ROUND ... hellhounds=BO1`, `ZOMBIE_SPAWN ... hound=1`
- `ZOMBIE_HOUNDS ready=1 ... arrivals=N strikes=N dressed=N deaths=N`
- `ZOMBIE_FX ... played=N on_tag=N refused=N loop_sounds=N`
- `ZOMBIE_WORLD ... wobbles=N`
- `ZOMBIE_UNSTUCK entity=N ...`
- `FRAME_PERF ...` and `ZOMBIE_PERF ...`
