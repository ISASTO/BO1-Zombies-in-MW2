# FOV test6: selector and saved preference

Build `0.1.5-fov-test6`. This is the next small runtime test after accepted
health/God and preview work. Retail FOV behavior has not yet been observed.

## Installation

1. Close MW2. Extract the entire `BO1MW2-fov-test6.zip` outside the game folder.
2. Run this package's `Uninstall.cmd` to remove the previous project DLL, then
   `Install.cmd`. Paste the installed Steam MW2 Single Player folder containing
   `iw4sp.exe`: Properties > Installed Files > Browse. The installer checks its
   exact hash and installs `d3d9.dll` beside it. Keep all game files in place.
3. Launch Single Player normally through Steam. Load a campaign mission, ideally
   one other than estate, and wait until the player can move.

For a manual install, copy only this d3d9.dll beside iw4sp.exe after removing the
previous project copy. Never overwrite an unidentified wrapper. The scripts and
LoaderProbe.exe stay in the extracted test package.

## Focused test sequence

1. Press **F4 > PLAYER > FOV settings**. Base FOV initially selects **65** if
   no saved preference exists. Live base FOV displays the current native float;
   an unavailable field displays UNKNOWN and never enables a write.
2. Keep Base FOV selected and use **Right** five times to choose **90**. Left
   decreases by 5; Enter advances by 5. Values clamp at **60** and **120**.
   The view should widen and Live base FOV should become **90.0** shortly.
3. Press Down to **Save for next launch**, then Enter. Expect the footer
   **Saved for next launch** and Saved base FOV **90**. Close with F4.
4. Fully exit MW2 and launch it again through Steam. Enter a mission without
   selecting a new FOV. Open the FOV page: Base, Saved and Live should show 90.
   A saved preference applies only once the player is alive in an active level.
5. Check normal ADS and, if available, scoped zoom. Report any broken zoom or
   repeated camera jitter. Reload a checkpoint and confirm the selected base
   FOV returns/remains 90. Briefly check health/God ON/OFF and menu back/close.
6. Choose **Restore default / clear saved** and Enter. Saved should become NONE;
   Base and Live should return to **65**. This action clears the preference
   file, writes the verified native default once when ready, and releases the
   recurring override. It is distinct from selecting 65 and saving it.
7. Exit and run **Diagnose.cmd**. Return its ZIP plus the observations in
   [testing.md](testing.md). If resetting is the last action, the file may be
   absent from the ZIP; loaded/saved/applied/reset records remain in latest.log.

Up/Down selects rows. Backspace/Esc returns to PLAYER with FOV settings selected;
another Back returns home. The live/saved value rows are read-only. Player's
seventh placeholder scrolls into the six-row viewport. Opening the mod menu
does not pause movement, mouse look or firing.

## Persistence and implementation limits

The preference lives in `<MW2>\bo1mw2\settings.ini`, using version 1 and an
integer fov field. Save replaces this own file through a flushed temporary file
and rename; success is acknowledged only after completion. Invalid/truncated/
unknown-version content is ignored. A save failure appears in the footer/log.
Session edits do not silently replace the previously saved value.

Uninstall and upgrade keep logs and settings. The saved preference works while
the mod is installed. No clean-install FOV write occurs without a selected or
saved preference. No gameplay FOV write occurs at the main menu, during loading,
or while the player is dead. A queued selection is checked at most every 250ms
after readiness, including after checkpoint/mission resets.

The exact native registration is 65, domain 1-160, flags 0x44. The menu's 60-120
range is a deliberate subset. Independent registration/camera signatures must
resolve the same pointer slot; runtime checks the name, float type, finite native
value, writable alignment, executable hash and living-player gate. Only the
four-byte current float at +0x10 is changed atomically. Native setter APIs,
flags, reset/latched values, cg_fovScale and weapon zoom values are untouched.
The observed native camera reads base FOV before separate ADS/scale calculations;
cutscenes or other camera paths can still override the final view. Those runtime
interactions need owner evidence.

## BO1 research carried forward

The supplied common_zombie.ff now resolves selected normal-column tuning through
our original local StringTable reader. Health/count/pacing values match the
test5 preview, so no calculation changes are introduced. The provenance label
now says Local BO1 CSV + script fallbacks. Between-round 10s remains a GSC
fallback because its CSV key is absent; capacity 24 comes from the script's AI
limit, distinct from the CSV round-budget base. Special/map/runtime overrides
remain unaudited. No original assets are included in this package or repository.
