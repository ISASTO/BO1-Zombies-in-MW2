# Test43 install and test

Build **0.1.42-zombies-test43**. It fixes the folded, stretched BO1 zombie
models and adds BO1's zombie sounds.

## Install

1. Close MW2 and extract the complete Test43 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test43 folder. This removes Test42 and its
   cached model file.
3. Run **Install.cmd** from the Test43 folder. It should print:
   - `BO1 zombie sounds are built from common_zombie.ff ...`;
   - `Kino der Toten found: …`.

## Run A: Favela

1. Start MW2 Single Player and wait about 10 seconds on the main menu. The
   first start rebuilds the models and builds the sounds.
2. Press **F4** → ZOMBIE MODE → Playable zombies, pick **Favela** and press
   ENTER on Start.

What should happen:

- **The menu row.** **BO1 anims / models / sounds** shows `A68/68 M6 S52 SO3`,
  or `S47` when the round music files were not found.
- **The models.** Zombies rise wearing the BO1 honor-guard bodies and zombie
  heads with normal proportions: arms, legs and heads where they belong, in
  every animation (rising, walking, running, attacking, dying).
- **The sounds.**
  - **Rising:** dirt.
  - **Moving:** moans while they walk, faster moans while they run.
  - **Footsteps:** zombie footsteps.
  - **Attacking:** an attack vocal and a whoosh on every swing.
  - **Behind you:** a moan when one gets close behind you.
  - **Dying:** a BO1 death vocal instead of an MW2 soldier scream.
- **The round music.** The round start music when a round starts, the round
  over music when the last zombie of a round dies.
- **Powerups.** Kill zombies until something drops, and listen for the drop,
  the grab and the powerup's own sound.
- **Hellhounds.** If hellhounds are on and a dog round comes, listen for the
  lightning and the explosions when they die.
- **Logging.** Select **Zombies info to log** once.

## Run B: Arcadia and Gulag showers

Play a couple of rounds on each; listen to the footsteps on different floors.

## If something goes wrong

- **A freeze.** Wait about 10 seconds before closing MW2, so the mod can
  write `bo1mw2\logs\hang-*.txt/.dmp`.
- **A crash.** The next start skips the sounds once (crash guard); start once
  more to retry them.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots** of the zombies, close up and far away (F12 in Steam).
- **Look:** whether the bodies and heads look right now.
- **Sound:**
  - whether the sounds play;
  - whether they are too loud or too quiet next to the guns;
  - whether the round music plays.
- **Freezes:** any freeze, or error text.

## Useful log lines

- `BO1_MODELS state=READY ... detail=built models=6` (first start) or
  `cached`.
- `BO1_SOUNDS state=READY ... built lists=... music_found=... decode_failures=0`.
- `BO1_SOUNDS level_zone=... append=1`, then
  `BO1_SOUNDS level=... available=... steps=... music=.../5 playsound_hook=1`.
- `ZOMBIE_SOUNDS ... played=... steps=... voices_suppressed=...`.
- `BO1_SOUNDS confirmed=1` (after 45 s of sounds).
