# Test38 install and test

Build **0.1.37-zombies-test38** does two things:

- **Friendly following.** Spawned friendlies follow you and engage instead of
  clumping.
- **Zombies, first attempt.** This is the first playable zombies mode.

## Install

1. Close MW2. Extract the complete Test38 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test38 folder. This removes Test37.
3. Run **Install.cmd** from the Test38 folder.

## Run A: friendlies (Estate)

1. Spawn 100–240 friendlies near you and walk around the map.
2. Check that they follow you as a loose crowd, not a standing clump.
3. Fight some enemies, or spawn enemies from the AI page. Check that
   friendlies near the fight move toward it and shoot instead of standing
   around.
4. Select **AI info to log** once while the crowd is following.

## Run B: zombies on the current level

1. Load **Loose Ends (Estate)** from Mission Select.
2. Press F4 → ZOMBIE MODE → Playable zombies. Leave Map on *This level*,
   Starting round 1, Hellhounds OFF, Campaign allies REMOVE.
3. Press ENTER on Start.

What should happen:

- **Start.** Campaign soldiers disappear. You are left with a USP .45 and
  frags, and "ROUND 1" appears.
- **Round 1.** About 6 zombies come at you from enemy spawn points. They are
  Estate's soldiers walking at you without firing. Close up they attack:
  about 60 damage a hit, so 2 hits takes you down at normal health.
- **Points.** These show bottom right: 500 to start, +10 per hit and +50–130
  per kill.
- **Rounds.** After each round there is a 10 s break, then the next round has
  more zombies with more health. From round 5 some of them run.

Then:

1. Play several rounds. Once you have earned about 2,000 points, a powerup
   should drop where a zombie died. It is marked on screen with its name and
   distance; walk over it.
2. Select **Zombies info to log**, then die on purpose. You should see GAME
   OVER.

## Run C: zombies launched from the main menu

1. From the main menu, press F4 → ZOMBIE MODE → Playable zombies.
2. Choose **Map: contingency** with LEFT/RIGHT, and set Hellhounds ON.
3. Press Start. The map loads and zombies begin by themselves.

Contingency is the map most likely to have dog spawners, so check the
**Hellhounds** row for ON rather than "ON (NO DOGS)". Then:

1. Play to round 5–8 for a hellhound round, or start at round 5 with
   Hellhounds ON. Its first dog round comes 5–7 rounds after the start.
2. Try one or two other maps from the list too, such as *arcadia* or *gulag*.

## Send back

Run **CollectLogs.cmd** once at the end. For each map, write down:

- **Zombies.** Whether they reached you, whether they got stuck, and how the
  attacks felt.
- **Points and powerups.** Whether the points and powerups behaved as
  described.
- **Errors.** Any dialog text, exactly as shown.

Useful log lines:

- **Bindings:**
  - `ZOMBIE_BINDINGS ready=1 builtins=11/11 cbuf=1 goal_entity=1` (near the top)
  - `AI_FOLLOW goal_entity_binding=1`
- **Game flow:**
  - `ZOMBIE_START …`, `ZOMBIE_ROUND …`, `ZOMBIE_SPAWN …`
  - `ZOMBIE_LOADOUT pistol=… grenade=…`
  - `ZOMBIE_POWERUP_DROP/GRAB …`
  - `ZOMBIE_END reason=…`
- **Friendlies:** `AI_FOLLOW mode=GOAL_ENTITY_PLAYER …`
