# Test46: BO1 effects, hellhounds everywhere, a quiet Spec Ops map

Version **0.1.45-zombies-test46**. Models, sounds and rounds are as in Test45.

## What Test45 showed

The owner's Test45 run (results `20261029-130009`): the powerup models,
HUD and sounds worked. What was left:

| Asked for | Done in Test46 |
|---|---|
| The old powerup name overlay flashes before the model appears | The overlay marker is never drawn for a drop that will get a model |
| The powerup should always turn and wobble, glow and hum like BO1 | BO1's glow effect on the model, its hum loop, the grab flash; wobble calls are counted in the log |
| Some zombies still talk like soldiers | Every MW2 dialogue line plays at volume 0 on a zombies map |
| No Spec Ops objectives, voice lines, timers, kill counters, goals | The Spec Ops HUD, objective markers and radio lines are off on a zombies map |
| Poor performance, worse at higher rounds | Player-feature reads every 250 ms when nothing is enabled; one D3D state block; frame and zombie timings in the log |
| Zombies clip through the floor on Terminal | A zombie found below the floor twice in a row is put back on it |
| Hellhounds with BO1's lightning, sounds and effects, anywhere a zombie can spawn, with BO1's skin and animations | BO1 hellhounds: Kino's wolf model, BO1's dog animations, the lightning strike, eye glow and fire/ash trail, on every map |
| Rapid fire and semi-auto -> auto sometimes show LOCKED | Fixed: about half of a level's weapons were refused by a wrong alignment check |

## LOCKED weapon options

**Why it happened.** The weapon rows read the held weapon's definition. MW2
allocates weapon definitions in the level's zone with 4-byte alignment
(`Load_WeaponCompleteDefPtr` 0xe4590 and `Load_WeaponCompleteDef` 0xe4230 use
`DB_AllocStreamPos(3)`). Test45 and earlier required 8-byte alignment. So the
definition of about every second weapon looked invalid, and its rows showed
LOCKED while you held it.

**The fix.** The check now uses the zone's real alignment
(`kZoneAssetAlignMask = 3`).

## A quiet Spec Ops map

On a map launched for zombies only (never in Spec Ops or the campaign):

- **The Spec Ops HUD.** `HudElem_UpdateClient` (0x16cf90) copies the script
  HUD elements to the player each frame. Its entry jumps to a copy that sends
  none. Timers, kill counters, objective lines, hints and fades stay off
  screen. The zombies HUD is drawn by the overlay.
- **Objectives.** The twelve `objective_*` builtins return at once. No
  objective markers, compass stars or "objective updated" messages.
- **Radio lines.** `so_dialog_play` (every Spec Ops radio line) is entered
  with `OP_End`, like the end-of-mission functions.
- **Soldier talk.** Every MW2 dialogue alias plays at volume 0: radio lines,
  battle chatter, face and pain lines, scripted level dialogue. They are found
  by file (everything under `voiceovers/`) and by alias name (battle chatter
  `<country>_<n>_...`, `fb_`, `generic_`, `so_`, `arcademode_`). Scripts that
  wait for a line to end still get their notify. BO1's own sound lists are
  never touched.

Everything is restored when the next level loads.

**In the log:** `ZOMBIE_QUIET patches=1 active=13/13`, `ZOMBIE_QUIET
dialogue_lists=N aliases_muted=N`.

## Performance

- The player features (god mode, FOV, the weapon rows) were re-read every
  16 ms, whatever was enabled. With 24 zombies that cost about 2 ms each time,
  with peaks of 28-39 ms. Now: every 16 ms only when a feature is on, 50 ms
  while the menu is open, 250 ms otherwise.
- The overlay keeps one D3D state block for the device. It no longer creates
  and frees one every frame.
- The hang watchdog counts stalls only while MW2 is in the foreground.

**In the log:** `FRAME_PERF frames=N average_ms=.. fps=.. slow_20ms=N
slow_50ms=N slow_100ms=N longest_ms=..`, and `ZOMBIE_PERF` with the time spent
per frame on navigation, spawning, actors, gibs and powerups.

