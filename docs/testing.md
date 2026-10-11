# Test 1: F4 menu foundation

Build: `0.1.0-menu-test1`, exact September 16, 2026 AMD64 executable. This is an
initial diagnostic build, not an accepted or proven retail mod yet.

## Installation

1. Exit both MW2 executables.
2. Extract the whole test ZIP to an ordinary folder, such as Desktop\BO1MW2-test1.
3. Find the installed Single Player game folder through Steam's Browse button.
4. Run Install.cmd and paste that folder. It checks the executable SHA256 and
   refuses to replace an existing D3D9 wrapper.
5. Launch Single Player normally through Steam; no command-line switches,
   alternate client, injector, Bink replacement, or executable replacement.

The only installed runtime file is `<MW2>\d3d9.dll`. The installer also creates a
receipt and the diagnostics directory. Keep the `.ps1`/`.cmd` tools outside the
game directory. Manual DLL copying also works, with the limits in the README.

## Test sequence

| Step | Do this | Observe |
| --- | --- | --- |
| 1 | Reach the main menu | Upper-left BO1MW2/F4 badge appears; game UI works |
| 2 | Press F4, Up/Down, Enter, Backspace, F4 | Menu opens, selection moves, categories open/close |
| 3 | Load any campaign mission and wait for control | HUD reports active level and correct map; PLAYER/God changes from LOCKED to OFF/ON |
| 4 | Open F4 while a friendly NPC is walking/talking | NPC keeps moving/talking; game is not paused |
| 5 | Use WASD, mouse, and fire with the menu open | Gameplay still receives these inputs |
| 6 | Navigate all six categories | Planned entries do nothing; Diagnostics HUD toggle works |
| 7 | Close the menu using Esc at its home page | Menu closes; arrows/Enter/Esc behave normally in the game afterward |
| 8 | Open PLAYER, select God Mode, press Enter | Status turns ON; ordinary enemy gunfire no longer removes health |
| 9 | Turn God Mode off, close menu, take damage | Normal damage returns; record actual health before/after |
| 10 | Let the player die normally or reload a checkpoint | No crash; state/health refresh; do not assume God persists across reloads |
| 11 | Return to main menu, then load another mission | No crash; God is locked in main menu and correct map returns in mission |
| 12 | Optionally Alt-Tab once and change game resolution | Renderer recovers; text and controls still work |
| 13 | Exit and run CollectLogs.cmd | A ZIP contains the run log, receipt, and any crash files |

Step 12 tests device-reset handling; it is not required to operate the mod menu.
God Mode is tested against ordinary damage. Scripted mission failure, a kill
volume, or a cutscene may use a different path and does not prove the flag failed.
The flag is changed once per selection and is not reapplied continuously. The
display reads the actual current entity flag, including changes caused by reloads.

## Return these observations

Copy this into the next message and fill it in:

```
Launch: worked / failed before main menu / failed loading mission
Upper-left badge: visible / missing / unreadable
Mission:
Display mode and resolution:
F4 opens and closes:
Up/Down/Enter/Backspace work:
Esc closes and normal controls return:
NPCs keep moving; WASD/mouse/fire work while menu is open:
Displayed level state, map name, health:
God ON under ordinary gunfire:
God OFF takes damage again:
Checkpoint reload and second mission:
Any crash or rendering corruption:
```

Attach the results ZIP. For a crash, describe the last action, any error dialog,
and whether it happened before or after the badge appeared. A screenshot of the
menu is useful if its text or positioning is wrong.

If the game fails to launch, close it and remove this project's `d3d9.dll`. Copy
the log before relaunching; a new launch replaces `latest.log`. A main-menu load
without a badge should still be checked for a log, which identifies the last
completed stage: host, system DLL, factory, device, input, EndScene, Present, font,
and first draw.

The exception handler attempts a text crash report and a normal minidump for
unhandled faults reaching it. Steam/game handlers, early loader errors, or a
later filter replacement can prevent those files. If no crash dump exists,
return the last log and Windows Application Error's faulting module/offset.

## Acceptance gates

The first milestone is accepted only after retail testing confirms normal launch,
in-game menu rendering, working navigation, continuous gameplay, and correct
controls after closing. Phase 1 additionally requires correct level gating,
God-on/God-off damage observations, a checkpoint reload, and a second mission.
Further code changes should address observed failures, then repeat the affected
checks. BO1 AI and asset injection wait until this foundation passes.
