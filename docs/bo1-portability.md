# BO1 behavior and asset portability audit, test11

Reviewed 2026-10-11. Primary behavioral evidence is the owner's validated T5
version-473 `common_zombie.ff` and `common_zombie_patch.ff`. The patch takes
precedence for shared script paths. Earlier Kino base/patch observations stay
recorded in bo1-research.md; those map streams and BlackOps.exe are not present
in the current working input set and were not re-audited this iteration.

`tools/audit_bo1_scripts.py` follows includes and static namespace references,
case-insensitively, from ten chosen mode/AI roots. The two current zones yield
80 base and 72 patch script records, 48 overridden paths, 60 reachable scripts
and 64 missing references. This is a lexical source graph, not final runtime
closure: debug blocks are excluded, dynamic callbacks and assets remain open,
and globally referenced campaign utilities are not all prerequisites to a
small Zombies subset. General zombie/combat/pain/pose utilities are now found
in the common base. Missing `common_scripts/utility`, `_anim`, `_spawner`,
`_spawn_manager`, `_utility_code`, global code scripts and map/character paths
are listed in the evidence manifest. Missing means absent from these two zones.

## Behavior findings and adapters

| Subsystem | Actual source evidence | Implication for our port |
| --- | --- | --- |
| Round lifecycle | `_zombiemode:3641–3983`, `round_wait:4130–4167` | First chalk sequence waits 1+2+3+.25+2 seconds. Later chalk waits .5+2; end chalk totals 10s with recovered fallback. Presentation callbacks/voices remain separate. |
| Spawn budget/cap | `round_spawning:3103–3289` | Wait .1s at cap or zero queue, obey spawn flag, pick active spawner, retry a repeated choice once, decrement only for a defined spawned AI, then wait pacing and a network frame. |
| Enemy counting | `_zombiemode_utility:51–69` | Enumerate axis/all AI species; exclude ignore_enemy_count; require defined animname. Counting every MW2 hostile soldier would not match it. |
| Actor creation | `_zombiemode_utility:71–109` | Script-move override, forced StalingradSpawn or DoSpawn, failure predicate and persistent spawner setup. Entity allocation alone is insufficient. |
| Remaining zombies | `_zombiemode:3294–3343` | Only rounds 4–9 launch the speed-up thread. Wait queued <=4, then enemy count <=3; only one remaining actor with legs gets a sprint override. This does not mean every low-population zombie instantly sprints. |
| Failsafe | `_zombiemode:4059–4127` | Observe position every 30s, exempt barrier activity within 8s, check below-world or <24-unit movement; guarded requeue differs for outside/inside playspace and dog rounds. Auto-clear is a test fixture, not this behavior. |
| Target selection | spawner `find_flesh:3922–4066`; utility `get_closest_valid_player:996–1048` | Normal initialization enables path-based closest-player calculation. Filter invalid/last-stand/zombified targets, solo ignore list reset, POI and map hooks; periodic 1–3s reacquisition, not a per-frame nearest-distance shortcut. |
| Pursuit | spawner `zombie_pathing:4082+` | Native follow/goal/path events, bad_path fallback to player breadcrumbs, invalidated attractors and blocked crumbs. MW2 path nodes may help but do not supply BO1's script state. |
| Melee admission | `zombie_melee:326–503`; `zombie_init:1063–1067` | 64-unit close range, 200-unit charge, 512-unit long charge; yaw 60° admission/110° continued, sentience/alive/disable/shield checks, exact closest-enemy epsilon comparison, 3s recent-miss charging gate and target reservation. Portable ordinary-zombie/player rule implemented; special/AI melee excluded. |
| Melee path | `zombie_melee:505–554` | Horizontal approach point 28 units before target, IsInGoal, MayMoveToPoint; knee/eye BulletTrace fallback, same-target hit checks, optional water exception. Needs native collision/nav adapters. Missing path result never permits close melee in our rule. |
| Attack execution | `zombie_melee:13–227` | Select animation, flagged restart/blend, damage on fire notetrack, continuation on stop and loop end on end. Shared initialization sets meleeDamage=60, but native melee damage/hit/player-health behavior is unported. No guessed periodic damage timer. |
| Pain | `zombie_pain:9–100` and helpers | Long-death/disable/pain-test gates, flame time, health/pose/hit-location logic, special weapon reactions and animation blend/physics. Damage amount alone cannot choose a faithful reaction. |
| Death | `zombie_death:24–201` and helpers | Custom death callback, actor-corpse lifetime, flagged death animation, notetrack/ragdoll handoff and fallback when ragdoll fails. Health zero plus immediate delete is inadequate. |

Line ranges are tied to the script hashes in the evidence manifest. Static
source does not prove the final Kino callback configuration or runtime timing.

## A / B / C responsibilities

