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
    Check(extras.timeScale==1.f && extras.gravity==400, "world starts at normal speed with no active overrides");
    for (int n=0;n<static_cast<int>(ExtraFeature::Count);++n)
        Check(!extras.wanted(static_cast<ExtraFeature>(n)), "fresh process has no saved world or weapon cheat state");
    for (int n=0;n<20;++n) extras.adjust(ExtraFeature::WorldSpeed,-1);
    Check(extras.timeScale==.1f && !extras.adjust(ExtraFeature::WorldSpeed,-1), "world speed clamps at one tenth");
    for(int n=0;n<150;++n)extras.adjust(ExtraFeature::WorldSpeed,1);
    Check(extras.timeScale==10.f && !extras.adjust(ExtraFeature::WorldSpeed,1) && !extras.wanted(ExtraFeature::WorldSpeed),"world speed reaches ten times without enabling itself");
    for(int n=0;n<90;++n)extras.adjust(ExtraFeature::WorldSpeed,-1);
    Check(extras.timeScale==1.f,"repeated tenth steps return exactly to normal speed");
    for (int n=0;n<20;++n) extras.adjust(ExtraFeature::Gravity,-1);
    Check(extras.gravity==100, "gravity stays positive");
    extras.toggle(ExtraFeature::ExplosiveBullets); extras.toggle(ExtraFeature::FreezeAI);
    extras.resetWeapons(); Check(!extras.wanted(ExtraFeature::ExplosiveBullets) && extras.wanted(ExtraFeature::FreezeAI), "weapon reset preserves world state");
    extras.resetWorld(); Check(!extras.wanted(ExtraFeature::FreezeAI) && extras.timeScale==1.f && extras.gravity==400, "world reset clears only its group");
    extras.toggle(ExtraFeature::FriendlyFire);extras.resetWeapons();
    Check(extras.wanted(ExtraFeature::FriendlyFire),"weapon reset preserves friendly fire in the World group");
    extras.resetWorld();Check(!extras.wanted(ExtraFeature::FriendlyFire),"World reset disables the friendly-fire override");
    extras.toggle(ExtraFeature::Hitmarkers);extras.toggle(ExtraFeature::SemiAutoFullAuto);extras.toggle(ExtraFeature::InvincibleFriendlies);
    extras.resetWeapons();
    Check(!extras.wanted(ExtraFeature::Hitmarkers) && !extras.wanted(ExtraFeature::SemiAutoFullAuto)
        && extras.wanted(ExtraFeature::InvincibleFriendlies),"weapon reset clears feedback and firing mode, retaining the separate AI protection toggle");

    ScalarOwnership<int> speed;
    speed.applied(0x2000,190,380);
    speed.applied(0x2000,380,475);
    Check(speed.original==190 && speed.injected==475 && speed.canRestore(0x2000,475), "strength adjustments retain the pre-override baseline");
    Check(!speed.canRestore(0x2000,200) && !speed.canRestore(0x3000,475), "engine changes or recycled dvars prevent stale restores");
    speed.applied(0x2000,200,570); Check(speed.original==200, "capture an intervening native value as the new baseline");
    ScalarOwnership<std::uintptr_t> handDefinition;
    handDefinition.applied(0x3008,0,0x4000);
    handDefinition.applied(0x3008,0x4000,0x5000);
    Check(handDefinition.original==0 && handDefinition.canRestore(0x3008,0x5000),
        "upgrading compatibility hand to a distinct native definition retains original empty cache for OFF");
    Check(!handDefinition.canRestore(0x3008,0x6000),
        "foreign hand-definition replacement cannot be claimed for upgrade or undo");
    Check(RapidFireTime(100)==50 && RapidFireTime(25)==12 && RapidFireTime(75,10)==7 && RapidFireTime(2,10)==1, "rapid fire supports ten times timing with a positive floor");
    for(int n=0;n<30;++n)extras.adjust(ExtraFeature::RapidFire,1);
    Check(extras.rapidFireMultiplier==10 && !extras.adjust(ExtraFeature::RapidFire,1) && !extras.wanted(ExtraFeature::RapidFire),"rapid selector clamps without enabling itself");
    for(int n=0;n<30;++n)extras.adjust(ExtraFeature::RapidFire,-1);
    Check(extras.rapidFireMultiplier==1 && RapidFireTime(75,extras.rapidFireMultiplier)==75,"one times preserves native timing");
    extras.toggle(ExtraFeature::NoRecoil);extras.resetWeapons();
    Check(extras.rapidFireMultiplier==2 && !extras.wanted(ExtraFeature::NoRecoil),"weapon reset restores rate default and clears recoil");
    WeaponFields w; w.valid=true; w.index=12; w.complete=0x4000; w.definition=0x5000; w.type=0; w.fireTime=100;
    std::strcpy(w.name,"test_rifle");
    RapidOwnership rapid; rapid.applied(w,50); w.fireTime=50;
    Check(rapid.canRestore(w), "same catalog identity and injected timing may restore");
    rapid.applied(w,10);w.fireTime=10;
    Check(rapid.original==100 && rapid.canRestore(w),"changing rate retains the native timing baseline");
    w.index=13; Check(!rapid.canRestore(w), "recycled weapon index never receives another weapon baseline"); w.index=12;
    w.definition=0x6000; Check(!rapid.canRestore(w), "definition pointer changes invalidate ownership"); w.definition=0x5000;
    std::strcpy(w.name,"different_rifle"); Check(!rapid.canRestore(w), "name changes invalidate reused addresses"); std::strcpy(w.name,"test_rifle");
    w.fireTime=90; Check(!rapid.canRestore(w), "native timing edits survive cleanup");
    w.fireMode=kSemiAutomaticFireMode;
    FireModeOwnership mode;mode.applied(w);w.fireMode=kAutomaticFireMode;
    Check(mode.original==1 && mode.canRestore(w),"automatic conversion retains the exact original firing mode");
    w.definition+=8;Check(!mode.canRestore(w),"recycled definitions cannot receive stale semi-auto restores");w.definition-=8;
    w.fireMode=3;Check(!mode.canRestore(w),"an intervening native burst mode survives cleanup");

    MenuState menu; menu.visible=true; menu.page=Page::Player; menu.selected=3;
    Check(menu.input(Input::Right)==Action::IncreaseSpeed && menu.input(Input::Left)==Action::DecreaseSpeed, "speed arrows route to strength adjustment");
    Check(menu.input(Input::Select)==Action::ToggleSuperSpeed, "Enter still toggles speed"); menu.selected=4;
    Check(menu.input(Input::Left)==Action::DecreaseJump, "jump left arrow routes correctly");
    menu.page=Page::Weapons; menu.selected=3;
    Check(menu.input(Input::Select)==Action::ToggleNoReload, "grenade row explicitly shares native ammo sustain");
    menu.selected=4;
    Check(menu.input(Input::Right)==Action::IncreaseRapidFire && menu.input(Input::Left)==Action::DecreaseRapidFire,"rapid fire arrows select the multiplier");
    menu.selected=7;Check(menu.input(Input::Select)==Action::ToggleNoRecoil,"no recoil routes independently from weapon info");
    menu.selected=8;Check(menu.input(Input::Select)==Action::ToggleHitmarkers,"hitmarker toggle routes separately");
    menu.selected=9;Check(menu.input(Input::Select)==Action::ToggleSemiAutoFullAuto,"held-fire conversion routes separately");
    menu.selected=11;Check(menu.count()==12 && menu.input(Input::Select)==Action::ResetWeapons,"weapon reset remains the final visible row");
    menu.page=Page::World; menu.selected=0;
    Check(menu.input(Input::Left)==Action::DecreaseTimeScale, "time scale slider routes correctly");
    menu.selected=3; Check(menu.input(Input::Select)==Action::KillAllEnemies, "enemy kill routes to the native service");
    menu.selected=4;Check(menu.input(Input::Select)==Action::ToggleFriendlyFire && menu.count()==6,"friendly fire occupies its own World row");
    menu.selected=5;Check(menu.input(Input::Select)==Action::ResetWorld,"World reset remains accessible after adding friendly fire");
    std::cout << "Adjustment bounds, scoped reset, ownership and weapon identity tests passed\n";
}
