# Test38: squad following, and the first playable zombies mode

Version **0.1.37-zombies-test38**. It builds on Test37, so actor capacity
stays at 256 actors / 274 sentients.

## 1. Friendlies standing around in clumps

Test37 reached 242 friendlies on Estate. The owner reported that "huge clumps
of the spawned friendlies will just sort of stand around instead of engaging
or actually following me."

### Why it happened (Test37 code)

| Cause | Effect |
| --- | --- |
| Only 4 spawned actors were serviced per server frame. | With ~240 friendlies, each one was revisited about every 3 s. |
| A friendly's follow goal was refreshed only when it had no target or was more than 640 units from the player. | Any enemy within 2,500 units left nearby friendlies standing on an old goal. |
| Follow goals were 12 fixed slots, 112 units around the player. | Hundreds of actors shared 12 points, which made the clumps. |

### Fix

**Native goal entity.** Our allies now use the player as their native goal
entity, which is what campaign scripts do with `setgoalentity(level.player)`.
The engine copies the player's position into the effective goal on every
think (the read at `0x12b89c`), so following no longer waits for our service.
The setgoalentity routine is replicated exactly, minus its script-parameter
reads:

1. Clear claimed-node and arrival requests (`0x1244b0`).
2. Set the handle through the native entity-handle setter (`0x15b190`).
3. Zero the goal node and goal volume.

The native `setgoalentity` method and the native position-goal routine both
call this setter, and both are checked at startup.

**Goal radius.** The radius is 384 to 1,024 units, growing with squad size:
240 allies get about 750 units instead of 12 shared points. When an ally has
an enemy, the radius stretches toward that enemy (up to 1,536 units), so the
engine's own cover and combat movement takes allies out of the clump and into
the fight.

**Flags and goals.** Campaign color scripts can pin an actor in place
(`fixednode`) or make it non-combatant (`pacifist`); both are cleared on our
allies. Allies never get position goals, because those would clear the goal
entity. Hostiles keep the Test37 advance-on-target behaviour.

**Throughput.** Up to 32 actors are serviced per server frame within a
1.5 ms budget (was 4), so 256 actors are revisited in about half a second.

**Log lines.**
- `AI_FOLLOW mode=GOAL_ENTITY_PLAYER goal_entity_assigns=… radius_updates=…`
- `AI_PERF … max_npcs_per_tick=32 … last_batch=… peak_batch=…`

## 2. Playable zombies: first attempt

### How to play

1. Press **F4**, then go to **ZOMBIE MODE → Playable zombies**.
2. Set the options:
   - **Map**: "This level", or any of the 19 campaign maps. Choosing a map
     loads it with the game's own `spmap` command, so it also works from the
     main menu.
   - **Starting round**: 1 to 100.
   - **Hellhounds**: on or off.
   - **Campaign allies**: REMOVE or KEEP.
3. Press **ENTER** on Start. The menu closes and the game begins once the
   level is ready.

Select Start again (END) to stop. Dying ends the game: "GAME OVER – You
survived N rounds".

### What is ported, and from where

The BO1 sources are the owner's local `common_zombie_patch.ff` scripts. They
are extracted with `tools/extract_bo1_scripts.py` into `local-data/`, which
is never committed. The portable rules live in `include/bo1_zombie_rules.hpp`
with fixtures, and the native adapter is in `src/ai.cpp`. The tables below
use these short source names:

| Short name | Source script |
| --- | --- |
| `_zombiemode` | `maps/_zombiemode.gsc` |
| `_zombiemode_spawner` | `maps/_zombiemode_spawner.gsc` |
| `_zombiemode_score` | `maps/_zombiemode_score.gsc` |
| `_zombiemode_powerups` | `maps/_zombiemode_powerups.gsc` |
| `_zombiemode_ai_dogs` | `maps/_zombiemode_ai_dogs.gsc` |

**Setup**

| Feature | BO1 source | In Test38 |
| --- | --- | --- |
| Campaign AI | — | Campaign enemies (and allies, unless KEEP) are deleted every second. Campaign spawners are closed with the native `ai_disableSpawn`; it opens for one native call at each zombie spawn. |
| Starting loadout | — | The design contract: USP .45 only, via native builtins `takeallweapons`, `giveweapon`, `setweaponammostock` (80) and `switchtoweapon`. Another loaded pistol is used if the level has no USP. |
| Grenades | `_zombiemode::award_grenades_for_survivors` | Frag grenades: 2 at the start, then up to 4 each round (0→2, 1→3, else 4). |

**Rounds and spawning**

| Feature | BO1 source | In Test38 |
| --- | --- | --- |
| Rounds | `_zombiemode` round logic (from Test5/Test13) | Normal solo counts and health, 24 at once, spawn delay 2 s ×0.95 per round, intro 8.25 s / 2.5 s, 10 s break. |
| Spawn points | `_zombiemode::round_spawning` | A random campaign enemy spawn point 400–2,500 units from the player. If the same point comes up twice, it re-draws once. |

**Zombie behaviour**

| Feature | BO1 source | In Test38 |
| --- | --- | --- |
| Zombie setup | `_zombiemode_spawner::zombie_spawn_init`, `zombie_setup_attack_properties` | Round health, no pain reactions, no grenades, no dropped weapons, goal radius 32, path/melee distances 64. |
| Move speed | `_zombiemode_spawner::set_run_speed`, `_zombiemode::zombie_speed_up` | Walk, run or sprint from `round×8 + random(35)`, driven by the native `walkdist`. The last zombie sprints after round 3. |
| Pursuit | `_zombiemode_spawner::find_flesh`, `zombie_follow_enemy` | `SetGoalPos(player)` on BO1's cadence: 0.1 s, plus 0.5–3 s when far away. |
| Attack | `animscripts/zombie_melee.gsc` | BO1's melee admission rule (Test13 port), then 60 damage through native `G_Damage` (`MOD_MELEE`) at the attack's "fire" point. |
| Stuck zombies | `_zombiemode::round_spawn_failsafe` | 30 s without moving 24 units (and not attacking) recycles the zombie into the queue. |

