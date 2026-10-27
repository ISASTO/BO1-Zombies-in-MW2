#pragma once
// Test39: loads BO1 zombie animations from the owner's own Black Ops
// install (zone\Common\common_zombie.ff) at run time. Nothing from BO1 is
// shipped with the mod; the fastfile is read locally on a background thread.
#include "bo1_xanim.hpp"
#include <string>
#include <vector>

namespace mod::bo1assets {
enum class State {Idle,Loading,Ready,Missing,Failed};
struct Status {
    State state=State::Idle;
    std::string zone;          // the fastfile used (UTF-8)
    std::string detail;        // failure or summary text
    unsigned found=0,wanted=0,rejected=0;
    double seconds=0.0;
};
const char* StateText(State state);
// Every BO1 anim the Test39 swap table uses.
const std::vector<std::string>& WantedAnims();
// Starts the background load once (no-op afterwards).
void StartLoad();
// Waits for the background load; true when the anims are Ready.
bool WaitReady(unsigned milliseconds);
Status GetStatus();
// Valid once Ready; the records live for the rest of the process.
const bo1xanim::Anim* Find(const std::string& name);
} // namespace mod::bo1assets
