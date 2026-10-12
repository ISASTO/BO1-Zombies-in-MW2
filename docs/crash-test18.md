# Test18: menu crashes, crowded squads and friendly-fire penalties

Version: `0.1.17-crash-test18`.

## Returned crashes

Two Test17 dumps fail in CRT string reading with an invalid pointer whose bytes
spell `Maintain`, part of an adjacent weapon help string. World navigation has
six rows but its help-text array has five entries. Highlighting the final row
reads past that array; merely moving selection can crash without toggling an
option. Test18 supplies the missing friendly-fire hint, checks every label/help
lookup, and requires each table's size to match navigation at compile time.

The crowded-squad dump faults at `iw4sp.exe+0x2249cb`, the native script VM's
opcode fetch. Its code pointer is noncanonical. It is a script-state corruption
crash, rather than proof that a 33rd actor was allocated. The ZIP's `latest.log`
belongs to a subsequent launch, so it cannot identify that run's final action.

Code audit identified a concrete corruption path: ActorInitHook reclaims a mod
NPC inside a campaign spawn builtin, and native delete sends a notification.
Scr_Notify clears the current builtin's outgoing parameters even when the
notification has zero arguments. The hidden delete can therefore consume the
campaign spawn's values/references before that builtin returns.

The new cleanup frame leaves caller values and references on the VM stack,
temporarily isolates parameter counts, and restores the caller's counts after
balanced native cleanup. A nested checkpoint reset abandons the old frame.
The regular mod service also waits for an idle VM with no owned parameters.
An isolated MS-ABI execution of the exact retail Scr_Notify and native vector
reference cleanup reproduces consumption of three caller parameters and a
reference decrement without the guard. It verifies preservation with the guard,
nested frames, later caller cleanup, and checkpoint reset. No native dependency
is substituted in that probe. See [probe results](script-parameters-test18-probe.json).
This proves the unsafe operation and its parameter fix; confirming it explains
the entire returned crowded-squad crash requires the in-game retest.

## Allow Friendly Fire

ON keeps the aim-block distance at zero and friendly damage scale at one. A
validated player hit on a living same-team NPC goes through native G_Damage
with world attribution instead of player attribution. Damage amount, weapon,
hit location, pain/death callbacks and native cleanup still run. Campaign
callbacks no longer receive that hit as a player attack on an ally, preventing
the player friendly-fire penalty at its source. This is intended to allow
repeated friendly hits without the friendly-fire mission-failure screen.

OFF and Reset world immediately restore normal player attribution and release
owned dvar overrides. Enemy hits, NPC attacks, player/self damage and neutral
teams retain their attribution. The native mission-failure builtin is unchanged;
other objectives can still fail, and native hero invulnerability still applies.
The damage detour is published on the server clock in the same atomic hook
transaction. The option stays locked until that hook is installed.
See [native binding checks](crash-test18-native-bindings.json).

## Crash evidence and performance

Every crash now copies its own log before a later launch replaces latest.log,
records VM pointers/counts and the most recent 64 native operations, and adds
indirect memory and thread information to the dump. The operation ring allocates
no heap memory and writes no files during gameplay. Normal log writes remain
cached; disk flushes occur during exception capture. Diagnose includes the new
crash-specific logs automatically.

The arena snapshots, name cache and maximum four-actor follow batch are retained.
Test17's subsequent returned run saved/restored ten spawned actors and removed
all ten successfully; those native save/adoption paths remain enabled.
The native capacity remains 32; Test18 does not install the incomplete expansion.

## Owner retest

Close MW2, extract `BO1MW2-crash-test18.zip`, run the previous Uninstall then the
new Install, and restart the mission once. The installer backs up a local players
folder when present and preserves saved FOV.

1. Navigate through every World row, including Allow Friendly Fire and Reset,
   repeatedly with Up/Down. No activation should be needed to test the crash fix.
2. Turn Allow Friendly Fire ON and fire repeated bursts at a spawned Soldier
   and an ordinary campaign ally. Check that the friendly-fire endscreen is
   absent. Compare OFF and Reset world, then verify ordinary enemy damage.
3. Spawn friendlies gradually toward the native limit, then advance into an
   enemy wave. Verify frame pacing and no script VM crash during reclamation.
4. With surviving allies, autosave, die/reload, remove spawned actors, and reload
   again. Confirm campaign NPCs remain and saved ownership is restored.
5. Run Diagnose after any failure. Crash-specific logs now survive a relaunch.
   Include the map, final action, toggle state and highest actor-slot count.

Windows retail gameplay is not available in the build environment. Successful
cross-builds and native/portable tests do not substitute for these checks.
