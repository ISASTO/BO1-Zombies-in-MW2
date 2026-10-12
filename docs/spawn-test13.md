# Spawn placement and native hand definitions, test13

Build **0.1.12-spawn-test13**. The incoming test12 estate run confirms real
native enemy creation/deletion and dual compatibility on several weapons.
The owner reports G18 failures, unclear spawn locations, no usable friendly
templates, and occasional collision errors. The eight-owned-actor ceiling was
our limit; the separate physical native actor pool contains 32 slots.

The Windows build, 12 portable C++ suites, 24 Python fixtures, 97 unique exact
executable anchors, independent binding checks, PE checks and actual-test12
installer upgrade pass. New native calls need this retail test. No Windows
game execution is available in the development environment.

## Install

1. Close MW2. Extract **BO1MW2-spawn-test13.zip** outside the game folder.
2. Run **Uninstall.cmd**, then **Install.cmd**, selecting the directory containing
   `iw4sp.exe`. Find it through Steam → MW2 Single Player → Properties →
   Installed Files → Browse. Saved FOV and logs are preserved.
3. Launch through Steam and **restart the mission once**. Wait for player control,
   then press **F4**. Use Up/Down, Enter, Left/Right and Backspace/Esc.

The exact supported AMD64 executable hash remains
`fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777`.
The installer checks that hash and its own payload. Original game files stay
in place. Manual installation copies only `d3d9.dll` beside `iw4sp.exe` after
removing the previous project DLL; keep any unrelated wrapper separately.

## First: G18 and undo

Use a live estate combat section with existing actors. Keep Freeze AI OFF,
spawn pause OFF, and Infinite Ammo/No Reload/Rapid Fire OFF initially.

1. Check Give All's bank count before touching any arrows and after a checkpoint.
   The existing test12 preview repair remains in this build.
2. Hold G18/glock, enable Force Dual Wield, and keep it held for a few seconds.
   **BUILD** means distinct native hand definitions are queued; **COMPAT** means
   generic normal animations are active. Select the row: the footer distinguishes
   completed native setup, a pending actor callback, and setup failure.
3. After the footer says **Distinct native hand definitions ready**, inspect
   both models, both fire inputs, ammunition and reload. Turn Dual OFF and
   check that single-wield returns. Repeat with Beretta and a rifle that worked
   in test12, then another weapon that previously failed.
4. Compare fixed-view FPS for ten seconds each: OFF → ON → OFF. Test both
   Give/Dual OFF orders and Reset weapon toggles. Turn both OFF before leaving.

Test12 admitted G18 and set its native flags. Its remaining visual failure was
therefore beyond admission. Test13 asks the verified native animation builder
for separate right/left opaque definitions on the callback thread, rather than
keeping both cache slots aliased to the normal definition. The result is cached
per weapon/lifetime and published with guarded ownership; OFF retains the
original cache baseline. Normal cache slot zero is never cleared. Authored
paired definitions already registered by MW2 remain preferred. This is an
evidence-based rendering hypothesis until the owner observes the result; generic
normal animations do not become authored akimbo animations through this change.

## Then: enemies and helpers

Test one enemy, remove it, then one friendly and remove it before increasing
population. Start with weapon modes and the controller rehearsal OFF.

| Control | Expected behavior |
| --- | --- |
| Enemy model | Left/Right chooses a loaded ordinary actor class. Placement is independent of the selected model. |
| Spawn Enemy | Find the nearest eligible current enemy mission spawner to the player's present location; trace ground and full native human bounds, retry nearby offsets if occupied, then create the NPC. With no enemy points, try clear ground near the player. Set its enemy to the player and update its native movement goal toward the player every 0.5 game seconds. |
| Friendly model | Use a loaded friendly spawner or ordinary live friendly actor as a template. If none exists, the menu marks **CONVERTED** and offers loaded enemy models with native ally-team conversion. Uniform/model appearance can therefore remain enemy-like. |
| Spawn Friendly | Try clear ground 96–192 units around the player on roughly the same floor. No friendly mission spawn point is required. Transfer team through the native helper, clear inherited enemy targeting and follow spaced goals near the player. |
| Last-spawn header | Shows the created entity number, distance from the player to its original spawn position, and placement attempts. The detailed log records model, source, nearest point, position, team and thread. It is not a moving-NPC distance tracker. |
| Our spawned actors | Dynamic ownership replaces the eight-actor array; repeat spawning beyond eight while native slots remain available. NPCs leaving the live actor state are retired and their helper spawners freed on the native thread. |
| Engine actor slots | Shows native active usage and the physical 32-slot ceiling, including mission NPCs. A lower named `ai_count` soft ceiling can temporarily rise to 32 and is conditionally restored after cleanup. |
| Remove our spawned actors | Removes only this process/lifetime's generation-matched debug NPCs and their helper spawners. Original mission NPCs remain. Test before saving, reloading or quitting. |

Spawn an enemy, move to a different part of the encounter, and repeat. Check
where it appears and whether it approaches. Campaign combat/cover choices may
affect its route despite the requested goal; this is ordinary MW2 AI.
Spawn friendlies in estate, where test12 showed 0/0 friendly templates, then in
another active mission. Confirm they remain friendly and fight hostiles. Test
near a wall or another NPC: occupied locations should cause offset retries,
not an inside-wall creation. **NO_CLEAR_POSITION** means every tested candidate
failed; move a little or select another loaded class.

The physical 32-slot pool is not removed by changing a menu counter or dvar.
Larger simultaneous populations require verified pool relocation, every
consumer/index/mask, and separate stress tests. Test13 removes the artificial
eight-NPC cap and does not evict campaign actors to create space. Likewise,
near-player placement does not load actor assets absent from a mission.
The current service needs an existing, unfrozen native actor callback. Empty
levels and Freeze AI can prevent queued work from running.

Native field and VM-variable copy helpers initialize a separate real spawner;
the original source's location and spawn budget are not edited. The temporary
spawner survives with its NPC because native/script initialization can keep
`self.spawner` references. Its mission target and targetname are cleared with a
refcount-aware native setter. Mission script side effects and using live actor
templates instead of authored spawners still require observed coverage.

## Return evidence

Remove debug NPCs and turn Give/Dual OFF **before saving/reloading/quitting**.
Ownership is in-process; a saved NPC from a previous lifetime is not claimed
automatically. Close MW2 and run **Diagnose.cmd before another launch** replaces
`latest.log`. Return its ZIP and the short [testing checklist](testing.md),
especially the exact gun, footer, spawn location/team/approach, attempts/counts,
fixed-view FPS, collision error text and last action before any crash.

Relevant records: `DUAL_NATIVE_BEGIN/READY/APPLY`, `DUAL_BLOCK`,
`AI_SPAWN_BEGIN/ACCEPT`, `AI_DELETE_BEGIN`, `AI_PLACEMENT`, `AI_CAPACITY` and
`AI_RESULT`. Saved FOV, God/health, bank undo and rehearsal are regression checks.

## BO1 mode boundary

The accepted BO1 controller remains a rehearsal. These NPCs provide verified
IW4 lifecycle/placement/team/goal infrastructure, with normal campaign behavior.
The startup/drop/revive/wall-buy plan and source facts are in
[bo1-mode-design.md](bo1-mode-design.md). The next prerequisite is an independent
game-thread service: removing all campaign actors would otherwise stop the
actor-callback service before its first zombie can spawn. Then validate one
source-derived BO1 pursuit/melee/animation fixture before activating live rounds.