**Points and powerups**

| Feature | BO1 source | In Test38 |
| --- | --- | --- |
| Points | `_zombiemode_score` | 500 to start and 10 per hit. Kills give 50 plus a bonus: head 50, neck 20, torso 10, knife 80. Values are rounded to 5 and doubled by Double Points. |
| Powerup drops | `_zombiemode_powerups::watch_for_drop`, `powerup_drop` | Armed at 2,000 points earned, then ×1.14 each time. 4 per round, plus a 3% free drop. |
| Powerup types | `_zombiemode_powerups::get_valid_powerup`, `zombie_theater` | Kino's randomized cycle. Carpenter and Fire Sale are skipped because a campaign map has no barriers and no box. |
| Pickup | `_zombiemode_powerups::powerup_grab`, `powerup_timeout` | Collected within 64 units. They disappear after 26.5 s, blinking at the end. |
| Effects | `_zombiemode_powerups` | Kaboom kills everything alive (0.1–0.7 s apart) and gives 400 points. Insta-Kill and Double Points last 30 s. Max Ammo applies `givemaxammo` to every weapon. |

**Hellhounds and HUD**

| Feature | BO1 source | In Test38 |
| --- | --- | --- |
| Hellhounds | `_zombiemode_ai_dogs` | First dog round at 5–7, then every 4–5 rounds. 6 dogs (8 from the third dog round), health 400/900/1300/1600 ×4, at most 2 alive, BO1's spawn waits, and Max Ammo at the last dog. They are the map's MW2 dogs with MW2 dog attacks, so a map without a dog spawner plays that round with zombies. |
| HUD | — | The round number is red at the bottom left, with points at the bottom right. Active powerups have timers. Dropped powerups are marked where they lie. Round, powerup and game-over messages use the native `iprintlnbold`. |

### How the native side works

Everything runs on the server clock, inside the idle script-VM work frame
used since Test19. Four mechanisms do the work:

- **Native builtins.** These are called the way the VM calls them: arguments
  are pushed with the native `Scr_Add*`, the builtin runs, and the native
  zero-object notify releases the stack. `tools/script_call_probe.cpp` runs
  this sequence against the exact retail `Scr_AddInt`, `Scr_AddFloat`,
  `Scr_GetFloat`, `Scr_GetInt`, `min`, `max`, `gettime` and the notify
  cleanup.
  - **Arguments** arrive in script order.
  - **Return values** are read and then released.
  - **Busy or invalid** VM states are refused.
  - **1,000 repeated calls** stay balanced.

  In this build, a native script error outside a running script returns
  without doing anything (`0x2226a0`). Inputs are still validated first.
- **Zombie spawning** reuses the Test34/35 owned-actor factory with shared
  helper spawners, so zombies count against the same 256-actor guard and
  load shedding.
- **Points** come from the existing native damage hook. Player damage to a
  zombie applies Insta-Kill and BO1's grenade and explosive bonus damage,
  then scores the hit and the kill.
- **The map launch** uses the native command buffer (`Cbuf_AddText`, critical
  section 0x24) with the same `spmap %s` command the mission flow uses.

`tools/verify_zombie_bindings.py` checks every field offset, builtin, call
site and enum against the SHA256-gated executable.

### Not in this first attempt

- **Models and animations.** There is no crawl-out-of-the-ground rise and no
  BO1 zombie animations or models. Zombies are the level's own soldiers: they
  walk or run at you holding their rifle and never fire it. Attacks have no
  swing animation; the hit lands about 0.45 s after the attack starts. All of
  this needs the BO1 animation and model import, which is the next large
  milestone.
- **Powerup visuals.** Powerups have no 3D model or glow; the HUD marks them.
- **Missing BO1 systems.** There are no barriers, wall buys, perks, Mystery
  Box, Pack-a-Punch or Quick Revive. Solo death is game over (BO1 solo
  without Quick Revive).
- **Helpers.** Helpers are not downed or revived yet. Allies spawned from the
  AI page still fight zombies, but they earn no points.
- **Main menu.** There is no main-menu button yet. The F4 menu works on the
  main menu and loads the map.
- **Level scripts.** These keep running: objectives, triggers, cutscenes and
  autosaves. Removing allies may stop scripts that wait for them. If a map
  misbehaves, try KEEP.
- **Testing.** No Windows gameplay test has been done yet.

## Validation

- **Fixtures.**
  - New `bo1_zombie_rules_tests`:
    - score values
    - move-speed thresholds
    - pursuit cadence
    - powerup drop arming, cycle, limits and lifetime
    - dog-round tracker, counts, health and waits
    - planned dog round through the controller (2 alive, 2 s poll, 8 s
      aftermath)
    - queue cancellation
  - Updated:
    - `ai_behavior_tests` (squad radius and batch budget)
    - `core_tests` (zombies page routing)
  - All 21 C++ and 42 Python tests pass.
- **Exact-native builtin-call probe**: [script-call-test38-probe.json](script-call-test38-probe.json).
- **Binding verification**: [zombie-bindings-test38.json](zombie-bindings-test38.json).
- **Unchanged suites.** All earlier native suites, audits and the capacity
  sweep are rerun unchanged (see the build verification record).