## Zombies below the floor

Every 0.7 s, a zombie that is not attacking is checked: a trace down its own
column, from 40 units above its feet to 4 below. If a floor is found more
than 10 units above its feet twice in a row, it is put back on that floor
(`forceteleport`).

**In the log:** `ZOMBIE_UNSTUCK entity=N lifted=..` (the first 16), and
`unstuck=N` in the zombies report.

## BO1 effects

Test46 converts ten of BO1's effects from your `common_zombie.ff` into MW2
effects. They are added to the generated zombie zone (`bo1_zombie_models.ff`).

| Effect | Used for |
|---|---|
| `misc/fx_zombie_powerup_on` | The green glow on a powerup (on `tag_origin`) |
| `misc/fx_zombie_powerup_grab` | The flash when you pick one up |
| `maps/zombie/fx_zombie_dog_lightning_buildup` | The electric build-up where a hellhound arrives |
| `maps/zombie/fx_zombie_dog_lightning_spawn` | The lightning strike 1.5 s later |
| `maps/zombie/fx_zombie_dog_eyes` | The hellhound's glowing eyes (on `tag_eye`) |
| `maps/zombie/fx_zombie_dog_fire_trail` / `_ash_trail` | Its trail: fire (half of them) or ash |
| `maps/zombie/fx_zombie_dog_explosion` | The burst when it dies |
| `maps/zombie/fx_mp_zombie_hand_dirt_burst` / `_body_dirt_billowing` | The dirt where a zombie climbs out |

**How the conversion works.**

- **The elems.** Spawn, life, velocity and visual-state curves, sort order and
  lighting are copied field by field. Both games keep the same layout up to
  the rotation, and the curve samples are identical structures.
- **T5-only fields** (rotation axis, wind, spawn sound, billboard pivot) are
  dropped.
- **Elem types.** Rotated sprites become billboards. A tail or line without
  velocity becomes a non-uniform billboard. A still line much longer than it
  is wide is dropped. Models, sounds, decals, trails and runners are dropped.
- **Flags.** Only the bits both games use the same way are kept.
- **Materials.** Each sprite material becomes an MW2 effect material:
  - the same colour map, read from your `.iwd` archives;
  - its blend (add, blend or screen) on MW2's own effect technique set, the
    zfeather one when the BO1 material feathers;
  - its feather and tint constants;
  - MW2's effect state bits.
- **References.** A Black Ops zone refers to an asset loaded earlier by a
  stream offset, not by name. Those offsets were resolved once, offline, for
  your `common_zombie.ff`. The mod only carries the resulting names: which
  material each such visual uses and which image each such material uses. A
  visual without an entry is dropped, never guessed.

**Checks.** The whole generated zone (24 models and the 10 effects: 83 elems,
35 materials) was loaded with MW2's own x64 loader on Linux
(`DB_LoadXFile` and every model, material, image and effect loader under it),
with every field compared. Effects are registered with `G_EffectIndex` while
the level initializes, and played with MW2's `playfx` / `playfxontag`. Before
`playfxontag`, the mod checks the entity has a model and the tag, with the
same natives the builtin uses: the builtin would otherwise stop the level
with a script error.

**In the log:** `BO1_MODELS state=READY ... hellhound=1 effects=10
effect_elems=83 effect_elems_dropped=8 effect_materials=35 effect_images=N`,
`BO1_FX precache=10/10`, and `ZOMBIE_FX ... played=N on_tag=N refused=N
loop_sounds=N` in the zombies report.

## Powerups

- **No marker flash.** The overlay's name marker is never drawn for a drop
  whose model is still being made.
- **The glow.** BO1's `powerup_on` effect on the model.
- **The hum.** BO1's `zmb_spawn_powerup_loop`, now built as a looping sound
  and played with `playloopsound`.