| Class | Functionality | Current state / needed work |
| --- | --- | --- |
| A: essentially direct behavior port | Count/health arithmetic, round obligations, cap, conditional pacing update, one-retry spawner policy; ordinary-player melee admission decisions | Implemented as original portable rules/controller with fixtures. Source ordering and float/int conversion points retained. First/later presentation waits reused in rehearsal only. |
| A: direct logic with pending inputs | Player filters/POI preference, remaining-zombie policy, failsafe predicates, pain/death selection branches | Source identified; audit map overrides and translate input semantics before integrating. |
| B: engine/API translation | GSC threads/waittill/endon/notify/flags, server/game clock, actor spawn/init, sentients, target/threat/senses, goal/path/traces, reservations, native melee, damage, corpse/ragdoll/notetracks | CommandTime is a verified IW4 clock for rehearsal. Everything else needs exact-build fields/calling conventions plus an engine-thread execution hook. D3D9 render thread must not call actor/VM mutation APIs. |
| B: assets and presentation | Skeleton/skin surfaces, bone/tag names, XAnim tracks/root motion/notifies, animation trees/blends, material/image/techsets, sound banks/aliases, fonts/HUD | Use typed, versioned conversion with validation; bind only after this 64-bit retail loader/runtime is understood. |
| C: direct transplantation unavailable | T5 x86 function bodies/pointers/actor layouts/VM IDs, literal T5 fastfiles, T5-specific native zombie species/function table and map/entity assumptions | Compatibility implementation required. No layout or address transplanted into IW4. |
| C: potentially unavailable IW4 features | BO1-specific dismemberment/destructibles, special species/weapons/material effects, streaming/audio contexts or advanced root/IK behavior absent from IW4 | Determine feature-level substitutes after exact decoder/renderer evidence; do not silently claim identical behavior. |

## Current native reverse-engineering references

The current MisaDev4 tree at `102f6992be332610aff587e681e3180b15f395bb`
contains actor state, species/function dispatch, SP extension, senses/threat,
navigation, pain/death and a zombie exposed thinker. The latter combines native
pre/post think, goal/path, perception, orientation, team movement, script-change
safety and several exposed substates. That reinforces the need for a native
adapter beyond find_flesh. It is reference code, not exact-byte proof for the
owner's executable. Some files mix historical MP-shaped types and SP extensions.
No top-level license was found in the inspected tree; no source/data copied.

Current IW4x main is `d1b175da1a25d31043246de103a702b206269e5a`.
Its inspected Structs.hpp supplies asset/entity/level concepts for its MP
target, not an actor_s ABI for this retail SP AMD64 executable. Our confirmed
IW4 partial player fields stay separate. No new native addresses or signatures
were added for test11; all 57 existing anchors still require exact hash gating.

## Asset formats and import plan

OpenAssetTools pin `068d4cd9f513ff0509b757498712b2ec3dee9463` has separate
T5/IW4 schemas and compiled XAnim import/export. ZoneTool develop pin
`aa4475b185de5ef6f5a63360d3d5e9c3321ca7ff` provides another IW4 XAnim
writer: script-string remapping, notifies, quantized tracks, indices and delta
subrecords. Its parse/dump reuses IW5 data assumptions, so it is not an exact
retail SP importer. Neither project's historical in-memory schema proves the
updated MW2 fastfile/runtime ABI. These references were inspected without
copying their implementation into ours.

| Asset | Format/dependencies confirmed in primary references | Conversion and validation needed |
| --- | --- | --- |
| Fastfile / GSC | Owner T5 v473 outer zlib and inline compressed RawFiles. Tool schemas encode T5 RawFile differently from IW4 RawFile; zone relocation/string blocks are separate from source extraction. | Own extractor is deliberately scoped. Need an owner's MW2 campaign/common zone plus exact loader audit before constructing/importing target zones. Do not copy a BO1 FF into MW2. |
| XAnimParts | Bone/script-string table, ordered notify names/times, frame/index widths, quantized short/byte tracks, root/delta subrecords. T5 adds loop/delta/IK/streaming/primed data; IW4 uses different flags/root channels. | Normalize to an owned interchange, retain names/notify ordering/timing/root displacement, map skeleton, encode target channels and verify round-trip sampling. Never copy structs or numeric script-string IDs. |
| Compiled XAnim interchange | OAT supports versions 17/18/19; IW4 output chooses 17 or 18 according to root-track needs, T5 chooses 19. T5/T6 share 19 with different flags. | These are exported interchange versions, not fastfile versions. Explicit source-game discriminator; don't relabel a file or drop fire/stop/end notifies. Runtime actor animtree/blend binding remains separate. |
| XModel / XSurface | Bone hierarchy, bind transforms, skin weights, tags, materials, LOD/collision. T5 has different surface/LOD/stream/constraint fields; IW4 uses its own surface/material/physics dependencies. | Named skeleton mapping plus weighted bind-pose validation, LOD/surface index checks, rebuilt target materials/collision. A visually similar mesh does not prove animation compatibility. |
| Materials / images | Techsets/shader bytecode, texture encodings/semantics, sampler/state constants. Current OAT T5 image export is partial for some encodings; T5 image/techset import not supported in its matrix. | Decode textures locally, preserve alpha/normal semantics, author target-compatible material/techset mapping, verify in IW4 renderer. Direct T5 shader reuse is unproven. |
| Sound / aliases | T5 SndBank/SndPatch, alias IDs/index and pack references; alias volumes/pitches/contexts are differently encoded from IW4's alias lists/SoundFile/LoadedSound and curves. | Resolve locally owned pack/locale audio and codecs, normalize sample rate/channels, convert spatialization/volume/pitch/loop/context semantics. OAT T5 bank/patch dump/load is currently unsupported; sound is a separate pipeline, not solved by model/animation export. |

Next useful live step after tomorrow's evidence: identify an exact IW4 SP
engine-thread scheduling point and read-only actor/sentient/asset identities;
then one validated spawn/init/cleanup fixture. Pair one normal BO1 zombie mesh
and one locomotion/melee animation with a typed local converter before calling
the result a zombie. Record notetrack events and compare BO1 observations.
Converted/original assets, scripts, audio and executables stay outside Git and
the distributable package; own source/tooling expects user-owned installations.
