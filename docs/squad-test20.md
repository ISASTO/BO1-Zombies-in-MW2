# Test20: shared ownership-label references

Version: `0.1.19-squad-test20`.

## Returned Test19 failures

The two uploads contain different latest crashes:

| Run | Fault | Observed state |
| --- | --- | --- |
| 06:21:17 | `iw4sp.exe+0x2244c8`, VM thread archive | Logical stack count -1; 32 native actors in the captured pool metadata; autosave retained 32 ownership tickets shortly before the fault |
| 06:22:57 | `iw4sp.exe+0x29967d`, model bone hierarchy | Invalid model parent-map pointer during collision processing after friendly-fire hits; peak 32 actors, 26 at crash; 23 mod ownership tickets at the last heartbeat |

Both report Test19 and preserved their run logs. Neither logged campaign slot
reclamation. The new VM memory confirms the first underflow; Test19's external
context guard did not prevent it. See [returned evidence](runtime-test19-crash-evidence.json).

## Concrete reference-count failure

`MarkOwned` interns the shared helper label with the native user-1 string factory,
assigns it through `Scr_SetString`, then previously cleared a local copy of its id.
That last action was incorrect: the factory acquires an engine-user reference
when that user first requests the string. Subsequent user-1 lookups return the
same id **without another temporary reference**. The setter separately acquires
each entity field's reference. Clearing the local copy cancels that acquisition.

The unchanged native hash/user-mask branches at `0x21b000..0x21b025` and
`0x21b14b..0x21b175` independently show this behavior. With 32 labeled helpers,
the old path leaves only one reference. The first native field cleanup frees
the slot while 31 fields still hold its id. More releases through those stale
fields can write into an unrelated allocation that reuses the same native pool
slot. DObj model vectors and archived VM buffers also use that pool.

Test20 removes the unowned temporary release. There are now 33 references for
32 fields: one engine-user reference and 32 native field references. Normal
field cleanup removes each field reference once. The engine owns its user
reference until its native user/shutdown lifecycle releases it; the DLL does
not add a separate permanent reference on every lookup.

The isolated native regression runs hash lookup, user-mask handling, field
assignment and release unchanged. Only single-thread locks and pool allocation/
free recording are substituted. It reproduces premature free, then demonstrates
two stale releases decrementing a reused model pointer and one decrementing an
archive code pointer. With a valid one-bone model, the actual native hierarchy
routine passes. After those two stale releases move its model pointer back two
bytes, the unchanged hierarchy faults at **the returned RVA `0x29967d`** in an
isolated child. The corrected runtime helper passes 32 assignments,
reassignment, full cleanup and 16 replacement cycles without premature frees.
See [native results](owned-strings-test20-probe.json) and
`tools/run_owned_strings_probe.py` / `tools/owned_strings_probe.cpp`.

This proves the ownership bug and its pool-reuse corruption mechanism. The
returned dumps do not capture the first freed label slot or its later reuse,
so assigning both individual retail faults to that exact slot remains an
inference. This build requires the owner's retail stress reproduction.

## Diagnostics and unchanged capacity

Each new marker assignment logs its native reference count and user-1 bit.
That occurs during spawning, not every frame. Crash dumps retain the VM ranges
and add the shared pool pointer plus up to 1 MiB of its readable storage, so a
recurrent failure can show the label, archive and model allocation data. Normal
cached log writes and the four-actor-per-tick follow budget remain unchanged.

The playable capacity remains **32**. The separate Test19 256-actor prototype
is unchanged; full snapshot/checkpoint/remaining lifetime conversion is still
required before enabling it. This release focuses on the reproduced corruption.

## Owner stress test

1. Close MW2 completely, uninstall the previous build, and install this whole
   Test20 package. Start the mission fresh once; a previous crashed process or
   its corrupted checkpoint is not a clean lifetime test.
2. In estate, spawn Soldiers toward 32 total active actors. Confirm movement,
   engagement and frame pacing. The first helper marker should have two native
   references; the shared helper label's count should rise with assignments.
3. Enable Allow Friendly Fire, kill several spawned allies, and add replacements.
   Repeat this cycle. Reproduce the second run's Rapid Fire 5x if desired.
4. Check autosave, death/reload, relaunch and Remove our spawned actors. Check
   that campaign actors remain and that no helper ownership labels are lost.
5. If a crash recurs, run Diagnose and return its ZIP with the last action and
   highest displayed active count. Keep the crash-specific log.

Build/reference probes are executed in Linux. No Windows gameplay or complete
native checkpoint stream is executed by those tests.
