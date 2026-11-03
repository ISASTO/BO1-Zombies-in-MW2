# Test47: powerup glow, hellhound fixes, Spawn Zombie

Version **0.1.46-zombies-test47**. Everything else is as in Test46.

## What Test46 showed

From the owner's Test46 run (log `latest.log`, 2026-11-03 14:33–14:41, Favela, rounds 1–7):

- the quiet map, the effects and the hellhounds all worked;
- what was left is listed below.

| Asked for | Cause found in the log and the zones | Done in Test47 |
|---|---|---|
| Powerups do not glow (only Insta-Kill does) | `ZOMBIE_FX ... on_tag=14 refused=5`: 7 drops; only the 2 Insta-Kills played their glow. The glow plays on `tag_origin`. Only the skull's single bone is `tag_origin`; the others are named `zombie_ammocan`, `x2_poly` and `zombie_bomb`, so `playfxontag` was refused. | Each powerup's single bone is named `tag_origin` when the model is built (builder `bo1mw2-models-5`, so the cache is rebuilt once) |
| Hellhounds go rigid near you instead of biting | Their bite was placed on `exposed_crouch_shoot_auto_v2`, which MW2's `generic_human` animtree keeps under `add_fire : additive`. An animation there is applied as an additive layer, so the wolf stayed in its bind pose. In the log, 13 bites started and 8 were broken off. | The bite moves to `crouch_grenade_throw`, a full-body leaf. Every hound leaf is now checked against the animtree; none is additive. |
| Hellhounds sometimes use a zombie animation | A dying hound played MW2's soldier death and the swapped zombie deaths. The exposed crouch alert idles were also additive. | A dying hound is removed (below). More crouch leaves carry dog idles: the crouch aim base, the crouch turns and the crouch twitches. Hounds are held crouched every frame. |
| The eye glow sits a foot or two off the face | The eyes started in the same frame as the model swap and were bolted by bone. BO1 bolts them to `J_EyeBall_LE`. | The eyes and trail start 0.3 s after the wolf model is set, on `j_eyeball_le` (falling back to `tag_eye`) |
| The fire stays on a dead hound | The dying actor was only hidden; the trail kept burning on it. | Like BO1's fire dogs (`self delete`): the burst and explosion sound play on their own entity, then the hound is deleted along with everything bolted to it |
| A "Spawn Zombie" button | — | F4 → AI → **Spawn Zombie (zombie mode)**: one more zombie rises in the round in progress (a hellhound on a dog round) |
| Spawning friendlies in zombie mode | Zombie mode keeps MW2's native spawn gate closed (`ai_disableSpawn`), so the AI page answered PAUSED | **Spawn Friendly** opens the gate for its own spawn while zombies run |

## Spawn Zombie

F4 → AI, row 3:

- **While zombie mode runs**, one more zombie is added to the current round. It spawns like the others, rises at a spawn point near you, and must be killed before the round ends. On a dog round it is a hellhound.
- **Between rounds**, the row shows NEXT ROUND and nothing is added (the next round's counts replace the current ones).
- **Without zombie mode**, it shows OFF.

**In the log:**

- `ZOMBIE_MENU_SPAWN added=N round=R total=T queued=Q dogs=D`;
- `menu_zombies=N` on the `ZOMBIE_HOUNDS` line.

## Friendlies in zombie mode

- **Spawn Friendly** works while zombies run; Spawn Enemy stays closed, showing ZOMBIES.
- The ally follows you and shoots zombies. Its kills count for the round but give no points.
- **Limit:** MW2's soldier animations are replaced by BO1's for the whole level, so an ally walks and runs with the zombie animations and crouch-runs like a hound. This is the same for campaign allies kept with **Campaign allies: KEEP**.

**In the log:** `AI_SPAWN_FRIENDLY zombies=1 gate=BYPASS`.

## Hellhounds, in detail

**Leaves.** There are now 94 animation slots (88 in Test46). Every hound slot is under a non-additive node of `generic_human`. The hound's leaves:

| MW2 leaf | BO1 animation |
|---|---|
| `crouch_fastwalk_F/L/R/B` | `zombie_dog_run` |
| `casual_crouch_idle`, `casual_crouch_twitch`, `exposed_crouch_idle_twitch_v2`, the four `exposed_crouch_turn_*` | `zombie_dog_idle` |
| `exposed_crouch_aim_5`, `casual_crouch_point`, `exposed_crouch_idle_twitch_v3` | `zombie_dog_attackidle` |
| `casual_crouch_idle_in`, `exposed_stand_2_crouch` | `zombie_dog_run_stop` |
| `crouch_grenade_throw` (bite) | `zombie_dog_run_attack` (`dog_melee` at 20%) |
| `exposed_crouch_reload` (bite) | `zombie_dog_run_attack_low` |

**Death.** In order:

1. Stop the eyes and the trail, and stop the loop sound.
2. Play `fx_zombie_dog_explosion`, with `zmb_hellhound_explode` on a script_origin that is deleted after 5 s.
3. Delete the hound.

**In the log:** `ZOMBIE_HOUNDS ... deaths=N removed=N`.

## Limits

- **Climbing.** A hound that climbs or jumps (mantles, windows, ledges) still plays MW2's soldier traversal. Those animations are shared by every actor, and zombies need them.
- **Death animations.** Every hound bursts on death; BO1's ash hounds played a death animation.

## Verification (this build)

- **Native C++:** 28 test programs, including the new `test47_tests`, which covers:
  - Spawn Zombie: added during the intro, while spawning and while draining; refused between rounds and when stopped; round counts stay consistent;
  - with the owner's `common_zombie.ff`: all four powerup models carry `tag_origin`.
- **Python:** `Test47Tests`. The local check against the owner's MW2 `common.ff` extracts `animtrees/generic_human.atr` and confirms that no hound leaf sits under an additive node.
- **Native suites:** the full set of suites, probes and audits, plus the installer fixture Test46 → Test47.
- **Details:** `docs/zombies-test47-build-verification.json`.
