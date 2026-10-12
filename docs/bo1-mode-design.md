# BO1 mode startup, drops, helpers and purchases

Updated 2026-10-12 from the owner's requirements and owned, locally extracted
base/patch scripts. This is the implementation contract and staged work; test13
does not enable these gameplay features. Patch scripts override matching base
paths. [bo1-mechanics-test13-evidence.json](bo1-mechanics-test13-evidence.json)
records exact source hashes/function line anchors without distributing scripts.
Existing rounds, health, AI, animation and format audits remain in
[bo1-portability.md](bo1-portability.md).

## Intended start and end

Start in a loaded campaign map, remove existing enemy/friendly actors from
active play, suppress campaign replacement spawns, equip only **USP .45**, and
start normal solo round **1**. Keep the menu available for cheats/debug settings
and helper spawning. The USP is the owner's chosen MW2 weapon substitution;
it is not BO1's M1911 starting asset. Require the loaded USP variant and a valid
inventory transaction; report a missing dependency before partially starting.

Map cleanup must distinguish actor entities from players, corpses, spawners,
vehicles, objectives, triggers and protected mission actors. Snapshot campaign
spawn gates and player inventory first. Disable encounter refill before cleanup
and use native removal on the engine thread. Removing a mission-critical actor
can end or break campaign scripting. Audit the map's progression/failure threads
and provide a tested per-map play-area configuration before calling a map
supported. Do not promise that deleted campaign state can be reconstructed.

End removes mode-owned zombies/helpers/drops/interactions, cancels callbacks and
timers, restores owned player/dvar edits, then requires mission restart if the
campaign was cleared. Checkpoint recovery follows the explicit mode policy;
session-only generation ownership cannot safely invent saved actor provenance.

**Scheduling prerequisite:** the current service executes after an existing
native actor think callback. Clearing all actors would stop that service. Find
and independently verify a server/game-frame hook or a deliberately allocated,
validated controller entity before implementing clear/start. The scheduler must
also survive player death, map transitions and empty waves. Rendering remains
separate from actor/VM work.

## Behavior and population

Use the accepted source-derived normal-solo round controller and count confirmed
mode-owned zombie lifetimes only. A failed spawn does not spend its obligation;
death, cleanup, corpse conversion and requeue are distinct events. Preserve BO1
inter-round delays, health arithmetic, pacing and capacity policy. The current
IW4 actor pool has 32 simultaneous native slots; BO1's ordinary 24-zombie cap
leaves at most eight slots for helpers/other native actors after campaign cleanup.
Arbitrarily many active helpers require a separately tested pool expansion.

First connect one BO1 actor fixture: source target filtering/acquisition,
path/goal pursuit, attack admission, animation selection, damage on the `fire`
notetrack, `stop`/`end` continuation, pain and death. Import animations/models/
audio only through typed local tools after exact target schema/loader validation.
An unarmed MW2 soldier with a periodic damage timer does not meet this contract.

Helper NPCs are an added compatibility feature. They should be valid targets for
zombies, fight zombies, enter a downed state, and accept a held-use revive.
Keep the human player count at one for solo round formulas by default; helpers
do not silently change BO1's human-count scaling. Audit BO1 AI-target branches
and add a separate helper-target adapter rather than pretending NPCs are T5
player entities.

## Recovered mechanics and dependencies

