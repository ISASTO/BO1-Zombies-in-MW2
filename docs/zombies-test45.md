# Test45: BO1 sound, HUD, powerups and dismemberment

Version **0.1.44-zombies-test45**. The models, sounds and spawns are as in
Test44.

## What Test44 showed

The owner's Test44 run (results `20261029-063050`): the sounds and the models
worked on every map. What was left:

| Asked for | Done in Test45 |
|---|---|
| No soldier sounds on top of the zombie sounds | Every soldier sound MW2's animscripts play on a zombie is dropped |
| No mission music; keep the map's ambience; add zombie ambience | The level's music is blocked; Kino's underscore and eerie one-shots play |
| BO1's HUD and powerup models | Chalk tally rounds, BO1's powerup icons, the four BO1 powerup models |
| The announcer on powerups ("Max Ammo!") | BO1's announcer for every powerup and for dog rounds |
| Dismemberment | BO1's gib rules: heads pop, arms and legs come off and fly |
| Zombies info in the log after each round and at the end | Automatic |
| Zombies ends when the game ends or the player leaves | GAME OVER returns to the main menu; quitting ends zombies |

## No soldier sounds

In Test44 the gear rattle (`gear_rattle_run`) still played on every running
step of a zombie. MW2's `notetracks.gsc` plays it after each footstep. The
playsound redirect now sorts every one-argument `playsound` MW2's animscripts
make on a zombie:

| Sound | What happens |
|---|---|
| `step_run_<surface>` | BO1 zombie step on that surface (as in Test43) |
| `bodyfall_*` | Kept: the thud of a body falling |
| `gear_rattle_*`, `step_scrape_*`, `land_*` | Dropped |
| `weap_*`, `ai_shotgun_*` (reload foley) | Dropped |
| `generic_*`, `*_death` (soldier voices) | Dropped |
| anything else (knife, dive) | Dropped |

A `playsound` that names a notify is left alone: its script waits for the
sound to end.

**In the log:** `ZOMBIE_AUDIO ... dropped_gear=N dropped_scrape=N ...`.

## No mission music, zombie ambience instead

**The music.** All mission music goes through the `musicplay` builtin
(`maps\_utility::MusicPlayWrapper`, `music_loop`, `music_play`). It sends
"mu_play" to the client. On a level launched for zombies, its first bytes
jump to a function that returns at once:

- installed while the level's scripts link, before any of them runs;
- removed when the next level loads.

Campaign and normal Spec Ops music are untouched. `musicstop` runs once when
the game starts. The map's ambience (`ambientplay`) is a different builtin and
keeps playing.

**The zombie ambience.** It is built into the sound zone from Kino's bank
(`zombie_theater.ff` and the `.iwd` archives):

- **A bed.** Kino's underscore (`mus_theatre_underscore`), quiet and in 2D,
  replayed when it ends. The build makes a smaller copy: mono, at most
  24 kHz, at most 150 s (about 7 MB). Its length is kept in
  `bo1mw2\cache\bo1_zombie_sounds.info`. If the underscore is missing, the
  WaW wave loop is used instead.
- **One-shots.** Every 15-40 s, one of Kino's wind howls, wood groans,
  whispers or metal clangs.

## The announcer

BO1's announcer is in the localized bank `zmb_theater.english` in
`zone\English\en_zombie_theater.ff`. Its voice files are streamed from the
localized archives (`english/sound/voiceovers/zmb/ann/...` in
`main\localized_english_iw*.iwd`). The sound build finds the localized zone
itself (English first, else another language).

| Moment | Line |
|---|---|
| Max Ammo picked up | `zmb_vox_ann_maxammo` |
| Insta-Kill | `zmb_vox_ann_instakill` |
| Double Points | `zmb_vox_ann_doublepoints` |
| Kaboom (nuke) | `zmb_vox_ann_nuke` |
| A dog round starts | `zmb_vox_ann_dogstart` |

MW2's centre text for a powerup now shows only if the announcer is missing.

## BO1 HUD

