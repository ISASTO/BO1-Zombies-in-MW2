# BO1 Zombies: initial local-input research

Scope: inventory and behavioral evidence only. No zombie AI, actor spawning,
asset import, or round replacement is implemented in test 1. This document
distinguishes readable source evidence from native-function hypotheses.

## Provenance and extraction

The supplied T5 zones are unsigned version 473 fastfiles. The outer zlib stream
begins at offset 12. Inline GSC/CSC RawFile buffers contain two size words and
another zlib stream. `tools/extract_bo1_scripts.py` independently validates those
sizes, EOF, and terminating NUL before extracting user-owned source locally.
The resulting manifest records zone hash, script hash, sizes, and buffer offset.

The extraction scope is inline compressed script records, not every asset type.
This pass validated 41 records from the Kino base, 72 from the common patch,
8 from the Kino patch, and no scripts in the two supplied locale zones. Counts
are per zone and include base/patch replacements, not 121 distinct scripts.
It preserves each zone separately so patch provenance is visible. When building
a resolved dependency graph, apply common/map patch replacements over base
scripts. Locale zones are not assumed to contain the main game logic.
Never commit extracted sources or converted game assets. Only original tooling,
hashes, derived findings, and compatibility implementations belong in this repo.

## Actual source located in uploaded files

| Responsibility | Source path | Uploaded zone |
| --- | --- | --- |
| Main mode, round lifecycle/count/health | `maps/_zombiemode.gsc` | `common_zombie_patch.ff` |
| Spawn initialization, run/walk selection, pursuit threads | `maps/_zombiemode_spawner.gsc` | common patch |
| Zone/spawner activation and connected areas | `maps/_zombiemode_zone_manager.gsc` | common patch |
| Barriers, teardown, rebuilding | `maps/_zombiemode_blockers.gsc` | common patch |
| Audio routing and voice choices | `maps/_zombiemode_audio.gsc` | common patch |
| Initialization and animation state | `animscripts/zombie_init.gsc` | common patch |
| Animation combat and melee | `animscripts/zombie_combat.gsc`, `animscripts/zombie_melee.gsc` | common patch |
| Movement and running | `animscripts/zombie_move.gsc`, `animscripts/zombie_run.gsc` | common patch |
| Pain, death, shared/traverse helpers | `animscripts/zombie_pain.gsc`, `animscripts/zombie_death.gsc`, `animscripts/zombie_shared.gsc`, `animscripts/traverse/zombie_shared.gsc` | common patch |
| Dogs, apes, score, powerups, perks | `maps/_zombiemode_ai_dogs.gsc`, `_zombiemode_ai_ape.gsc`, `_zombiemode_score.gsc`, `_zombiemode_powerups.gsc`, `_zombiemode_perks.gsc` | common patch |
| Client presentation, audio, callbacks | `clientscripts/_zombiemode.csc`, `_audio.csc`, `_callbacks.csc` | common patch |
| Kino entry/setup and map overrides | `maps/zombie_theater.gsc`, `maps/zombie_theater_ffotd.gsc` | map base and patch |
| Kino teleporter, movie screen, box, effects | corresponding `maps/zombie_theater_*.gsc` and `clientscripts/zombie_theater*.csc` | map base and patch |
| Quad and traversal behaviors | `maps/_zombiemode_ai_quad.gsc`, `maps/zombie_theater_quad.gsc`, `animscripts/traverse/zombie_*.gsc` | map base; some common patch replacements |
| Initial zombie actor/character | `aitype/zombie_ger_zombie.gsc`, `character/c_ger_honorguard_zt.gsc`, model-alias scripts | map base |

Some included dependencies, such as general animation utilities, may still live
in unprovided common zones. Build an explicit missing-dependency list before
requesting more inputs. The supplied patch already contains the central Zombies
logic, so those files are sufficient for this initial research.

## Round and health findings from actual source

Line references below apply to the extracted `common_zombie_patch.ff` source,
not an arbitrary online script or decompilation. No original script bodies are
included here.

