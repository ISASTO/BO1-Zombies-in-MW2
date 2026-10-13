# Test21: weapon feedback, held fire, ally protection and expanded lifecycle work

Version: `0.1.20-squad-test21`.

The returned Test20 run lasted from 07:49:40 to 08:03:32 local time, reached
32 active actors, recorded 278 creations/148 deletions and 24 checkpoint write
events, and has no new crash. Its newest retained dump predates the run. The
owner also reports stability in relatively extreme scenarios. This is evidence
that the Test20 ownership-reference fix helped; it is not proof against every
possible future crash. See [returned evidence](runtime-test20-evidence.json).

## New controls

| Section | Control | Behavior |
| --- | --- | --- |
| Weapons | Hitmarkers | Draws a white X for 160 ms, with a short fade, after a player attack actually lowers a validated actor's health. Works with the menu and status HUD closed. Misses, NPC attacks and blocked damage produce no marker. |
| Weapons | Make Semi-Auto Full-Auto | Changes the held bullet gun's single-shot mode to the native automatic mode. Holding fire repeats at the gun's current firing delay. Works with Rapid Fire; normal reload/equip/pump/bolt delays remain. Burst modes retain their original thresholds. |
| AI | Invincible Spawned Friendlies | Blocks damage to living, owned actors on the player's team. It covers existing and newly spawned helpers; their allocation identity must still match. Campaign actors and enemies are excluded. Removal uses the existing delete path and remains available. |
| World | World Speed | Replaces Slow Motion. Left/right selects 0.1x through 10x in exact tenth steps, starting at 1x. Enter toggles the override. OFF restores the captured native speed. |

The automatic conversion restores the original firing mode on OFF, switching
weapons, weapon-bank changes, weapon reset or a player/level lifetime change.
Restoration requires the same weapon index, complete definition, definition,
name and injected mode; a replaced asset or intervening native edit is preserved.
The held gun definition is shared engine data, as with existing Rapid Fire.
Hitmarkers and held-fire conversion are cleared by Reset weapon toggles.
The separate AI protection toggle survives weapon/world resets.

Hit feedback and protection reuse the existing exact-profile damage hook.
Protection does not freeze health, create another scan or write permanent God
flags. Feedback checks actual health loss and the entity allocation generation
after damage. Its renderer uses stack vertices, one small draw and the existing
D3D state restoration. Hit events do not write a disk log. Diagnostic counters
appear with the existing AI snapshot. Both new menu pages show all twelve rows.

The native firing-mode field is independently checked at definition `+0x64`.
The unchanged routine at `0x83f90` passes **530 cases**: both hands can continue
through 256 held shots after mode conversion, OFF restores the one-shot gate,
and burst modes keep their two/three/four-shot thresholds. See
[native firing probe](fire-mode-test21-probe.json) and
[independent bindings](controls-test21-native-bindings.json).

## Third 256-actor prototype stage

The private appended-layout fixture now converts the recorded actor-stride and
pool-span traversal sites, direct sentient-pool references, remaining actor
index divisions and associated iteration bounds. It relocates 64 identified
inline perception accesses and the common native perception accessor. The
actor prefix and its tail fields retain their original offsets.

Special sentient allocation formerly counted non-actor sentients in an unrolled
fifty-record census. That census, victim search and free-path iterators now
cover 274 records while retaining the existing sixteen non-actor quota.
Another newly identified dependency is the WORD reference-head table: its
original fifty entries sit next to other globals. It is relocated and reset
across all 274 slots, avoiding cleanup writes into neighboring state.

The actual native record writer/reader (`0x1adfb0` / `0x1adf50`) now run with
the expanded actor descriptor list and full `0x48c0` record size. A private
population chunk contains a 32-byte version/layout discriminator, 256 actor
flags/bodies and 274 sentient flags/bodies. Only byte transport is substituted;
field serialization and pointer conversion execute natively.

| Native check | Result |
| --- | --- |
| Common perception accessor | 70,144 correct actor/sentient combinations |
| Complete actor records | 256 records restored byte-for-byte, including actor references beyond slot 31 |
| Complete sentient records | 274 records restored byte-for-byte, including references to slot 273 |
| Perception node pointers | 70,144 actual node-pointer round-trips |
| Population chunk | 4,805,570 bytes consumed exactly; wrong version/layout rejected |
| Stock actor record | Original native descriptor decoding, then migration of fifty records to appended storage; truncated/overlapping input rejected |
| Special sentient replacement | Native victim selection reaches slot 270; cleanup clears references and perception in all 256 actors |
| Free/reuse | High sentient allocation and entity link released, then reused by the native allocator |
| Reference-head reset | All 274 entries cleared, surrounding guards intact |
| Animation cleanup | Native loop visits and clears all 256 relocated entries |
| Prior Test19 checks | Allocation/iteration, clear/copy, snapshot production through corpse 319 and typed pointer fixups still pass |

See [expanded native results and exact edits](expanded-lifecycle-test21-probe.json),
`tools/run_expanded_lifecycle_probe.py` and `tools/expanded_lifecycle_probe.cpp`.
The fixture substitutes byte-stream transport and animation-tree release, skips
the following unrelated global engine shutdown, and retains Test19's isolated
perception notification/profiler dependencies. It does not boot MW2.

**Runtime capacity remains 32.** This is a tested expanded population chunk,
not a complete in-game checkpoint or a converted client snapshot. Activation
still needs the client/transport snapshot representation and consumers, native
checkpoint routing/format detection with surrounding engine state, further
transitive consumer review, and startup/asset/lifetime testing in Windows.
The private image and its capacity edits are never installed or distributed.
No partial expansion is included in this DLL.

## Owner checks

Close MW2 completely, uninstall the previous build and install the whole Test21
folder. Saved FOV is preserved and a local players folder is backed up.

1. Enable Hitmarkers. Hit enemies, miss deliberately, and hit an invincible
   spawned ally. Verify the marker only appears on actual actor health loss.
2. With a semi-auto pistol/rifle, hold fire. Toggle conversion OFF and verify
   single presses again. Check weapon switching, reload and Rapid Fire together.
3. Spawn allies, enable Invincible Spawned Friendlies, and test enemy fire,
   explosions and Allow Friendly Fire. Turn protection OFF and confirm normal
   damage. Remove spawned actors and check save/reload with the toggle enabled.
4. Adjust World Speed while OFF, then enable 0.1x, 1x and 10x. Turn it OFF and
   check restoration; repeat alongside Freeze AI and Rapid Fire.
5. Repeat the crowded-squad Test20 stress run and return Diagnose output if
   stability or frame pacing regresses.

Seventeen C++ suites, 28 Python fixtures, 99 exact profile anchors and native
firing/expanded-lifecycle regressions pass. The new controls need Windows retail
testing; the Linux workspace cannot validate their live presentation or gameplay.
