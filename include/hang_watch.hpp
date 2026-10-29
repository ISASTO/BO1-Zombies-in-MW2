#pragma once
// Test41: freeze diagnostics. Test40 froze ~10 s after zombies started
// spawning and left no crash report: either the game stopped inside a loop or
// wait, or an engine handler swallowed a fault and showed a dialog hidden by
// the fullscreen window. This makes the next freeze self-explaining:
//  * a record-only vectored handler notes the first distinct faults (code,
//    instruction, faulting address, nearby return addresses); it never
//    handles anything (EXCEPTION_CONTINUE_SEARCH);
//  * a watchdog thread notices when neither a server frame nor a rendered
//    frame has happened for kHangMilliseconds during a zombies game while the
//    game is the foreground app, then logs the process's windows (and the text
//    of any error dialog, which it brings to the front), samples the main
//    and render threads (instruction pointer and return addresses), and
//    writes bo1mw2\logs\hang-<time>.txt/.dmp like a crash report.
// Nothing here changes game memory or control flow.
#include <cstdint>

namespace mod::hangwatch {
inline constexpr unsigned long long kHangMilliseconds=6000;
// Test45: a wait inside the graphics driver (under the Direct3D proxy) is
// only reported once it lasts this long.
inline constexpr unsigned long long kDriverWaitMilliseconds=30000;
inline constexpr unsigned kMaxReports=2;
void Start();                  // once the log exists; idempotent
void ServerFrame();            // every server frame (server clock hook)
void RenderFrame();            // every EndScene
void SetArmed(bool zombiesRunning);
void LogFaults();              // print new first-chance records (heartbeat)
} // namespace mod::hangwatch
