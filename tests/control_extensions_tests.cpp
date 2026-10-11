#include "control_extensions.hpp"
#include "core.hpp"
#include "player_controls.hpp"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    using namespace mod;
    PlayerPreferences player;
    Check(player.speed==380 && player.jump==117, "retain accepted default strengths");
    Check(player.adjust(PlayerFeature::SuperSpeed,1) && player.speed==475, "speed increases by half a base-speed unit");
    Check(!player.wanted(PlayerFeature::SuperSpeed), "arrow selection while OFF does not silently enable a cheat");
    for (int n=0;n<30;++n) player.adjust(PlayerFeature::SuperSpeed,1);
    Check(player.speed==950 && !player.adjust(PlayerFeature::SuperSpeed,1), "speed clamps at five times base speed");
    for (int n=0;n<30;++n) player.adjust(PlayerFeature::SuperSpeed,-1);
    Check(player.speed==190, "speed decrease clamps at normal speed");
    for (int n=0;n<30;++n) player.adjust(PlayerFeature::SuperJump,1);
    Check(player.jump==312 && ValidJumpPair(312.f,624.f), "jump clamps at eight times normal height");
    Check(!ValidJumpPair(117.f,78.f) && !ValidJumpPair(118.f,236.f), "inconsistent or unselected jump data fails closed");
    player.reset(); Check(player.speed==380 && player.jump==117, "reset clears requests and restores selector defaults");

    ExtraPreferences extras;
    Check(extras.timeScale==.5f && extras.gravity==400, "world starts with selected strengths and no active overrides");
    for (int n=0;n<static_cast<int>(ExtraFeature::Count);++n)
        Check(!extras.wanted(static_cast<ExtraFeature>(n)), "fresh process has no saved world or weapon cheat state");
    for (int n=0;n<20;++n) extras.adjust(ExtraFeature::SlowMotion,-1);
    Check(extras.timeScale==.25f && !extras.adjust(ExtraFeature::SlowMotion,-1), "time scale never reaches zero");
    for (int n=0;n<20;++n) extras.adjust(ExtraFeature::Gravity,-1);
    Check(extras.gravity==100, "gravity stays positive");
    extras.toggle(ExtraFeature::ExplosiveBullets); extras.toggle(ExtraFeature::FreezeAI);
    extras.resetWeapons(); Check(!extras.wanted(ExtraFeature::ExplosiveBullets) && extras.wanted(ExtraFeature::FreezeAI), "weapon reset preserves world state");
    extras.resetWorld(); Check(!extras.wanted(ExtraFeature::FreezeAI) && extras.timeScale==.5f && extras.gravity==400, "world reset clears only its group");

    ScalarOwnership<int> speed;
    speed.applied(0x2000,190,380);
    speed.applied(0x2000,380,475);
    Check(speed.original==190 && speed.injected==475 && speed.canRestore(0x2000,475), "strength adjustments retain the pre-override baseline");
    Check(!speed.canRestore(0x2000,200) && !speed.canRestore(0x3000,475), "engine changes or recycled dvars prevent stale restores");
    speed.applied(0x2000,200,570); Check(speed.original==200, "capture an intervening native value as the new baseline");
    Check(RapidFireTime(100)==50 && RapidFireTime(25)==20, "rapid fire halves normal timing with a positive floor");
    WeaponFields w; w.valid=true; w.index=12; w.complete=0x4000; w.definition=0x5000; w.type=0; w.fireTime=100;
    std::strcpy(w.name,"test_rifle");
    RapidOwnership rapid; rapid.applied(w,50); w.fireTime=50;
    Check(rapid.canRestore(w), "same catalog identity and injected timing may restore");
    w.index=13; Check(!rapid.canRestore(w), "recycled weapon index never receives another weapon baseline"); w.index=12;
    w.definition=0x6000; Check(!rapid.canRestore(w), "definition pointer changes invalidate ownership"); w.definition=0x5000;
    std::strcpy(w.name,"different_rifle"); Check(!rapid.canRestore(w), "name changes invalidate reused addresses"); std::strcpy(w.name,"test_rifle");
    w.fireTime=90; Check(!rapid.canRestore(w), "native timing edits survive cleanup");

    MenuState menu; menu.visible=true; menu.page=Page::Player; menu.selected=5;
    Check(menu.input(Input::Right)==Action::IncreaseSpeed && menu.input(Input::Left)==Action::DecreaseSpeed, "speed arrows route to strength adjustment");
    Check(menu.input(Input::Select)==Action::ToggleSuperSpeed, "Enter still toggles speed"); menu.selected=6;
    Check(menu.input(Input::Left)==Action::DecreaseJump, "jump left arrow routes correctly");
    menu.page=Page::Weapons; menu.selected=1;
    Check(menu.input(Input::Select)==Action::ToggleNoReload, "grenade row explicitly shares native ammo sustain");
    menu.page=Page::World; menu.selected=0;
    Check(menu.input(Input::Left)==Action::DecreaseTimeScale, "time scale slider routes correctly");
    menu.selected=3; Check(menu.input(Input::Select)==Action::None, "unverified enemy kill never queues an action");
    std::cout << "Adjustment bounds, scoped reset, ownership and weapon identity tests passed\n";
}