The overlay draws BO1's own HUD art, read from the owner's archives by the
model build into `bo1mw2\cache\bo1_hud.bin`:

- **The round, bottom left.** Rounds 1-5 are BO1's chalk tally marks
  (`chalkmarks_1`-`5`). Rounds 6-10 are a group of five plus the rest. Above
  ten, the round is a large blood-red figure. When a round starts, the marks
  flash white and fade back to red over 2 s, as `chalk_one_up` does. MW2's
  "ROUND N" centre text is gone when the chalk marks are there.
- **Powerups, bottom centre.** BO1's Double Points and Insta-Kill icons
  (`specialty_2x_zombies`, `specialty_instakill_zombies`). They blink in their
  last 5 s.
- **Points, bottom right.** As before.

The images are BO1's IWI files, decoded on the owner's PC. The decoder reads
RGBA, RGB, luminance-alpha, luminance, alpha and DXT1/3/5.

## Powerup models

The four powerups are BO1's models from `common_zombie.ff`, with their gold
and ammo-box materials:

| Powerup | Model |
|---|---|
| Max Ammo | `zombie_ammocan` |
| Double Points | `zombie_x2_icon` |
| Kaboom | `zombie_bomb` |
| Insta-Kill | `zombie_skull` |

`zombie_skull`'s single bone has no name in the zone; it becomes
`tag_origin`.

A dropped powerup is a `script_model`. It floats 40 units up, where BO1 puts
it. As in `powerup_wobble`, it turns to a new attitude every 2.5-5 s. As in
`powerup_timeout`, it blinks before it times out. It is deleted when grabbed
or when it times out. The green marker now shows only where no model could
be placed.

`spawn()` returns an object id, not an entity number. The new model is the
one entity that became in use during the call. Spawns are refused when fewer
than 96 entities are free. The damage hook, which can run inside
`G_Damage`, never spawns or deletes anything; the service does it on the next
frame.

## Dismemberment

The honorguard zombie's dismemberment models follow it in
`zombie_theater.ff`:

| BO1 | What it is | Recognised by |
|---|---|---|
| `torsoDmg1` | upper body | uses both wrists, no knees |
| `torsoDmg2` | right arm torn off at the elbow | no `j_elbow_ri`, `j_wrist_ri` |
| `torsoDmg3` | left arm torn off | no `j_wrist_le` |
| `torsoDmg4` | guts | the last left-arm variant |
| `torsoDmg5` | neck stump | ends at `j_neck`, no head |
| `legDmg1` | legs | both knees and ankles |
| `legDmg2` | right leg off | no `j_knee_ri`, `j_ankle_ri` |
| `legDmg3` | left leg off | no `j_ankle_le` |
| `legDmg4` | no legs | no knees, no ankles |
| `gibSpawn1-4` | the pieces that fly off | two bones, physics data |

The parts are recognised by the bones their vertices use, never by position.
Only this character's parts count: they use its body sheet or head skin.

**The materials.** Each surface's material is sorted into:

- the body sheet or head skin (as on the full zombie);
- the gore cap of a torn limb (`gore_limbs`, with its own textures);
- a blood decal (`gore_blend`, `gore_punch`). Decals are dropped, because MW2's
  opaque model material cannot blend them.

The thrown pieces' physics data is not converted; they fly under gravity
instead (below).

**The rules** (`_zombiemode_spawner.gsc`, `zombie_death.gsc`):

- A hit may gib only when the player dealt it with a weapon (not melee) and it
  took at least 10% of the health the zombie had.
- **Head:** health at most 10% after a hit to the head, helmet or neck. The
  head comes off, the neck stump goes on, and the head-gib sound plays. A
  zombie still alive bleeds 20% of its health every second.
- **Limbs, once per zombie:**
  - torso: the guts or the right arm;
  - an arm or hand: that arm;
  - a leg or foot, only on the killing hit: one leg (twice as likely) or both.
- **Explosive kills** gib half of the time, any of the six.
- **Deviation:** BO1 never pops heads with pistols. Here pistols count,
  because zombies here start with the USP .45.

