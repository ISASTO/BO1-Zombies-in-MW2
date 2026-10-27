#pragma once
// Test40: the BO1 zombie character models, generated as a MW2 fastfile from
// the owner's own Black Ops install (zone\Common\zombie_theater.ff and the
// textures in main\*.iwd) on a background thread, then cached in
// bo1mw2\cache\ until a source file changes.
#include <array>
#include <string>

namespace mod::bo1models {
enum class State {Idle,Building,Ready,Missing,Failed};
struct Status {
    State state=State::Idle;
    bool cached=false;                   // reused a previous build
    std::string detail;                  // failure text or summary
    std::string file;                    // the generated fastfile (UTF-8)
    unsigned textures=0,fallbacks=0;
    std::size_t bytes=0;
    double seconds=0.0;
};
const char* StateText(State state);
void StartBuild();
// Waits for the build; true when the fastfile is Ready.
bool WaitReady(unsigned milliseconds);
Status GetStatus();
// DB_LoadXAssets zone name for the fastfile: zone\<language>\ is prepended by
// the engine, so the name climbs back to the MW2 folder.
inline constexpr const char* kZoneName="..\\..\\bo1mw2\\cache\\bo1_zombie_models";
// BO1 c_ger_honorguard_zomb_bodyalias: body1 once, body2 three times.
inline constexpr std::array<const char*,4> kBodyAlias={"c_ger_honorguard_body1","c_ger_honorguard_body2",
    "c_ger_honorguard_body2","c_ger_honorguard_body2"};
inline constexpr std::array<const char*,4> kHeadAlias={"c_ger_zombie_head1","c_ger_zombie_head2","c_ger_zombie_head3","c_ger_zombie_head4"};
} // namespace mod::bo1models
