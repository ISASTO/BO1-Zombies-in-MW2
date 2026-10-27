# Test39: Spec Ops maps and the first BO1 animation import

Version **0.1.38-zombies-test39**. It builds on Test38: capacity stays at 256
actors / 274 sentients, and the squad-follow fix is unchanged.

## 1. Zombies on Spec Ops maps

Test38 played zombies on whole campaign levels. Spawn points could be far
apart, and zombies stuck on long paths were recycled by the failsafe. The
Test38 Favela log shows this happening often.

Each Spec Ops mission is a campaign location cut down to a small play area,
with its own enemy spawners inside that area. Zombies now play on those maps.

### The map list

The **Spec Ops map** row offers 19 missions:

| Location | Missions |
| --- | --- |
| Training | Training pit |
| Pripyat | Hidden |
| Contingency | Forest, Rooftops |
| Bridge | Crossing, Demolition |
| Favela | Favela, Juggernauts, Rooftops (Bomb Squad) |
| Invasion | Invasion, Homeland Security |
| Arcadia | Wardriving |
| Airport | Terminal |
| Gulag | Showers |
| Boneyard | Boneyard |
| Estate | Estate |
| Oil rig | Deck, Wetwork |
| Cliffhanger | Base |

Four missions are left out:

- **AC-130** and **helicopter gunner**: these are co-op only.
- **The two snowmobile races**: these put the player in a vehicle.

### How a map is loaded

The mod loads a map with the same command sequence the game's own Spec Ops
menu uses (from `ui.ff`):

```
set specialops 1; set solo_play 1; set so_nofail 1; set start "";
set ui_load_so_level <map>; set coop_mapName <map>; devmap <map>
```

This works from the main menu. If **This level** is selected while a Spec Ops
map is loaded, that map is loaded again so it can be prepared (see below).

### Stopping the mission from ending the game

A Spec Ops mission ends the level through three script functions:

- `maps/_specialops::fade_challenge_out` (success)
- `maps/_specialops_code::specialops_mission_over_setup` (shared success and
  failure setup: freezes the player and deletes every AI)
- `maps/_specialops_code::failure_summary_display` (failure)

Without a guard, any of these would end a zombies game: completing an
objective, a wave counter running out, a timer expiring, or leaving the play
area.

The guard works while the level's scripts are still being linked. The DLL
redirects the tail jump at the end of `GScr_LoadScripts` (`0x1953f8`). At
that point the compiler's file and function table still exists, so
`Scr_GetFunctionHandle` (`0x216920`) can find each function. The first
opcode of each function is then replaced with `OP_End`. In this build:

- the VM dispatches opcodes from a byte table of 0x8c entries;
- entry 0 is `OP_End`, which unwinds to the caller's code position.

So each function returns immediately and the end-of-mission screens never
appear. Objectives, HUD and dialogue otherwise stay as they are.

Dying gives the zombies GAME OVER. After 9 seconds the same Spec Ops map
loads again for a new game. F4 → Start cancels the reload.

## 2. BO1 animations, imported from your own Black Ops

### Where the animations come from

The animations are read at run time from the owner's own
`Black Ops\zone\Common\common_zombie.ff`. Nothing from BO1 is in the mod or
the repository.

The installer finds the folder and writes its path to
`bo1mw2\bo1_path.txt`. It looks in these places, and asks if none of them
has the file:

- next to MW2
- the Steam libraries (BO1 is app 42700)
- common game folders

When the game starts, a background thread:

1. reads the 51 MB fastfile;
2. inflates it with the bundled miniz (MIT);
3. carves out the 27 BO1 animations the mod uses.

This takes well under a second.

### Why the conversion is almost a copy

The compressed animation data has the same layout in IW3, IW4, IW5, T4, T5
and T6. That data is the per-bone byte, short, int and random pools plus the
frame indices; OpenAssetTools reads all six games with one reader. Two checks
confirm it for these files:

