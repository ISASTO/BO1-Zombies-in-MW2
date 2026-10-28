# Test42: the freeze, found with the new hang reports

Version **0.1.41-zombies-test42**. Everything else is as in Test41.

## What the Test41 hang reports showed

Test41 froze on all three maps. Each time, the new watchdog wrote
`hang-*.txt` with the stalled main thread sampled five times, and each time
the evidence agreed:

- **Where the game was stuck.** All 15 samples were at the same instruction,
  `iw4sp+0x21a9b0`. It is the hash-chain walk in `SL_RemoveRefToString`
  (0x21a8b0), the function that releases a script string. That loop only ends
  when it finds the string being released.
- **How it got there.** The stacks show the release coming from the animation
  system: `XAnimSetGoalWeight` (0x29e730) releasing an animation's notify
  name. That happened either in the actor's think (a rise ending) or inside
  the mod's `stopanimscripted` after a rise.
- **Which string.** The dumps hold the released string's table entry:
  - `"zombie_rise"` on Favela and Arcadia;
  - `"zombie_attack"` on Gulag.

  Its reference count was already 0. The string had been freed earlier, so
  it was no longer in the table, and the search for it never ends.

## Why the string was freed: how animscripted uses its notify name

The mod starts the rise and the attacks by calling the native `animscripted`
builtin with the notify name as a string argument. Inside the builtin:

1. It reads the notify name with `Scr_GetConstString(0)` (0x183257). This
   borrows the id; it takes no reference.
2. It pushes the arguments of `animscripts\scripted::init`. The first push
   runs `IncInParam` (0x221ff0), which **releases the builtin's own
   parameters** (the "out-params").
3. It pushes the borrowed id again with `Scr_AddConstString` (0x183351).

**Called from a script,** that is safe: a string literal is kept alive by the
compiled script for the whole level.

**Called from the mod,** the only reference was our parameter. Step 2 freed
`"zombie_rise"`, and step 3 revived a dead id. When the rise or the attack
ended, its last release went looking for a string that was no longer in the
table, and the game hung.

This also explains Test40's freeze about 10 s in (when the first rise ended)
and why Test41 sometimes froze after 3 s: the short rise is 3 s.

## The fix

Every string the mod passes to a builtin or script thread is now **pinned**:
the mod holds one extra reference, as a script literal would. This covers:

- the notify names;
- model and weapon names;
- the helmet note;
- announcements.

**How it is pinned.** The pin is taken with exactly the call `Scr_AddString`
makes: `SL_GetString(text, 0)`, which passes type 7 (0x21a670). It is taken
once per string and level, and renewed on every level load.

**Checks.** The binding verifier checks every link in this chain:

- `IncInParam` releasing the out-params;
- `animscripted` reading the name, then pushing it again;
- `Scr_AddString`'s interning call and its type.

A Python test checks that every builtin and script-thread call pins its
strings first.

## Smaller changes

- **The crash guard skips the models for one run only.** A freeze left the
  marker in Test41, so later runs played without the models even though the
  freeze had nothing to do with them. The next run now tries the models again.
- **Hang dumps are named `hang-*.dmp`.** In Test41 they were named
  `crash-*.dmp`.
- **A new log line,** `ZOMBIE_STRINGS pinned_level=… pinned_total=…`.

## Still from Test41

These fixes are kept:

- the BO1 attacks without root motion;
- the model zone loading first;
- rooftop spawn handling;
- the hang watchdog and fault recorder.

**Testing:** No Windows gameplay test of Test42 yet.
