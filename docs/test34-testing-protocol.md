# Test34 install and retest

Build: **0.1.33-squad-test34**. Goal: stable to 256 actors on Estate and
Contingency, including checkpoint reloads, with no engine error dialogs.

## Install

1. Close MW2. Extract the complete Test34 ZIP into a new folder.
2. Run **Uninstall.cmd** from the Test34 folder (removes Test33).
3. Run **Install.cmd** from the Test34 folder.

## Run A: Estate to 256

Fresh Estate through Mission Select. Soldier, World Speed 1.0x, Freeze AI OFF,
Invincible Spawned Friendlies ON, explosives/rapid fire OFF.

Spawn to 128, 179 (the Test33 wall), 192, 224 and 256. Hold about 30 seconds
at each and select **AI info to log** at each stage.

## Run B: Contingency to 256

Same settings on fresh Contingency, same stages (111 was the Test33 wall).

## Run C: checkpoint reload with a crowd

At the highest stable count on either map, cause a checkpoint reload
(friendly fire, or die). It should reload cleanly. Repeat once more.

## If spawning stops before 256

That is the new resource guard, not a crash. The log names the reason in
`AI_RESOURCE_GUARD refused=…`. Please note the actor count and send the logs.
`ENTITIES` would mean the map's entity table (a hard engine limit) is nearly
full.

## Send back

Run **CollectLogs.cmd** after any crash or error dialog, or after finishing.
Please also write down any dialog text exactly. Useful log lines:

- `SCRIPT_VM enabled=1 …` near the top. It must say `enabled=1`; anything
  else means the expansion did not apply.
- `SCRIPT_VM_USAGE children=…/409592 … parents=…/36856`
- `SCRIPT_MEMORY strings_tree_nodes=…/65536 pointer_heap_bytes=…`
- `ENGINE_LIMITS entities=…/2046 checkpoint_cursor=… dobj_transfers=…`
- `AI_ANIM_INFO used=… peak=…` (capacity 16,383)
