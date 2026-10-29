# Test45 install and test

Build **0.1.44-zombies-test45**. It removes the soldier sounds and mission
music, and adds zombie ambience, the announcer, BO1's HUD and powerup models,
and dismemberment.

## Install

1. Close MW2 and extract the complete Test45 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test45 folder. This removes Test44.
3. Run **Install.cmd** from the Test45 folder.

The first start rebuilds the BO1 model and sound files, because both changed.
This takes a little longer than usual.

## Run A: Favela, a few rounds

1. Start MW2 Single Player and wait about 20 seconds on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies, pick **Favela** and press
   ENTER on Start.

What should happen:

- **The menu row.** **BO1 anims / models / sounds** shows
  `A76/76 M6 P4 G13 S..`. P4 is the four powerup models; G13 is the
  dismemberment models.
- **The sound.**
  - No mission music.
  - Favela's own ambience still plays.
  - A quiet, eerie music bed; now and then wind, groans, whispers or clangs.
  - Zombie steps, with no rattle of soldier gear.
- **The HUD.**
  - The round is shown as BO1's red chalk marks bottom left. They flash white
    when a round starts.
  - No "ROUND N" text in the middle of the screen.
- **Powerups.**
  - A drop shows BO1's model (ammo can, x2, bomb, skull) floating and turning.
    It blinks before it disappears.
  - Picking one up: the grab sound, then the announcer ("Max Ammo!",
    "Insta-Kill!", "Double Points!", "Kaboom!").
  - Double Points and Insta-Kill show BO1's icon bottom centre. It blinks in
    the last seconds.
- **Dismemberment.**
  - Shoot zombies in the arms: an arm comes off with a bloody stump and flies
    away.
  - Kill them with leg shots: legs come off.
  - A head shot on a nearly dead zombie pops the head.

## Run B: the end of a game

1. Play until you die (or let the zombies get you).
   - GAME OVER shows, with "Main menu in N s".
   - After about 8 seconds the game returns to the main menu, and the zombies
     HUD is gone.
2. Start zombies again on another map (for example Gulag showers). Then quit
   from the pause menu.
   - The game goes to the main menu.
   - The zombies HUD is gone.
   - Nothing reloads by itself.

## Run C: Terminal or Contingency, round 6 and later

Play until at least round 6 (or start at round 11 with Starting round).

- **Rounds 6-10:** two groups of chalk marks.
- **Above 10:** a large red number.
- **A dog round** (hellhounds on): the announcer at its start.

## If something goes wrong

- **A freeze.** Wait about 10 seconds before closing MW2.
- **A crash.** The next start skips the models and sounds once; start once
  more to retry them.
- **A script error.** Take a screenshot of the text.

## Send back

Run **CollectLogs.cmd** once at the end. The log has the zombies report after
every round and at the end of each game, so you don't need to select
"Zombies info to log". Also send:

- **Screenshots:**
  - the HUD (chalk marks and a powerup icon);
  - a powerup model on the ground;
  - a zombie missing an arm or its head.
- **Sound:**
  - whether any soldier sound or mission music is left;
  - whether the ambience is too loud or too quiet;
  - whether the announcer speaks.
- **Errors:** any error text.

## Useful log lines

- `BO1_SOUNDS state=READY ... ambience=5 announcer=6 localized=zmb_theater.english bed_ms=...`
- `BO1_MODELS state=READY ... powerups=1 gibs=1 gib_pieces=1 ... hud_images=7/7`
- `BO1_MODELS precache=1 ... powerups=1111 gibs=1 gib_pieces=1`
- `BO1_HUD images=7/7`
- `BO1_MUSIC block=1 changed=1`
- `ZOMBIE_AUTO_LOG reason=ROUND_N` and `reason=END_PLAYER_DIED`
- `ZOMBIE_AUDIO ... music_blocked=N ... dropped_gear=N`
- `ZOMBIE_WORLD ... drop_models=N head_gibs=N limb_gibs=N pieces_thrown=N`
- `ZOMBIE_GAME_OVER main_menu=1 command=disconnect`
