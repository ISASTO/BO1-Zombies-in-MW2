# Test39 install and test

Build **0.1.38-zombies-test39**: zombies on Spec Ops maps, with BO1 zombie
animations read from your own Black Ops install.

## Install

1. Close MW2 and extract the complete Test39 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test39 folder. This removes Test38.
3. Run **Install.cmd** from the Test39 folder.

The installer looks for Black Ops (`zone\Common\common_zombie.ff`). If it
cannot find it, it asks for the folder: paste
`D:\Games\Call of Duty - Black Ops`. It should print
`Black Ops found: …`.

## Run A: a Spec Ops map from the main menu

1. Start MW2 Single Player. Stay on the main menu.
2. Press **F4** → ZOMBIE MODE → Playable zombies.
3. On **Spec Ops map**, use LEFT/RIGHT to pick **Training pit**.
4. Leave Starting round on 1 and Hellhounds OFF, then press ENTER on Start.

What should happen:

- **Loading.** The map loads by itself, and after about 3 s "ROUND 1"
  appears.
- **The menu row.** The **BO1 animations** row shows something like
  `BO1 35/35 SO 3`: animations applied, and the 3 Spec Ops end paths
  disabled.
- **Rising.** Zombies climb out of the ground at the map's enemy spawn
  points. Walkers take about 10 s to rise; fast ones are quicker.
- **Moving.** They walk, run and idle with BO1 zombie animations. They still
  carry an MW2 rifle and have an MW2 soldier's body.
- **Attacking.** They stop, swing at you with a BO1 attack, and the hit lands
  partway through the swing.
- **Dying.** They fall with BO1 death animations, or ragdoll.

Then:

1. Play a few rounds and walk around the whole area, including the place
   where the Spec Ops mission would normally end. Check that no "Mission
   complete" or "Mission failed" screen ever appears.
2. Select **Zombies info to log** once.
3. Die on purpose. You should see GAME OVER, then "New game in N s", and the
   map should load again with a new game.

## Run B: other maps

Try two or three more maps, for example **Gulag showers**, **Favela** and
**Invasion (Homeland Security)**.

**Homeland Security** normally ends itself after its waves, so check that it
does not end the game.

To leave, press F4 → Start (it shows END or CANCEL), then ESC → Quit.

## Send back

Run **CollectLogs.cmd** once at the end. For each map, write down:

- **Rising.** Did zombies rise out of the ground? Did they pop up, or get
  stuck under the floor?
- **Animations.** Did the walk, run, attack and death animations look
  right?
- **Mission screens.** Did any Spec Ops end screen appear?
- **Errors.** Any dialog text, exactly as shown.

Useful log lines:

- **Startup:**
  - `BO1_ASSETS state=READY found=16/16 …`
  - `LEVEL_PREP bindings=1 anim_builtins=4/4`
- **Map load:**
  - `LEVEL_SCRIPTS_LOADED map=so_… zombies_launch=1`
  - `SO_GUARD function=… -> OP_End` (3 lines)
  - `BO1_ANIM swap=35/35 …`
  - `LEVEL_PREP map=… so_guards=3/3 anims=35`
- **In play:**
  - `BO1_ANIM tree=… script_tree=… rise=…`
  - `ZOMBIE_SPAWN … rise=1`
  - `ZOMBIE_PREP … rises=… animated_attacks=…`
- **After death:** `ZOMBIE_RELAUNCH …`
