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
3. carves out the 16 BO1 animations the mod uses.

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
replaced in place. The tables below show which ones. Each original 136-byte
header is saved and restored when the next level loads, before any actor of
that level exists. A header is restored only if it is still the one the mod
wrote.

**What MW2 plays → the BO1 animation**

| MW2 animation (animscripts) | BO1 animation |
| --- | --- |
| `walk_cqb_f` (walk) | `ai_zombie_walk_v1` |
| `walk_forward` | `ai_zombie_walk_v2` |
| `walk_lowready_f` | `ai_zombie_walk_v4` |
| `run_lowready_f` (run) | `ai_zombie_run_v2` |
| `run_cqb_f_search_v1` | `ai_zombie_walk_fast_v1` |
| `heat_run_loop` | `ai_zombie_run_v3` |
| `sprint_loop_distant` | `ai_zombie_sprint_v1` |
| `sprint1_loop` | `ai_zombie_sprint_v2` |
| standing idles (5) | `ai_zombie_idle_v1` |
| standing deaths (17) | `ai_zombie_death_v1` / `ai_zombie_death_v2` |

**Rise and attack slots**

The five flashbang reactions (`exposed_flashbang_v1`–`v5`) carry the rise and
attack animations. Zombies are never flashed, and these reactions are in
every level's `generic_human` tree, so they can be played with
`animscripted`.

| Flashbang slot | BO1 animation | Used for |
| --- | --- | --- |
| `exposed_flashbang_v1` | `ai_zombie_traverse_ground_v1_walk` | rise (walker) |
| `exposed_flashbang_v2` | `ai_zombie_traverse_ground_v1_run` | rise (runner) |
| `exposed_flashbang_v3` | `ai_zombie_traverse_ground_climbout_fast` | rise (sprinter) |
| `exposed_flashbang_v4` | `ai_zombie_attack_v1` | attack |
| `exposed_flashbang_v5` | `ai_zombie_attack_v2` | attack |

### Rising out of the ground (`do_zombie_rise`)

Every new zombie plays the BO1 rise for its speed, as in
`_zombiemode_spawner::do_zombie_rise`:

- The animation starts 45 units under the spawn spot, facing the player.
- The zombie is hidden for the first 0.35 s, like BO1's `hide_pop`.

The `%anim` value is built natively. The DLL:

1. reads the actor's anim tree (`0x19fd90`);
2. finds the replaced flashbang entry in its `XAnim_s`;
3. takes the script animtree index (`0x20b0e0`);
4. pushes the result with `Scr_AddAnim` (`0x222070`, `VAR_ANIMATION`) into the
   native `animscripted`.

While a zombie is rising it does not chase, attack, or count towards the
failsafe.

### Attacks (`zombie_melee`)

The attack decision is unchanged from Test38. Once a zombie attacks:

- It plays a BO1 attack animation facing the player, and stays in place.
- The hit lands at that animation's own `fire` notetrack: 46% of 1.8 s for
  `attack_v1`.
- Without the import, the Test38 timing is used instead.

## Not in this build

**Models**

- Zombies are still the map's MW2 soldiers.
- They still hold a rifle, because MW2's animscripts attach and detach the
  weapon model themselves.

Importing the BO1 zombie models and their materials and textures (from
`zombie_theater.ff` and others) is the next milestone.

**Rise effects**

- There are no dirt or rise effects.
- There are no BO1 zombie sounds.

**Animation choice**

- One animation per MW2 slot for the whole level, so every walker uses the
  same walk. BO1 picks one of 8 walks per zombie.
- The sprint speed class uses the run animation: MW2 only sprints when a
  script sets `self.sprint`.

**Spec Ops HUD**

The Spec Ops timer, kill counter and objective text still show.

**Testing**

No Windows gameplay test has been done yet.

## Validation

**Fixtures**

New `bo1_xanim_tests` uses a synthetic BO1 zone with every part type. It
checks:

- the header and delta reads;
- exact pool consumption;
- that a corrupted stored size is rejected;
- the x64 image layout and its pointers.

With `BO1_ZONE_DEC` set it also carves and rebuilds the real animations
(local only).

**Suites**

All 22 C++ and 44 Python tests pass. The Python tests cover:

- the Spec Ops map list;
- the three Spec Ops end functions;
- the animation slot table.

**Exact-native probe**

[level-prep-test39-probe.json](level-prep-test39-probe.json) runs the
`tools/level_prep_probe.cpp` code against the retail code, mapped at its
preferred base. It confirms:

- `DB_FindXAssetEntry` finds a converted header (case-insensitively).
- `XAnimGetLength` and `XAnimIsLeafNode` read the converted header.
- The animtree index helpers resolve the tree.
- An anim pushed with `Scr_AddAnim` passes `Scr_GetAnim`'s tree check.

**Binding verification**

[zombie-bindings-test39.json](zombie-bindings-test39.json) adds these checks:

- the loader tail jump;
- `Scr_EndLoadScripts` releasing `scriptsPos`;
- the program buffer references;
- the VM dispatch and `OP_End`;
- the asset hash and entry layout;
- `SL_GetString`;
- `Scr_AddAnim` and `Scr_GetAnim`;
- the `animscripted` argument layout;
- the `XAnim_s` readers;
- the four new method entries.
