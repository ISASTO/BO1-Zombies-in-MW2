# Test19: large-squad VM crash and appended 256-actor prototype

Version: `0.1.18-squad-test19`.

## Crash findings and DLL changes

The returned Test18 dump faults at `iw4sp.exe+0x2244c8`, in the native thread
stack archiver. The register arithmetic shows a logical value count of **-1**:
the current stack top is one value below that thread's start. The native routine
allocates a small buffer from the truncated size, then writes using the enormous
unsigned index. The last logged population was **26 active actors**, with 33
owned allocation tickets including corpses. There were no campaign slot
reclamations in that run. Its crash-specific log was preserved successfully.
See [returned evidence](runtime-test18-crash-evidence.json).

Test18 isolated builtin parameters during deletes, but the external native AI
service also needs a complete idle VM context. Test19 enters with the native
frame, value-stack and local-variable stack bases, saves the caller's VM state,
and restores its frame record, sentinel and pointers after the operation.
Ignored native return parameters/references are released through the native
zero-object notification routine. Work waits while a script frame is active.
A nested checkpoint reset abandons the old context rather than restoring it
over the new VM. This is a correction to the external-call contract; the dump
does not identify the first individual callback that leaked the old context.

An enemy chosen from the 500 ms world cache is validated against its current
allocation, brain, health and team before assignment. Dead, recycled, allied
and neutral replacements are rejected. A genuine new target still gets the
native enemy-change notification; repeated reassertions of that target do not
wake the same script listeners again.

The exact-native regression verifies parameter/refcount handling, ignored-return
cleanup, busy-frame deferral and restoration of a parked VM's frame/local state.
See [context probe](script-context-test19-probe.json) and
[binding checks](squad-test19-native-bindings.json).

The Test18 crash report used incorrect addresses for the two VM state pointers.
Test19 corrects those addresses and records start-top, frame/local counts and
the frame pointer. Dumps explicitly include the VM memory region and pool
metadata, so another underflow can be traced using the actual variables rather
than only the instruction registers. These copies happen only during a crash.

## Second 256-actor attempt

The new prototype appends 274 perception records after the original actor
prefix. Its stride is `0x48c0` instead of `0x1560`. Original goal, animation,
active-flag and other brain field offsets stay unchanged. Confirmed native
perception accesses move to the appended records, avoiding the old overwrite
at sentient index 50. Actor-index division changes with the larger stride.

The isolated executable fixture passes:

| Native operation | Verified result |
| --- | --- |
| Actor allocation/iteration | 256 distinct actors, refused 257th allocation, iterator reaches index 255 |
| Ordinary sentient allocation | 274 slots, refused allocation 275 |
| Perception clear/copy | All 274 records, intact original actor tail and arena guards |
| Actor reuse/reset | Appended records cleared on reuse; converted reset clears all 256 active flags |
| Animation pointer lookup | All 256 pointer entries resolve through native index arithmetic |
| Snapshot production | Actor indices 0..255; corpse index 319; full-size team/goal/animation arrays and native corpse goal copy |
| Save-field pointer fixups | Actual native round-trips for 256 actor and 274 sentient references |
| Perception save descriptors | All 274 appended node-reference fields fit the new actor layout |

See [native results and exact edit manifest](expanded-layout-test19-probe.json),
`tools/run_expanded_layout_probe.py`, and `tools/expanded_layout_probe.cpp`.
The original executable is read-only; the patched fixture is temporary and is
never installed or distributed.

**The DLL still uses 32 actors.** The expanded fixture does not yet convert all
remaining perception consumers, secondary reset/free/special sentient paths,
or the complete snapshot consumer/client representation. Full checkpoints
still need coordinated serialized sizes, flags/counts, format identification
and legacy loading. Pointer fixups passing independently are not a successful
full checkpoint round-trip. The prototype isolates memset, the iteration
predicate, perception callbacks and the snapshot profiler; it does not execute
campaign scripts, animations, engine startup or Windows gameplay.

This attempt closes more layout issues than the first allocator-only probe,
but publishing those partial edits into the DLL would leave live engine paths
using incompatible layouts. Runtime activation remains disabled until those
paths have an implemented and verified conversion.

## Owner retest

Close MW2, uninstall the previous DLL, install this whole Test19 package and
restart the mission once. Saved FOV is preserved, and a local players folder is
backed up when present.

1. In estate, reproduce the previous run: enable Allow Friendly Fire, spawn
   Soldiers, fight through a wave, kill some spawned allies and add replacements.
2. Build back toward 26-32 active actors. Check native engagement and follow
   behavior, especially after a target dies or a slot is reused.
3. Check autosave, death/reload and Remove our spawned actors with surviving
   allies. Original campaign actors must remain.
4. If a crash recurs, run Diagnose and return its ZIP with the last action and
   highest displayed active-slot count. The dump now includes the relevant VM
   variables; do not discard the associated crash-specific log.

The script crash fix requires this retail retest. The native prototype is
engineering evidence for the next expansion stage, not a 256-actor gameplay
build or an enabled hidden option.
