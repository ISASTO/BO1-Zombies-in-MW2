# Test40 install and test

Build **0.1.39-zombies-test40**: zombies wear the BO1 Kino der Toten zombie
models, built from your own Black Ops install. Everything else is Test39.

## Install

1. Close MW2 and extract the complete Test40 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test40 folder. This removes Test39.
3. Run **Install.cmd** from the Test40 folder.

The installer should print two lines:

- `Black Ops found: …`
- `Kino der Toten found: BO1 zombie models are built from it the first time
  MW2 starts.`

If it asks for the Black Ops folder, paste
`D:\Games\Call of Duty - Black Ops`.

## Run A: Training pit from the main menu

1. Start MW2 Single Player and wait on the main menu for about 10 seconds.
   The models are built the first time; later starts reuse them.
2. Press **F4** → ZOMBIE MODE → Playable zombies. The **BO1 anims / models**
   row should show `READY 27/27 M READY`.
3. Pick **Training pit**. Leave Starting round on 1 and Hellhounds OFF, then
   press ENTER on Start.

What should happen:

- **Loading.** The map loads as in Test39. The row then shows
  `A68/68 M6 SO3`.
- **Look.** Zombies rise out of the ground in German honor-guard uniforms
  with BO1 zombie heads, not MW2 soldiers. You should see:
  - two body styles, one more common than the other;
  - four different heads;
  - no MW2 helmet, rifle or gear.
- **Animations.** Rising, walking, attacking and dying look as in Test39.
- **Shooting.** Shooting them shows blood. Heads and bodies stay together
  when they die and ragdoll.

Then:

1. Look at a zombie up close, then at a few far away, so the distant
   versions (LODs) are used.
2. **Please take screenshots** (Steam's screenshot key, usually F12):
   - one zombie close up, front and side;
   - a group from a distance.
3. Play for at least a minute, to round 2 or 3.
4. Select **Zombies info to log** once.

## Run B: other maps and a restart

1. Try **Favela** and **Gulag showers**. Check that the models look the same
   in their lighting.
2. Quit MW2 normally, start it again and play one more map. The model build
   should now be reused (`cached=1` in the log), so the menu row shows
   `M READY` right away.

## If something goes wrong

- **MW2 crashes on loading or soon after.** Start MW2 again and launch
  zombies again. The crash guard skips the models, so zombies wear MW2
  soldiers and the row shows `M-SKIPPED`. Run CollectLogs.cmd and send the
  zip; that run is what tells me what went wrong. Delete
  `bo1mw2\cache\bo1_zombie_models.loading` (or rerun Install.cmd) to try the
  models again.
- **An error dialog appears.** Write down its text exactly.
- **The row shows `M MISSING` or `M FAILED`.** Zombies keep MW2 soldiers.
  Send the logs; the `BO1_MODELS` line says why.

## Send back

Run **CollectLogs.cmd** once at the end. Also send:

- **Screenshots** of the zombies, close up and far away.
- **Look:** Do the bodies and heads look right? Are any textures plain grey,
  black, pink, overly shiny, or stretched? Do the heads sit on the necks?
- **Movement:** Do any parts stretch or spike when zombies move or die?
- **Visibility:** Did any zombie turn invisible, especially far away?
- **Errors:** any dialog text, exactly as shown.

## Useful log lines

**Startup**

- `BO1_MODELS state=READY cached=0 textures=8 fallbacks=0 … detail=built models=6 …`
- `BO1_MODELS hook_installed=1`

**Map load**

- `BO1_MODELS level_zone=<map zone> append=1 state=READY …`
- `BO1_MODELS precache=1 bodies=4 heads=4 … helmet_swap=1`
- `LEVEL_PREP map=… so_guards=3/3 anims=68 bo1_models=8 zone_loaded=1`

**In play**

- `BO1_MODELS confirmed=1 …` (after 45 s with dressed zombies)
- `ZOMBIE_MODELS state=READY skipped=- … dressed=… dress_failures=0 helmet_swaps=…`