**Applying a gib.** As `do_gib` does: `SetModel(upper body)` plus
`Attach(legs)`, and the gib sound. The thrown piece is spawned at its tag
(`j_elbow_ri`, `j_elbow_le`, `j_knee_ri`, `j_knee_le`), read before the body
changes. It flies outward and along the shot, under MW2's gravity
(`movegravity`), spinning, until the path meets the world. It lies there for
12 s; at most 16 at a time.

A head gib rebuilds the attachments with `detachall`: neck stump, plus the
legs if a limb came off before. `detach` raises a script error when the model
is not attached; `detachall` never does.

The decision is made in the damage hook; the change is applied by the service
on the next frame, on the living or dying actor.

**In the log:**

- `ZOMBIE_WORLD ... head_gibs=N limb_gibs=N pieces_thrown=N gib_failures=N`
- `ZOMBIE_GIB entity=... applied=0` (the first 8 failures)

## Zombies info in the log

The full report ("Zombies info to log") is written:

- when each round after the first starts (`ZOMBIE_AUTO_LOG reason=ROUND_N`);
- when a game ends, before its state is cleared
  (`ZOMBIE_AUTO_LOG reason=END_<reason>`).

Two lines are new:

- `ZOMBIE_AUDIO`: music blocked, ambience, announcer, dropped soldier sounds;
- `ZOMBIE_WORLD`: powerup models and dismemberment.

## The end of a game

- **GAME OVER** (the player died) on a map launched for zombies: the GAME OVER
  screen shows for 8 s, then the game returns to the main menu ("disconnect"),
  as BO1 does. F4 → Start during those 8 s starts a new game instead. The old
  reload of the same map is still there as the `autoRestart` setting, now off.
- **Leaving the level** (quit to the main menu, or the game disconnects): once
  no level has been running for 1.5 s, the zombies game ends. Nothing waits to
  relaunch, and the HUD is gone.
- **A pending zombies launch** is dropped when a different level becomes
  playable after a load (the player picked something else).
- At the end, the sounds playing on the player stop (`stopsounds`): the bed,
  one-shots and announcer. The powerup models and thrown pieces are deleted.
  Then the game-over music plays.

## The hang watchdog

Both hang reports in Test44 caught the main thread waiting inside the
graphics driver, called through this DLL's Direct3D proxy. The game carried
on afterwards. Such a wait is now logged as `HANG_DRIVER_WAIT` and is reported
as a hang only if it lasts 30 s.

## Checked here

- **Native tests:** 26/26, including the new `test45_tests`:
  - soldier-sound classes;
  - every gib rule;
  - the IWI decoder (luminance-alpha, DXT1, DXT5) and the HUD pack;
  - the bed's smaller copy;
  - surface removal and used bones;
  - with the owner's zones: the dismemberment and powerup carves and the
    announcer bank's file paths.
- **Python tests:** 73/73 (`Test45Tests` plus the updated sound and binding
  tests).
- **Binding verifier:** Test45 section:
  - `musicplay`'s prologue and its "mu_play" server command;
  - `stopsounds` (event 5 through `G_AddEvent`);
  - the entity array;
  - `spawn` (classname, spawn class) and the mover check in `movegravity` and
    `rotateto`;
  - all 11 new builtins in the method/function tables.
- **The whole zombie zone through the retail loader:** 23 models (the
  character, its 9 dismemberment models, 4 pieces and the 4 powerups),
  6 materials, 16 images, with the owner's zones.
- **The sound zone through the retail loader:** 56 alias lists with the
  ambience one-shots and gib sounds.
- **All 30 native suites and probes.**

## Not covered offline

- **The streamed files.** The underscore and the announcer could not be
  decoded here: they live in archives too large to copy. They decode like the
  round music, which works.
- **In game:** how the HUD, powerup models, ambience volume and dismemberment
  look and sound.
- **The music block.** Whether any Spec Ops level plays music another way.

**Testing:** no Windows gameplay test of Test45 yet.
