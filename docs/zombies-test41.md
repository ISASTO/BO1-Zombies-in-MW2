# Test41: fixes from the first Test40 run

Version **0.1.40-zombies-test41**. It fixes the three problems found in the
first Test40 run (Favela, then Arcadia):

- the BO1 models never appeared;
- the game froze soon after zombies started spawning;
- on Favela some zombies spawned on rooftops and could not get down.

## 1. The freeze: BO1 attacks with no root motion

**What happened.** The Test40 logs end without a crash report:

- **Favela:** the last line is at most 11 s after the first zombie spawned.
- **Arcadia:** 8 to 10 s after the first zombie spawned.

That is about when the first zombies finish rising and reach you.

**The cause.** A new exact-native probe found it.
`tools/anim_delta_probe.cpp` runs the retail root-motion sampler
(`XAnimGetRelDelta` 0x29cbb0 → `XAnimCalcRelDeltaParts` 0x29b200) on every
converted BO1 animation. Three of BO1's six main attacks faulted:

- `ai_zombie_attack_v1`
- `ai_zombie_attack_v4`
- `ai_zombie_attack_v6`

These animations carry the "delta" (root-motion) flag but have no root-motion
data at all. BO1 checks for the data. MW2 checks only the flag
(`test byte [parts+0x14], 2`) and then reads the missing data, a null pointer.

So the game broke the first time a zombie picked one of these three attacks:
roughly one swing in three.

**The fix.** The converter now writes such an animation without the flag
(`HasDelta` in `include/bo1_xanim.hpp`). That is exactly what BO1 plays: the
zombie swings in place.

**Checks.**

- The probe samples each of the 27 BO1 animations the mod uses (3,600 samples
  for the longest) under a timeout.
- None now faults, hangs or returns a non-finite value.
- Every rise climbs the right height: 45 units for version 1, 15 for
  version 2.

The committed report uses synthetic animations covering every shape:

- no root motion;
- translation;
- yaw;
- small frames;
- 16-bit key indices past frame 255.

[anim-delta-test41-probe.json](anim-delta-test41-probe.json)

## 2. The models: loaded after the level needed them

The Test40 log shows the generated zone joining the level load
(`append=1`), but every model lookup failed (`present=0`). Nothing was wrong
with the file itself: the level loaded without an error.

**Why the lookups failed.**

- The database thread loads a list of zones in order.
- The server waits only for the assets it asks for: the level's collision
  map, its scripts, and so on.
- In Test40 the model zone was second in the list. It was still loading when
  the level scripts precached the models.

**The fix.**

- **Load order.** The model zone now goes first, so it has finished before
  the level zone starts.
- **Verification.** The binding verifier checks the database thread's in-order
  queue loop.
- **Crash guard.** In Test40 the freeze also left the crash-guard marker, so
  Arcadia skipped the models (`M-SKIPPED`). Now:
  - The marker is cleared when a level loads without precaching any model.
  - Install.cmd clears it too.

## 3. Rooftop spawns: zombies with no path

Spec Ops maps keep the campaign's enemy spawners, and some of them on Favela
are on rooftops and ledges with no path down. BO1 never has this problem: it
only spawns zombies in zones connected to yours.

**Detecting it.** Each zombie's path is now checked after it rises. A zombie
the engine finds no path for is the signal: its goal is you, but the path's
point count stays 0. The DLL reads the same count that the engine's
`Path_Exists` (0x144d30) and the `pathgoalpos` getter use: `path_t` at
actor+0xfa4, count at +0x380.

**What happens to such a zombie.**

- **Point proven unreachable:** no path for 3 s while another zombie does have
  a path to you. The zombie is respawned elsewhere (it still counts towards
  the round) and that spawn point is not used again on this map.
- **Never pathed, nothing proven:** no path for 12 s. The zombie is
  respawned, but the point is not blacklisted, because you may only be out
  of reach for a moment.
- **Limits:**
  - A point any zombie has pathed from is never blacklisted.
  - At most three quarters of a map's points are blacklisted.
  - The list survives the GAME OVER reload of the same map.

The log shows each case as `ZOMBIE_UNREACHABLE`, with a `ZOMBIE_REACH`
summary.

## 4. If anything still freezes: a self-explaining report

Test40 froze without any report. Two diagnostics now run all the time; they
only record, and never change what the game does.

**Fault recorder.** A record-only vectored exception handler notes up to 16
distinct faults, even ones the engine catches itself:

- the exception code;
- the instruction;
- the faulting address;
- nearby game and mod return addresses.

They appear in the log as `FIRST_CHANCE …` within a quarter second.

**Hang watchdog.** It triggers during a zombies game when no server frame and
no rendered frame has happened for 6 s while MW2 is the foreground app. It
then:

- logs every MW2 window, with the text of any error dialog, and brings that
  dialog to the front (a fatal-error box hidden behind the fullscreen game
  looks like a freeze);
- samples the main and render threads five times. It records where each one
  is and the return addresses on its stack, while the thread is suspended
  and without touching the heap;
- writes `bo1mw2\logs\hang-<time>.txt` and `.dmp`, like a crash report:
  engine error text, script VM state and the native event ring.

## Not changed

Everything else is as in Test40:

- the BO1 models and the crash guard;
- Spec Ops maps;
- the BO1 animations and zombie setup.

## Validation

- **Native tests.**
  - `bo1_xanim_tests` covers the delta flag without root-motion data, and
    the real `ai_zombie_attack_v1` (local).
  - `bo1_zombie_rules_tests` covers the reachability rules.
- **Exact-native root-motion probe.** Synthetic animations (committed) and
  the 27 BO1 animations (local): no faults.
- **Binding verifier.** Adds the in-order zone queue, `Path_Exists`, the
  `Actor_HasPath` and `pathgoalpos` getter chain, and the path offsets.
- **Earlier suites.** All earlier C++, Python and exact-native suites pass,
  including the Test40 model-zone probe.
- **Testing.** No Windows gameplay test of Test41 yet.
