# Test44: maps that crashed, spawns that never arrived, and cleaner zombies

Version **0.1.43-zombies-test44**. The models and sounds are as in Test43.

## What Test43 showed

The owner's Test43 run (results `20261028-175959`):

| Map | What happened | Why |
|---|---|---|
| Favela, Terminal | "Exceeded limit of 1350 'loaded_sound' assets" while loading | The BO1 sounds (147 of them) pushed MW2's sound pool over its fixed size |
| Contingency, Bomb squad | Played well for 8 rounds, but no BO1 models or sounds | The crash guard held them back for one run after the Favela crash, as designed |
| Gulag showers | Nobody arrived; later "Missing tag [j_helmet] on entity [862] (body_complete_enemy_drone_a)" | Every zombie spawned where no path reached the showers. One was made from a Gulag "drone" spawner |
| Contingency | Zombies swung at the player from several metres away | The swing started inside BO1's 200-unit charge range |
| Contingency | Zombies sometimes ran like MW2 soldiers for a second | MW2's turn, stairs and sideways-walk anims were still soldier anims |

## The sound pool

MW2 keeps every loaded sound in a fixed pool of 1350 entries. Favela and
Terminal already use most of it, so the 147 BO1 sounds did not fit.

**The fix.** The pool is enlarged to 4096 entries before the game starts:

- MW2's asset pools are free lists. Two `.data` tables name each type's pool
  and its size.
- `DB_Init` links every pool once at startup, using each type's own routine.
- From `DllMain`, before `DB_Init` runs, the mod points the loaded_sound
  entries of both tables at a new, zeroed pool of 4096 entries (256 KB).
  `DB_Init` then links it like any other pool.

The mod checks everything first and changes nothing if any check fails. It
checks:

- the table entries;
- the init, allocate and free routines;
- `DB_Init`'s pool loop and its one caller;
- that the pools are not linked yet.

No code or data addresses the old pool directly; the verifier scans the whole
code section for references. The log reports how full the pool is on every
level:

```
ASSET_POOLS level=... loaded_sound_in_use=N capacity=4096 expanded=1
```

**Checked here.** A new exact-native probe (`tools/asset_pool_probe.cpp`) runs
the retail `DB_Init` on the enlarged pool. Then it checks:

- 4096 entries are linked in order;
- the retail allocator hands out 4096 distinct entries and then reports the
  pool full;
- the retail free routine takes them all back;
- the 28 other pools are linked as before.

## Spawn points the zombies can walk from

Test41 recycled a zombie after 12 s without a path. In Gulag showers nearly
every enemy spawner is in another wing of the prison, so zombies rose,
waited, were recycled and rose somewhere else unreachable, round after round.

**The fix.** The mod now reads MW2's own navigation graph from the level:

- the path nodes and the links between them, with the link lengths the AI
  uses;
- only links that are currently connected, so a closed door counts as closed.

Every 5 s it re-reads the links, so doors that open or close are noticed.
From the node nearest the player, it finds the walking distance to every
node. The search follows links backwards, so one-way drops are handled
correctly.

Each spawn point is tied to the nearest node beside it that is in clear view.
A zombie spawns only where:

1. that node leads to the player;
2. the walking distance is 350–2500 units (dogs: 300–1200);
3. the spawn point is at least 250 units away in a straight line.

If fewer than 3 spawners qualify, zombies may also rise at walkable path
nodes 400–1800 units away on foot. Up to 16 nodes are drawn at random from
path, cover, exposed and concealment nodes; doors, traversals and turrets are
left out. BO1 zombies climb out of any floor, so this keeps rounds going on
levels whose spawners are all out of reach. These spawn points are numbered
from 0x8000 in the log.

If there are still too few candidates, the band widens. If the level has no
usable graph, the old straight-line choice is used.

**What this fixes:**

- **Gulag showers:** zombies rise in the wing the player is in.
- **Favela:** roofs no zombie could leave are not used.

**In the log:**

- `ZOMBIE_NAV build=... nodes=... usable=... links=... disconnected=...`
- `ZOMBIE_NAV player_node=... reachable_points=R/P`
- `ZOMBIE_SPAWN ... path=...` gives each zombie's walking distance.
- `ZOMBIE_REACH ... by_path=... by_line=...`

**The graph layout.** It was read from the engine's own code:

| Part | Size or offset | Fields |
|---|---|---|
| node | 0xa8 bytes | type at +0, origin at +0x14, total links at +0x38, links at +0x40, active links at +0x6c |
| link | 0xc bytes | cost at +0, target node at +4, disconnect count at +6 |

The engine's own A* walks only the first "active links" of a node. A link
that is disconnected (a closed door) is moved past that count.
`tools/verify_zombie_bindings.py` checks all of this against the
executable. `tests/nav_graph_tests.cpp` checks the reader and the distances
on a synthetic graph:

- a corridor;
- a roof reachable only by a one-way drop;
- a closed door;
- bad links.

## No more drone zombies

Gulag places "drone" spawners whose complete-body model has no helmet bone,
although the character still sets a hat model. A head shot or a grenade made
MW2's `helmetPop()` look for `j_helmet` and end the level.

Zombies are no longer made from these templates:

- drones;
- riot shields;
- civilians;
- juggernauts;
- hostages.

If a level has nothing else, the full list is used.

## Swings only in reach

BO1 lets a zombie start an attack within 200 units, because the BO1 engine
first runs the zombie at its target (a "charge"). MW2 has no charge, so the
attack played where the zombie stood, and it swung at the air.

A swing now starts only within 64 units (BO1's `meleeAttackDist`) and within
48 units of the player's height. The swing still breaks off if the player
leaves 96 units, and each "fire" notetrack still hits only within 72.

## Fewer MW2 soldier anims

- **Turn anims.** Zombies now also run MW2's `disable_turnAnims`, so
  `move.gsc` never plays the soldier `run_turn_*` anims when a path bends.
- **More locomotion slots.** Eight more of MW2's locomotion anims now play
  BO1 zombie anims (76 slots in all; the menu row shows `A76/76`):

  | MW2 anim | BO1 anim |
  |---|---|
  | `run_cqb_f_search_v2` | `ai_zombie_walk_fast_v1` |
  | `traverse_stair_run`, `traverse_stair_run_down_01` (walkers on stairs) | `ai_zombie_walk_v1`, `ai_zombie_walk_v2` |
  | `traverse_stair_run_01`, `traverse_stair_run_down` (runners on stairs) | `ai_zombie_run_v2`, `ai_zombie_run_v3` |
  | `walk_left`, `walk_right`, `walk_backward` (blended in while a turning zombie's facing lags its motion) | BO1 walks |

- **Run attacks moved.** Two of the BO1 run attacks lived on MW2's
  running-bullet-reaction anims, which `run.gsc` can still play. They moved
  to the backward run-and-gun blends (`run_n_gun_l_120`, `run_n_gun_r_120`),
  which only an AI with an enemy to shoot at plays. Zombies have no gun and
  ignore everyone.

## Checks done here

- **Native tests:** 25/25, including the new `nav_graph_tests`.
- **Python tests:** 59/59, including the owner's local `common.ff` check that
  every slot anim exists, and the MW2 script check for `disable_turnanims`.
- **Binding verifier:** the Test44 section (asset pools and navigation graph).
- **Native suites and probes:** all, plus the new asset-pool probe.

## Not covered offline

- Whether 4096 is enough on every map. The `ASSET_POOLS` line will show it.
- How the spawn choice plays on each map.
- The in-game look of the stairs and sideways anims.

**Testing:** no Windows gameplay test of Test44 yet.
