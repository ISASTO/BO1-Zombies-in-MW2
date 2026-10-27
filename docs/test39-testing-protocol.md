# Test39 install and test

Build **0.1.38-zombies-test39**: zombies on Spec Ops maps, with BO1 zombie
animations read from your own Black Ops install and BO1's zombie setup.

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
- **The menu row.** The **BO1 animations** row shows `BO1 68/68 SO 3`:
  every animation applied, and the 3 Spec Ops end paths disabled.
- **Rising.** Zombies climb out of the ground at the map's enemy spawn
  points. Walkers take up to 10 s to rise; runners and sprinters are quicker.
- **No rifles.** About a third of a second after a zombie appears, its rifle
  is removed. Zombies do not talk (no MW2 battle chatter).
- **Moving.** They walk, run, sprint and idle with BO1 zombie animations.
  They still have an MW2 soldier's body.
- **Attacking.** They stop and swing at you with one of BO1's attacks. Each
  swing in the animation is a separate hit. If you back away, they stop
  swinging and follow you.
- **Dying.** They fall with BO1 death animations and then ragdoll. They can
  be killed while rising or swinging.

Then:

1. Play to round 4 or 5 so you see runners and sprinters, and walk around
   the whole area, including where the Spec Ops mission would normally end.
   No "Mission complete" or "Mission failed" screen should appear.
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
- **Rifles.** Were the rifles gone?
- **Animations.** Did the walk, run, sprint, attack and death animations look
  right? Did any zombie freeze in a pose?
- **Mission screens.** Did any Spec Ops end screen appear?
- **Errors.** Any dialog text, exactly as shown.

Useful log lines:

- **Startup:**
  - `BO1_ASSETS state=READY found=27/27 …`
  - `LEVEL_PREP bindings=1 anim_builtins=4/4`
  - `ZOMBIE_SETUP thread_calls=1 functions=10`
  - `LEVEL_PREP hook_installed=1`
- **Map load:**
  - `LEVEL_SCRIPTS_LOADED map=so_… zombies_launch=1 setup_functions=10/10`
  - `SO_GUARD function=… -> OP_End` (3 lines)
  - `BO1_ANIM swap=68/68 … special=17/17`
  - `LEVEL_PREP map=… so_guards=3/3 anims=68`
- **In play:**
  - `BO1_ANIM tree=… script_tree=… special_leaves=17/17`
  - `ZOMBIE_SPAWN … rise=1`
  - `ZOMBIE_PREP … rises=… animated_attacks=… setup_zombies=… setup_failures=0`
- **After death:** `ZOMBIE_RELAUNCH …`