| Finding | Evidence | Important qualification |
| --- | --- | --- |
| Concurrent limit defaults to 24 | `_zombiemode.gsc:83-86`; native `SetAILimit` call | Distinct from total enemies in a round; map overrides possible |
| Fallback round-one health is 150; additive increase 100; multiplier 0.1 | `:695-697` | `mp/zombiemode.csv` can override these values |
| Health recomputed from base each round | `ai_calculate_health`, `:4017-4031` | Adds 100 for steps 2 through 9; from step 10 adds `int(current health * multiplier)` each step |
| Base pacing fallback 2 seconds | `:698` | Runtime override and end-round mutation must be preserved |
| Spawn delay shrinks 5% at round end | `:3953-3961` | Branch is based on the prior value: above .08 multiply by .95; below .08 set .08. Preserve the one-round undershoot rather than replacing it with a continuous clamp |
| Inter-round fallback is 10 seconds | `:709`, round HUD/wait flow around `:3836` | Audio/round notifications and map hooks also affect the sequence |
| Base count fallback 24, per-player modifier 6 | `:702-703`; round_spawning around `:3141-3164` | Uses round/5 multiplier, bounded below by 1, then an additional round*.15 multiplier at round 10+ |
| Solo count uses half of the per-player modifier | `:3157-3163` | Co-op branch uses player-count-minus-one instead; do not use a remembered polynomial |
| Early-round counts are scaled | `default_max_zombie_func`, `:3075-3102` | First round .25; otherwise rounds below 3/.3, below 4/.5, below 5/.7, below 6/.9; integer conversions occur at specified points |
| Spawn controller waits for both capacity and remaining budget | `:3196-3203` | Wait interval .1; also obeys `spawn_zombies` flag |
| Spawn chosen from active enemy spawners | `:3207-3217` | Random selection with previous-spawn handling; Kino zone graph decides which spawners are active |
| Round completion waits for alive count, unspawned budget, and intermission | `:4158` | Separate outstanding budget from living entities |

These are **source control flow plus recovered normal tuning**, not a complete
final map/runtime configuration. Normal difficulty selects CSV column 2;
between-round time omits the argument and uses utility default column 1.
The original five supplied streams lacked the table name. The subsequently
provided common_zombie.ff contains the complete structural StringTable.

The exact local native divide implementation now confirms that integer operands
return binary32 float, and the int builtin truncates toward zero. Test5's pure
rules implement this calculation order with bounded fixtures.
The menu labels their provenance and does not spawn actors. See
[round-test5.md](round-test5.md) and [numeric evidence](bo1-round-rules-evidence.json).
No active normal-round count callback assignment was found in the supplied Kino
map scripts. Special quad scheduling does have a separate count/pacing path,
which is excluded. Full configuration, callbacks and VM scheduling remain to be
resolved before a playable controller is claimed.

### Common-zone tuning recovered for test6

`tools/recover_bo1_tuning.py` reads the supplied unsigned version-473 zone and
emits selected derived values/provenance, never a full original CSV. The table
at raw offset 0x2a5b17f is 5 columns by 106 rows, with 530 eight-byte cells.
Normal column 2 is labeled MEDIUM. Recovered health 150 / increase 100 /
multiplier .1, spawn delay 2.0, base budget 24 and per-player modifier 6 match
test5. Runner interval 10 and movement multiplier 8 are recorded for later AI
research and are not implemented. The between-round key is absent across the
table's key hashes, so 10s remains the patched GSC fallback. Concurrent limit
24 comes from script initialization/SetAILimit, distinct from CSV max_ai.

Inline strings use checked case-insensitive DJB2 hashes. Packed XString refs
subtract one, then split into three block bits and 29 offset bits. Virtual block
4 offsets are not raw file offsets. The tool reconstructs a constant physical
minus virtual delta only for tables whose local aliases give a unique mapping,
builds a pointer-keyed cache, and verifies each selected reference's hash.
The input yields 13 structural tables, 12 mapped tables and 754 cached strings.
The target has 14 distinct alias values; two source tables rely on one alias
each. Those weaker anchors and unresolved unrelated references limit the scope.
Global hash matching is never used to choose a shared string: real collisions
exist for short numeric text. Ambiguous/conflicting/unresolved required pointers
fail rather than guessing. Index arrays are validated as permutations; their
sort order is not claimed. This is a scoped forensic reader, not a full zone
fixup loader or importer. Synthetic corruption/alias/truncation fixtures pass.

See [tuning evidence](bo1-normal-tuning-evidence.json) for exact file hash,
selected cell references, raw offsets and mappings. Patch precedence, final
callbacks, special rounds and native timing still need independent audits.

## Pursuit, attack, reaction, and animation coupling

`_zombiemode_spawner.gsc` has `find_flesh()` near line 3922, state initialization,
rise/barrier handling, movement-speed selection, and target/path threads.
Native T5 exposed-actor thinking remains part of the behavior; that GSC alone
does not constitute a complete zombie state machine.

