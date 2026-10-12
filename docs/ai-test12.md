# Native AI controls and weapon repairs, test12

Build: **0.1.11-ai-test12**. The Windows DLL builds without warnings; 11 portable
C++ suites, 22 Python fixtures and 77 exact-build signatures pass. Independent
method tables, native field accesses, teams, caller/callee targets and entity
allocation counters agree. These are static and portable checks. The first
retail execution of the new actor calls is pending.

The incoming test11 estate run confirms checkpoint bank recovery and the BO1
controller rehearsal. It also confirms that dual never became ready. The old
name validator inspected unused animation slot 0 and rejected optional empty
names. The actual native builder starts at 1 and substitutes idle[1] for an empty
name; the new validator follows that rule. This is a confirmed source mismatch,
but the old log did not identify which gate blocked each gun. The new log records
`DUAL_BLOCK` with a reason if another gate still fails.

## Installation

1. Close MW2. Extract **BO1MW2-ai-test12.zip** outside the game folder.
2. Run **Uninstall.cmd**, then **Install.cmd**, selecting the folder containing
   `iw4sp.exe` through Steam's Installed Files → Browse. Use the entire package.
3. Launch normally through Steam. **Restart the mission once** so a checkpoint
   saved by a previous process with added guns is not used as an original baseline.
4. Enter player control, press **F4**, then use Up/Down, Enter, Left/Right and
   Backspace/Esc. The menu stays in-game and does not pause MW2.

The supported file is the supplied September 2026 AMD64 Steam executable, SHA256
`fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777`.
Saved FOV is retained. Keep the game EXE, Bink DLL and fastfiles unchanged.
The installer/remover preserves logs/settings and verifies its own DLL receipt.

## First: weapon regressions

Keep Infinite Ammo, No Reload, Rapid Fire and Explosive Bullets OFF initially.

1. Open WEAPONS before touching Give All or its arrows. Its page count should
   already reflect the eligible catalog: in the same estate load that had four
   banks, expect **OFF 1/4**, rather than a temporary 1/1.
2. Give ON, cycle to bank 2, reload a checkpoint, then check the label **before
   pressing Left/Right**. Cycle all banks, Give OFF and check actual inventory.
3. Hold Beretta and then G18/glock. Dual ON should become **ON** for authored
   hand animations or **COMPAT** for the generic normal-animation fallback.
   Check both models, both fire inputs, ammunition, reload and Dual OFF.
4. If it stays WAIT, select that row and report the footer reason and exact gun.
   `MISSING_NATIVE_TREE`, `ANIMATION_NAMES`, `ASSET_FIELDS`, `ASSET_CHANGED` and
   `WRITE_FAILED` identify different failures. Unsupported guns remain gated.
5. Record FPS at a fixed view for ten seconds each: OFF → ON → OFF. Check both
   OFF orders with Give/Dual together, then reset weapon toggles.

The generic fallback reuses validated normal animation definitions in the two
native hand slots. It does not import an authored akimbo animation set. Existing
selective undo and normal-pickup preservation remain in place; history is
in-process. Turn both modes OFF before saving/quitting.

## Then: actual native AI controls

For the first AI test, use a live estate section with ordinary enemy/friendly
NPCs. Keep Freeze AI OFF, Pause AI spawning OFF and the controller rehearsal
ended; isolate AI from the weapon modes for this first test.

| Row | Test and expected observable behavior |
| --- | --- |
| AI info to log | Scroll to the last row and press Enter. The header reports the last request; the log records profile, actor/team counts, selected templates and callback thread. |
| Enemy template | Left/Right cycles up to 32 nearest eligible **loaded mission spawners**. The header shows the selected script classname and distance in game units. NONE/0/0 means no eligible template is currently loaded. |
| Spawn Enemy | Select a nearby template, look away from its spawn area and press Enter once. Expect QUEUED, then CREATED or NATIVE_REFUSED. A successful actor appears at that mission spawn point with ordinary MW2 behavior. Check the owned/live counts after a brief update delay. |
| Remove our spawned actors | Remove the first spawned actor before continuing. Expect DELETED and owned count returning to 0. Existing campaign actors must remain. |
| Friendly template / Spawn Friendly | Repeat once with a loaded friendly template. Team initialization may complete after the native factory returns; observe its behavior and the next count update. |
| Pause AI spawning | ON uses the native spawn gate. Existing actors continue thinking; new debug requests report PAUSED/SpawnPaused. OFF restores our prior gate value when we still own it. |
| Freeze AI (linked) | Matches WORLD Freeze AI. Test briefly and restore OFF. It is separate from the spawn gate. |
| Alive enemy / friendly | Counts validated native axis/allies actor entities with positive health; other teams and corpses are excluded. Not a BO1 zombie count. |
| Our spawned actors | At most 8 current debug actor tickets. This is separate from the normal campaign population. |

Normal collision, visibility, spawn count and actor capacity rules are retained.
Try another nearby template/location if the native factory refuses; this build
does not use forced Stalingrad spawning or evict mission actors to make space.
Hero-named templates and spawners with the native force flag are excluded.
Spent templates can temporarily borrow one spawn budget; guarded restoration
preserves the original budget after the native call. Native script notifications
still run, so debugging a mission spawner can trigger mission callbacks.

The service installs only after a spawn/remove request. It replaces one native
actor think-handler pointer, calls the original first, then services one bounded
command per server timestamp. It requires an existing actor callback: an empty
map locks spawning. It does not call the script VM from D3D9 rendering, steal
instructions or allocate a fake actor structure. Queued requests expire after
10 seconds of game command time and are canceled on detected checkpoint/map/
client/pool changes. Entity number, native allocation serial, type and self-links
protect cleanup from removing a recycled or unrelated entity.

Remove debug actors **before saving, reloading, leaving the mission or quitting**.
Ownership is deliberately discarded across a detected lifetime change and is
not saved to disk, so a previously saved debug NPC will not be automatically
claimed or deleted. Cleanup is removal, not a BO1 death event. Native spawn/
delete failures and script side effects need this first retail test.

## Evidence to return

After the tests, close MW2 and run **Diagnose.cmd before another launch**.
Attach its ZIP and the short [testing checklist](testing.md). Include the
selected classname/location, result label, live/owned counts and any crash's
last action. The log surrounds native calls with `AI_SPAWN_BEGIN`,
`AI_SPAWN_ACCEPT` and `AI_DELETE_BEGIN`; it also records `AI_SERVICE`,
`AI_RESULT`, `AI_STATE`, `DUAL_BLOCK` and `WEAPON_PAGE_PREVIEW`.

## Boundary with BO1 Zombies

These controls implement the real IW4 actor lifecycle foundation. They spawn
normal mission NPCs; they are not represented as BO1 zombies. The accepted BO1
controller remains a separately labeled simulation until this adapter is
validated and BO1-specific pursuit, melee, damage/death and animation bindings
are translated. No zombie model, animation, sound or original script is bundled.
The A/B/C split and conversion prerequisites remain in
[bo1-portability.md](bo1-portability.md). The next live milestone is one validated
zombie behavior/animation fixture, built from owned source data and compared to
BO1, before connecting the round controller to a population.