- `include/bo1_xanim.hpp` consumes the pools exactly as the engine does. All
  1,882 animations in `common_zombie.ff` pass, as do 600 animations from
  MW2's `common.ff`.
- The x64 MW2 `XAnimParts` layout (136 bytes) was confirmed against MW2's own
  zone stream, and against the native readers: numframes `0x12`, notify
  `0x78`, notifyCount `0x1f`, framerate `0x2c`. The delta records' union sits
  at `+8`.

So the BO1 channel data is carried over byte for byte. Only three things are
rewritten for MW2:

- **The asset header**: flags, counts and pointers.
- **The root-motion (delta) records**: BO1's yaw-only delta becomes MW2's
  `quat2`.
- **The bone and notetrack names**: these become MW2 script strings via
  `SL_GetString`. BO1 sound notetracks (`sndnt#…`) are dropped because MW2
  has no such aliases.

The zombie skeleton uses MW2's bone names. Of the 108 bones in a BO1 walk,
104 exist in MW2's soldier skeleton.

### How the animations are applied

When a zombies launch loads its map, the MW2 animations zombies play are
replaced in place, 68 of them in all. Each original 136-byte header is saved
and restored when the next level loads, before any actor of that level
exists. A header is restored only if it is still the one the mod wrote.

All 68 MW2 names were checked against the owner's MW2 `common.ff`: each is
an `XAnimParts` there (the optional `MW2_COMMON_DEC` test repeats the check
locally).

**What MW2 plays → the BO1 animation**

| MW2 animation (animscripts) | BO1 animation |
| --- | --- |
| `walk_cqb_f` (walk) | `ai_zombie_walk_v1` |
| `walk_forward` | `ai_zombie_walk_v2` |
| `run_lowready_f` (run) | `ai_zombie_run_v2` |
| `run_cqb_f_search_v1` | `ai_zombie_walk_fast_v1` |
| `heat_run_loop` | `ai_zombie_run_v3` |
| `sprint_loop_distant` (sprint) | `ai_zombie_sprint_v1` |
| `sprint1_loop` (short sprint) | `ai_zombie_sprint_v2` |
| standing idles (5) | `ai_zombie_idle_v1` |
| `death.gsc` deaths (39: standing, extended, running, shotgun/sniper) | `ai_zombie_death_v1` / `ai_zombie_death_v2` |

Explosion deaths stay MW2's, which throw the body like BO1's explosive
deaths.

**Rise and attack slots**

BO1's rise and attack animations have no MW2 counterpart, so each one takes
over a leaf of every level's `generic_human` tree that a zombie never plays
itself. That lets `animscripted` name it.

| MW2 leaf (why a zombie never plays it) | BO1 animation | Used for |
| --- | --- | --- |
| `exposed_flashbang_v1` (no flashbangs) | `ai_zombie_traverse_ground_v1_walk` | rise, walker, version 1 |
| `exposed_idle_twitch` (surprise disabled) | `ai_zombie_traverse_ground_v2_walk_alta` | rise, walker, version 2 |
| `exposed_flashbang_v2` | `ai_zombie_traverse_ground_v1_run` | rise, runner |
| `exposed_flashbang_v3` | `ai_zombie_traverse_ground_climbout_fast` | rise, sprinter |
| `exposed_flashbang_v4` / `v5` | `ai_zombie_attack_v1` / `v2` | `_zombie_melee` |
| `grenade_return_*` (3; grenadeawareness 0) | `ai_zombie_attack_v4`, `v6`, `attack_forward_v1` | `_zombie_melee` |
| `exposed_squat_down_grenade_f` | `ai_zombie_attack_forward_v2` | `_zombie_melee` |
| `exposed_squat_idle_grenade_f`, `exposed_dive_grenade_b`, `exposed_idle_reacta`/`b` | `ai_zombie_walk_attack_v1`–`v4` | `_zombie_walk_melee` |
| `exposed_dive_grenade_f`, `run_react_duck`, `run_react_flinch` (bullet reactions disabled) | `ai_zombie_run_attack_v1`–`v3` | `_zombie_run_melee` |