- **The grab flash.** BO1's `powerup_grab` effect where you pick it up.
- **The wobble.** The rotation is BO1's `powerup_wobble`: a new random
  attitude every 2.5-5 s, the next one started before the last ends. The
  zombies report now counts the rotations (`wobbles=N`) so the next test can
  tell whether it runs all the time.

## BO1 hellhounds

**Anywhere.** Hellhounds no longer need a map with MW2 dog spawners. On a
dog round each one arrives at a zombie spawn point near you, chosen like a
zombie's.

**The arrival** (BO1's `dog_spawn_fx`):

1. The electric build-up at the spot, with `zmb_hellhound_prespawn`.
2. 1.5 s later: the lightning strike, `zmb_hellhound_bolt`, and a short
   earthquake (`Earthquake(0.5, 0.75, origin, 1000)`).
3. The hound appears with `zmb_hellhound_spawn`.

**The hound.**

- **Model.** Kino's zombie wolf (`zombie_theater.ff`, 58 bones) with its own
  textures. The fur cards are left out: they are alpha-tested in Black Ops and
  MW2's model material would draw them as solid sheets.
- **Eyes and trail.** The eyes glow (`tag_eye`). Half the hounds leave a fire
  trail with `zmb_hellhound_loop_fire`; the others an ash trail with
  `zmb_hellhound_loop_breath`.
- **Animations.** BO1's dog animations (`zombie_dog_run`, `_idle`,
  `_attackidle`, `_run_stop`, `_run_attack`, `_run_attack_low`).
- **Behaviour.** A hound is an MW2 actor made from a zombie template, held
  crouched. MW2's animscripts then pick their crouch run, crouch idles and
  stance change, and on a zombies map those leaves carry the dog animations.
  No zombie ever crouches.
- **Attacks.** It runs at you and bites with BO1's attack animations. The bite
  lands at the animation's `dog_melee` notetrack, for BO1's 40 damage.
- **Death.** It bursts into fire and ash (`zmb_hellhound_explode`) and its
  body disappears.

Without the wolf model, or if the crouch leaves could not be swapped, dog
rounds fall back to Test45's behaviour: MW2's dogs where the map has dog
spawners, otherwise a normal round.

**In the log:** `ZOMBIE_DOG_ROUND round=N ... hellhounds=BO1`,
`ZOMBIE_SPAWN ... dog=1 hound=1`, `ZOMBIE_HOUNDS ready=1 wolf=1 leaves=3
arrivals=N strikes=N dressed=N deaths=N`.

## Zombies climbing out

BO1's `zombie_rise_burst_fx`: a dirt burst where a zombie starts to climb
out, and 0.25 s later the billowing dust.

## Limits

- Effects use MW2's own effect shaders, so they look close to BO1's, not
  identical.
- The hellhound's footsteps are silent (MW2 has no dog step sounds for a
  human actor).
- Hounds use the crouch leaves of MW2's human animation tree. Campaign allies
  kept on a zombies map (Campaign allies: KEEP) would play the dog run when
  they crouch-run.

## Verification (this build)

- 27 native C++ test programs, including the new `test46_tests`. It covers the
  alignment fix, the update cadence, dialogue classification, the floor rule,
  the hellhound bite, IWI bitmap formats and the effect conversion on a
  synthetic Black Ops zone. With your zones, it also converts every effect and
  carves the hellhound. The local Kino pipeline test in `bo1_model_tests` now
  expects the hellhound too.
- The Python checks (`Test46Tests`), including the locally gated ones against
  MW2's `common.ff` and scripts: every hound leaf exists in MW2.
- The exact-native probes: the generated zone through MW2's loaders (above),
  the BO1 dog animations through MW2's root-motion sampler (`zombie_dog_run`:
  724 units in 2 s), the sound zone (only the powerup hum and the two
  hellhound loops carry the looping flag), and the bindings verifier's new
  Test46 section. All 30 native suites, probes and audits pass.
- Details: `docs/zombies-test46-build-verification.json`.