| Mechanic | Owned patched source | Confirmed rule and limits |
| --- | --- | --- |
| Drop setup | `_zombiemode_powerups:init`, lines 20–33 | Common defaults: score increment 2000, max 4 successful ordinary drops per round. Final map overrides remain to audit. |
| Score-driven drop eligibility | `watch_for_drop`, 523–552 | Initial threshold uses human starting score plus the increment. Every 0.5 seconds, sum player `score_total`; on strict threshold crossing multiply the increment by 1.14, update threshold and arm the drop flag. This needs an earned-score ledger before faithful drops, even if the visible points UI is deferred. |
| Ordinary drop creation | `powerup_drop`, 615–704 | Obey mutator, per-round budget and included pool. Random integer 0..99 values 0..2 bypass the armed-score gate; other values require it. Reserve the budget before a yielding spawn, validate playable volumes, roll back invalid placement, run setup/timeout/grab and consume the flag. Rare-drop overrides are separate. It is not a uniform fixed chance on every kill. |
| Pickup lifetime | `powerup_timeout`, 1626–1674 | Once entered, waits 15 seconds, then 40 flashes: 15×0.5s, 10×0.25s, 15×0.1s; nominal total **26.5 seconds** before deletion. Grab/death cancels the thread. This is derived timing for this patched function; setup delays and map overrides are separate. |
| Max Ammo | `full_ammo_powerup`, 1807–1834 | Skip players in last stand; GiveMaxAmmo for owned weapons. Clip-refill code is commented out, so do not fill magazines. Equipment/notifications need their own adapter. |
| Double Points | `double_points_powerup`, 1792–1805 | Cancel the prior named timer, set point scalar 2 for 30 seconds, then restore 1. Repeated pickup replaces the running timer. Needs actual scoring events and HUD/notification translation. |
| Insta-Kill | `insta_kill_powerup`, 1836–1855 | Cancel the prior timer, set the mode flag for 30 seconds, then clear and notify players. Translate actual zombie damage branches; the flag alone is not a native IW4 cheat. |
| Nuke | `nuke_powerup`, 1677–1755 | After a 0.5s flash, collect valid source actors, honor custom damage/shields/marked state, then use staged 0.1–0.7s delays and damage/death semantics. Submit a 400-point event per human player through the scoring multiplier path. It is not equivalent to Delete All. |
| Revive settings | `_zombiemode`, 779–780; `_laststand:revive_trigger_spawn` | Common setup chooses radius 75 and bleedout dvar 45 seconds. Check map/perk overrides before final configuration. |
| Revive progress | `_laststand:revive_do_revive`, 642–751 | Default 3 seconds, Quick Revive halves it to 1.5. Poll at 0.05s; interruption clears progress/animation/HUD appropriately. T5 `startrevive`/`stoprevive` and player checks need an NPC compatibility adapter. |
| Bleedout | `_laststand:Laststand_Bleedout`, 366–427 | Tick the configured countdown every second, then defer the final bleedout while actively being revived. Separate downed, reviving, revived and dead states; do not equate health zero with immediate removal. |
| Scoring | `_zombiemode_score:player_add_points`, 11–140 | Damage/ADS/death/bonuses/revive/nuke are different events; round to source score units and apply the current multiplier. Score balance and earned-score total serve different purposes. Bot damage needs an explicit policy. |
| Weapon pricing | `_zombiemode_weapons:add_zombie_weapon`, 28–76 | CSV costs/ammo prices override fallbacks. Missing ammo price falls back to half the weapon cost, rounded with the source helper. Resolve the owned table before claiming BO1 prices; MW2 weapon mapping is a deliberate substitution. |
| Wall interaction | `_zombiemode_weapons:weapon_spawn_think`, 2649+ | Validate player/buy state/points, charge weapon or ammo price, use weapon-give helpers and look-at triggers. Upgrade, hacked-buy, grenade and special-weapon branches need separate support. |

Registration of a powerup/model in the common script does not prove it is in a
particular map's ordinary drop pool. Audit `include_zombie_powerup`, map callbacks,
grab selection, Carpenter, Fire Sale and special-round guarantees before enabling
each. Carpenter requires functioning barricades; Fire Sale requires the box.
Do not include unavailable mechanics as silently inert drops.

## Purchases on MW2 maps

Use our own per-map JSON anchors for wall buys and playable/spawn areas. A later
in-game placement tool should capture a collision-checked position/orientation
from the player's view and save only coordinates, weapon keys and pricing rules.
Use native look-at/use interaction and an in-game price prompt; until imported
presentation works, use a clearly documented MW2 marker/material substitute.
Reload anchors with map/version validation and reject unknown weapons/invalid
coordinates. Do not require a mystery-box implementation just to buy a weapon.

## A / B / C split and implementation order

| Class | Work |
| --- | --- |
| A: direct source behavior | Round/health formulas, drop eligibility/budget/timers, scoring decisions, revive progress/bleedout transitions, purchase debit rules. Retain source evaluation order and timer cancellation. |
| B: translate engine interfaces | Frame scheduler, cleanup/spawn gating, USP inventory setup, zombie targets/nav/traces, damage/notetracks, pickup entities/touch detection, HUD/use prompts, helper downing/revive, event/score routing and wall-buy interaction. Exact IW4 fields/APIs are independently verified. |
| C: compatibility/substitution | T5 player last-stand implementation applied to NPC helpers, T5 actor/VM layouts, direct T5 FF/asset loading, missing IW4 animation/material/audio/dismemberment features, BO1 weapon-to-MW2 mapping and absent campaign-map barricades. Keep each limitation explicit. |

1. Accept test13 native placement/team/goal/cleanup and hand-definition evidence.
2. Verify independent server scheduling, mode ownership and a protected map setup;
   test suppression/cleanup/USP-only startup separately from live waves.
3. Validate one real BO1 behavior/animation/damage/death fixture against owned BO1.
4. Connect round one and source-derived wave accounting/pacing to that adapter.
5. Add the score-event ledger needed by ordinary drops and Double Points, then
   implement one powerup at a time with pickup/timer/death tests.
6. Translate downed helpers/held-use revival and add validated wall-buy anchors.
   Expose points/purchase UI with those interactions.
7. Implement Mystery Box last, including its own source-derived selection/movement
   and Fire Sale dependencies. Special rounds follow separate source audits.

Original assets, scripts and audio remain local. Git and the delivered ZIP contain
our code, tooling, hashes, derived findings and tests; no original game payloads.