The local `zombie_melee.gsc` starts a selected attack animation and waits for
named animation notifications. A `fire` notification triggers native `melee()`;
`stop` rechecks whether the attack can continue; `end` finishes the animation.
It evaluates enemy validity, orientation, distance, previous misses, goal and
movement feasibility, and traces. Damage timing is tied to XAnim notetracks, not
an invented periodic hit timer. At lines 680 onward, the selected animation
pool depends on walk/run/sprint and full-body/crawl/no-leg state.

Pain/death scripts and shared notetrack processing need the same audit. Tests
must compare hit timing, animation transitions, and reactions against BO1 before
claiming faithful behavior. Simply retargeting armed MW2 soldiers is insufficient.

The initial actor script assigns zombie type, axis team, and 150 initial health,
then delegates appearance to the character script. Its default model comment
names `char_ger_honorgd_fullbody`; the actual character/model-alias chain selects
body/head variations. This default comment is not proof of every spawned model.
The melee source contains aliases such as `zmb_vocals_sonic_attack`; audio routing
is conditional on actor type, so an alias found in a shared file is not assumed
to be standard Kino zombie audio. Resolve the character's real alias set and
sound bank/IWD dependencies before extraction/conversion.

## Native T5 research targets

The current public `MisaDev4/bo1-zombies-decompiled` tree contains actor headers,
state/navigation/senses/threat/pain/death code, and exposed zombie thinking.
Pin the reference commit `102f6992be332610aff587e681e3180b15f395bb`.
Its supplied-address annotations include an exposed thinker, origin/orientation
updates, goal/path finding, and pre/post-think helpers. They are leads, not
confirmed addresses in this uploaded BlackOps.exe. No code from that tree has
been copied or compiled here, and its behavior may include compatibility changes.

Next audit targets in the local T5 executable: actor state dispatch, sentient
target selection, enemy visibility/threat tests, actor path-following, native
melee tracing/damage, animation notetrack emission, corpse/dismemberment state,
and the GSC builtins used by the extracted scripts. Recover only the fields and
calls each compatibility feature actually needs. Independently match native
evidence and test it before translating to the new x64 IW4 structures.

## A / B / C division

| Class | Functionality | Planned treatment |
| --- | --- | --- |
| A: essentially direct logic | Health recurrence, count multipliers/early-round factors, capacity/budget accounting, delays, round notifications, failsafe policy | Original implementation of source-derived rules; fixtures from local source/CSV and BO1 observations |
| B: engine/API translation | Script threads/endon/waittill, target acquisition, pursuit, LOS/goal tests, melee damage/traces, movement/orientation, animation state and notetrack events, pain/death callbacks | Explicit T5-to-IW4 adapter; implement required semantics rather than copying native structs |
| B: asset translation where feasible | Skeleton/model meshes, XAnim curves/notetracks, texture inputs, decodable sound data, alias timing | Versioned local conversion tools with provenance and dependency manifests; no bundled assets |
| C: compatibility or substitution required | Direct execution of x86 T5 actor code in x64 IW4; T5 pointers/vtables/VM internals; T5-only shader/runtime features; actor/corpse systems without an IW4 equivalent | Implement a compatibility layer or choose a documented substitute; never treat binary similarity as compatibility |
| C pending feasibility audit | Exact T5 dismemberment, unusual material effects, special traversal/hazards or round-specific engine features | Establish a minimum faithful behavior target and test conversion/API support before committing to a feature |

Existing MW2 campaign spawners/nav nodes are candidates, not guaranteed valid
zombie entrances. They may be script-controlled, mission-only, inaccessible
outside the intended route, or dependent on combat triggers. BO1 zone/spawner
logic needs a per-map adapter and path-reachability validation. Campaign scripts
and mission objectives must also be isolated deliberately for a survival mode.

## Asset format/dependency gate

| Asset | Required audit before conversion |
| --- | --- |
| GSC/CSC | Dependency closure, builtin inventory, numeric/event/thread semantics, script loading in exact IW4 runtime |
| XModel | Serialized version, bone names/hierarchy/bind pose, weights, surfaces/LODs, material links, hit locations/collision, dismember variants |
| XAnimParts | Bone channels, compression/quantization, delta/root motion, frame counts/rate, event/notetrack names and times, anim-tree wiring |
| Sounds | Alias records, sample format/codec, streamed vs loaded representation, channel/volume/pitch attenuation, subtitle/localization and bank/IWD dependencies |
| Materials/images | IWI/image formats, normal/specular conventions, sampler state, technique/shader compatibility, per-surface mappings |

OpenAssetTools' game-specific schemas and loaders are useful references, and
ZoneTool/IW4x provide IW4 asset/zone context. Their supported historical formats
do not prove successful loading in the supplied 2026 retail executable. Test one
asset type at a time, preserving an explicit conversion log.
