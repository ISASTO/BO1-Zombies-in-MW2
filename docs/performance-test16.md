# Scan and Ghost repair test16

Build: **0.1.15-performance-test16**. Test15 failed the owner's stutter/Ghost
checks. Its recorded world scans averaged **61,605 microseconds**, peaking at
**152,909 microseconds**. Server-thread scheduling alone did not remove that cost.

## Focused changes

- The already-copied entity arena now supplies all reads within its bounds,
  including serial checks and targetname fields. Those fields no longer fall
  through to repeated ReadProcessMemory calls.
- Classname/targetname text caches by entity allocation serial and string id.
  Recycled slots and changed ids invalidate the corresponding entry. Level
  lifetime changes and checkpoint reads invalidate the complete cache.
- Once the exact executable hash matches, VirtualQuery enumerates readable,
  committed MEM_IMAGE ranges of the main EXE. Reads wholly inside those permanent
  ranges use local copies. Guard/no-access/uncommitted ranges are excluded.
  Heap/string/asset reads outside the main EXE retain ReadProcessMemory checks.
  Write guards and native profile validation remain in place.
- Both player-feature and AI readers use that path. Player-state/actor fields
  and executable string literals no longer incur a syscall for every scalar read.
- Ghost's first attachment uses tag id 1 in the supplied log. Capture previously
  rejected empty tag text even when lookup succeeded. The native initializer
  interns an empty literal, stores its WORD id, and the native attach helper uses
  that id as its default. Test16 accepts that constant and valid empty text while
  retaining rejection of malformed/unreadable text.
- AI_SCAN_PERF separates pool-copy, decode/name and sort/status times. It reports
  kernel/image read counts and name-cache hits. PLAYER_READ_PERF reports read
  counts for player controls. The existing AI_PERF/WEAPON_PERF totals remain.

## Retail test

1. Close MW2, extract the complete ZIP, run Uninstall.cmd then Install.cmd, and
   restart the mission once.
2. Move and aim for 30 seconds before opening AI. Open AI, wait briefly for its
   first discovery, then close the menu and check the old periodic stutter.
3. Spawn 5-10 Soldiers and repeat ordinary movement/aiming for 30 seconds.
4. With Ghost present, select Ghost and spawn one helper. Allow roughly a second
   for appearance initialization. Check body/head/attachments and combat.
5. Briefly recheck No Recoil, Rapid Fire and Kill All Enemies. If practical, carry
   a few allies through an autosave to check the retained Test15 recovery path.
6. Close MW2 and run Diagnose.cmd before launching again. Return the ZIP and
   whether steady stutter/Ghost improved. A single first-discovery pause should
   be distinguished from continuing periodic stalls.

The Windows cross-build, portable bounds/full-arena/empty-tag fixtures, existing
tests and exact-executable audit run here. This workspace cannot run MW2, so no
post-change FPS or native Ghost result is claimed. Actor expansion remains
deferred. G18 appearance and global cross-mission character asset loading remain
pending. Existing Test15 autosave recovery and weapon controls are retained.