### Rising out of the ground (`do_zombie_rise`)

Every new zombie plays the BO1 rise for its speed, as in
`_zombiemode_spawner::do_zombie_rise`:

- Walkers use version 1 or 2 at random; runners and sprinters always use
  version 1.
- The animation starts under the spawn spot (45 units for version 1, 14 for
  version 2), facing the player.
- The zombie is hidden for the first 0.5 s, like BO1's `hide_pop`.
- When the animation has ended, `stopanimscripted` makes sure the actor has
  left scripted mode (a no-op if it already has).

The `%anim` value is built natively. The DLL:

1. reads the actor's anim tree (`0x19fd90`);
2. finds the replaced leaf in its `XAnim_s`;
3. takes the script animtree index (`0x20b0e0`);
4. pushes the result with `Scr_AddAnim` (`0x222070`, `VAR_ANIMATION`) into the
   native `animscripted`.

While a zombie is rising it does not chase, attack, or count towards the
failsafe.

### Attacks (`zombie_melee::MeleeCombat`)

The attack decision is unchanged from Test38. Once a zombie attacks:

- It picks at random from BO1's `_zombie_melee` set (6 attacks) plus the
  walk set (4) for walkers or the run set (3) for runners and sprinters,
  exactly as `pick_zombie_melee_anim` does.
- Every `fire` notetrack in that animation is one hit check (`self melee()`
  in BO1): `attack_v2` swings five times over 7.2 s, `attack_v4` once.
- The engine ends BO1's melee animscript when the target leaves melee range.
  Here the swing is broken off (with `stopanimscripted`) once you are more
  than 96 units away, after the first 250 ms; the zombie then chases again.
- Without the import, the Test38 timing is used instead.

## 3. BO1's zombie setup, through MW2's own scripts

BO1's `zombie_spawn_init` turns a spawned AI into a zombie. Most of what it
sets lives in script variables, which native code cannot write directly. But
MW2's `maps/_utility` has a function for nearly each of them. The DLL runs
those functions on every zombie, the way the engine runs its own callbacks:

