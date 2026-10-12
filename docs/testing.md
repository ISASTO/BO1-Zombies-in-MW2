# Current owner checks

Current build: **BO1MW2-squad-test17.zip**, version `0.1.16-squad-test17`.
Follow [squad-test17.md](squad-test17.md) for installation, autosave continuity,
death/relaunch ownership, friendly fire and removal checks.

Test16 performance is owner-confirmed; its returned scan timing evidence is in
[runtime-test16-evidence.json](runtime-test16-evidence.json). Keep that frame
pacing as a regression check while exercising Test17's native save changes.

The coordinated 256-slot attempt is documented in
[actor-capacity-audit.md](actor-capacity-audit.md). The isolated allocator/iteration
probe passes 256, but perception corrupts actor state and snapshot/save layouts
remain blockers. **Runtime capacity stays 32.**

Saved FOV persists. Other toggles start OFF after a full process restart. Close
the game and run Diagnose before relaunching to preserve the just-tested log.
No Windows game execution is performed by the Linux build/portable/probe checks.