- `Scr_GetFunctionHandle` finds each function while the level's scripts link
  (the same hook as the Spec Ops guard, now installed at startup so every
  level's handles are known);
- `Scr_ExecEntThreadNum` (`0x2226f0`) runs it as a thread on the zombie and
  `Scr_FreeThread` (`0x222830`) releases it. `animscripted` itself runs
  `animscripts\scripted::init` exactly this way (`0x183365`/`0x18336d`).

| BO1 `zombie_spawn_init` | MW2 function |
| --- | --- |
| no weapon (zombie character) | `gun_remove()` |
| `disableArrivals`, `disableExits` | `disable_arrivals()`, `disable_exits()` |
| `a.disablepain` | `disable_pain()` |
| no long death | `disable_long_death()` |
| `disable_react()` | `disable_surprise()`, `disable_bulletwhizbyreaction()`, `disable_danger_react()` |
| `chatInitialized = false` | `set_battlechatter(false)` |
| sprint run cycle | `enable_sprint()` for sprinters |

They run 300 ms after the spawn, once the actor's animscripts are set up and
while it is still hidden underground. In addition, `allowdeath` (actor field
`0x165`) is set at spawn so a zombie can die while rising or swinging, as
BO1 sets it.

## Physics

BO1 zombies does not change player movement: its `_zombiemode.gsc` only sets
fire damage, revive and last-stand dvars. MW2 and BO1 share the same player
speed and jump values, so nothing is changed. Zombie movement speed comes
from the imported animations' root motion, so walkers, runners and sprinters
now move at BO1's speeds. Deaths ragdoll at BO1's own `start_ragdoll`
notetracks.

## Not in this build

**Models.** Zombies still have an MW2 soldier's body and head. The BO1
zombie characters are not in `common_zombie.ff`. Kino der Toten's
`zombie_theater.ff` (also transferred and checked against its CRC) holds
them: `c_ger_honorguard_body1`/`body2` with heads `c_ger_zombie_head1`–`4`,
plus the gib pieces. Their textures are in BO1's `.iwd` image archives, not
in the zone. Importing them means converting the model, its surfaces and
skinning, and building MW2 materials and Direct3D textures from BO1's
images. That is the next milestone.

**Effects and sounds.** There are no dirt or rise effects, and no BO1 zombie
sounds (BO1's sound notetracks are dropped).

**Animation variety.** One animation per MW2 slot for the whole level, so
every walker uses the same walk. BO1 picks one of 8 walks per zombie.

**Spec Ops HUD.** The Spec Ops timer, kill counter and objective text still
show.

**Testing.** No Windows gameplay test has been done yet.

## Validation

**Fixtures**

`bo1_xanim_tests` uses a synthetic BO1 zone with every part type. It checks:

- the header and delta reads;
- exact pool consumption;
- that a corrupted stored size is rejected;
- the x64 image layout and its pointers.

With `BO1_ZONE_DEC` set it also carves and rebuilds the real animations
(local only). All 27 BO1 animations the mod uses carve cleanly from the
owner's `common_zombie.ff`.

`bo1_zombie_rules_tests` adds the attack hit times from `fire` notetracks,
the break-off rule and the rise offsets.

**Suites**

All 22 C++ and 46 Python tests pass (one runs only where the owner's MW2
`common.ff` is available locally; it passed here). The Python tests cover:

- the Spec Ops map list;
- the three Spec Ops end functions;
- the animation slot table: one donor per BO1-only role, BO1's melee set
  sizes, and (locally) every MW2 name present in `common.ff`;
- the setup-function table.

**Exact-native probes**

[level-prep-test39-probe.json](level-prep-test39-probe.json) runs the
`tools/level_prep_probe.cpp` code against the retail code, mapped at its
preferred base. It confirms:

- `DB_FindXAssetEntry` finds a converted header (case-insensitively).
- `XAnimGetLength` and `XAnimIsLeafNode` read the converted header.
- The animtree index helpers resolve the tree.
- An anim pushed with `Scr_AddAnim` passes `Scr_GetAnim`'s tree check.

[script-call-test39-probe.json](script-call-test39-probe.json) adds thread
calls: arguments pushed with the exact `Scr_AddInt`, received in script
order, the thread freed and the stack restored. The thread routine itself is
a stand-in that applies `VM_Execute`'s parameter accounting, which the
binding verifier checks byte for byte; running a compiled thread needs the
live VM.

**Binding verification**

[zombie-bindings-test39.json](zombie-bindings-test39.json) checks:

- the loader tail jump;
- `Scr_EndLoadScripts` releasing `scriptsPos`;
- the program buffer references;
- the VM dispatch and `OP_End`;
- the asset hash and entry layout;
- `SL_GetString`;
- `Scr_AddAnim` and `Scr_GetAnim`;
- the `animscripted` argument layout;
- the `XAnim_s` readers;
- the four new method entries;
- `Scr_ExecEntThread`/`Scr_ExecEntThreadNum`/`Scr_FreeThread`, their use by
  `animscripted`, and `VM_Execute`'s parameter accounting;
- the `allowdeath` actor field.

**Native suites**

All earlier exact-native suites and audits still pass: runtime, resources,
iterator, animation, history, awareness, threat, crowd, fracture,
glass_state, tables, layout, lifecycle, actor audit, consumer audit,
script_vm, capacity sweep, script_parameters, capacity, fire_mode and
owned_strings, plus the script_call, level_prep and binding checks above.
The DllMain patches and generated capacity tables are unchanged from Test38.
