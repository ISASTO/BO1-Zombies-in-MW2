#include "core.hpp"
#include <cstdlib>
#include <iostream>
#include <array>

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    using namespace mod;
    const std::uint8_t bytes[] = {0x48,0x8b,0x10,0x90,0x48,0x8b,0x20,0x90};
    Check(FindPattern(bytes, sizeof(bytes), ParsePattern("48 8B ?? 90")).size()==2, "ambiguous signature remains ambiguous");
    Check(FindPattern(bytes, sizeof(bytes), ParsePattern("48 8B 20 90"))==std::vector<std::size_t>{4}, "match at end of region");
    Check(FindPattern(bytes, sizeof(bytes), ParsePattern("48 8B 21 90")).empty(), "missing signature");
    Check(ParsePattern("4G").empty() && ParsePattern("4").empty() && ParsePattern("??EE").empty(), "invalid pattern fails closed");
    Check(FindPattern(bytes, sizeof(bytes), {}).empty(), "empty pattern is never a match");
    const std::uint8_t backwards[] = {0x48,0x8b,0x05,0xf0,0xff,0xff,0xff};
    Check(RipTarget(0x1000,backwards,3,7)==0xff7, "negative RIP displacement");
    Check(FindPattern(bytes,2,ParsePattern("48 8B ?? 90")).empty(), "pattern exceeds readable region");

    MenuState m;
    m.input(Input::Select); Check(m.page==Page::Home, "hidden menu ignores gameplay inputs");
    m.input(Input::Toggle); m.input(Input::Up);
    Check(m.visible && m.selected==5, "home navigation wraps backwards");
    m.input(Input::Select); Check(m.page==Page::Diagnostics && m.selected==0, "enter selected category");
    Check(m.input(Input::Select)==Action::ToggleHud && !m.hud, "diagnostic toggle updates visible state");
    m.input(Input::Down); Check(m.input(Input::Select)==Action::WriteSnapshot, "diagnostic snapshot action");
    m.input(Input::Back); Check(m.page==Page::Home && m.selected==5, "back retains category selection");
    m.input(Input::Down); m.input(Input::Select);
    Check(m.page==Page::Player && m.input(Input::Select)==Action::ToggleGod, "God action routing");
    m.input(Input::Down); m.input(Input::Select);
    Check(m.page==Page::View && m.selected==0 && m.count()==5,"nested FOV page opens");
    Check(m.input(Input::Right)==Action::ChangeFov && m.fov.selected==70,"FOV keyboard action");
    m.input(Input::Down); Check(m.input(Input::Select)==Action::SaveFov,"save action routing");
    m.input(Input::Down); Check(m.input(Input::Select)==Action::ResetFov,"default action routing");
    m.input(Input::Down); Check(m.input(Input::Select)==Action::None,"live FOV output is read-only");
    m.input(Input::Down); Check(m.input(Input::Select)==Action::None,"saved FOV output is read-only");
    m.input(Input::Back); Check(m.page==Page::Player && m.selected==1,"FOV back remembers parent row");
    const std::array<Action,6> playerActions={Action::ToggleInfiniteAmmo,Action::ToggleNoReload,Action::ToggleNoclip,
        Action::ToggleSuperSpeed,Action::ToggleSuperJump,Action::ResetPlayer};
    for(const auto action:playerActions) { m.input(Input::Down); Check(m.input(Input::Select)==action,"player control action routing");
        Check(m.firstVisibleRow()==0,"all eight player rows fit without scrolling"); }
    Check(m.selected==7 && m.count()==8 && kVisibleMenuRows==8,"reset row and compact player viewport");
    m.input(Input::Down); Check(m.selected==0 && m.firstVisibleRow()==0,"player menu wraps after eight rows");
    m.input(Input::Back); m.input(Input::Back);
    Check(!m.visible, "back from home closes menu");
    m.input(Input::Toggle); m.input(Input::Toggle); Check(!m.visible, "two toggle events cancel");
    m.input(Input::Right); Check(m.previewRound==1, "closed menu ignores round input");
    m.input(Input::Toggle);
    for(int i=0;i<4;++i) m.input(Input::Down);
    m.input(Input::Select);
    Check(m.page==Page::Zombies && m.count()==4, "Zombies category includes preview");
    Check(m.input(Input::Select)==Action::WriteRoundPreview && m.page==Page::ZombieRules,
          "open nested read-only preview and log it");
    Check(m.input(Input::Left)==Action::None && m.previewRound==1, "minimum round clamps");
    Check(m.input(Input::Right)==Action::WriteRoundPreview && m.previewRound==2, "increment preview round");
    for(int i=0;i<110;++i) m.input(Input::Right);
    Check(m.previewRound==100 && m.input(Input::Select)==Action::None, "maximum round clamps");
    m.input(Input::Down);
    Check(m.input(Input::Select)==Action::None && m.input(Input::Left)==Action::None && m.previewRound==100,
          "read-only value rows never write or edit a game state");
    m.input(Input::Up); m.input(Input::Left);
    Check(m.previewRound==99, "decrement preview round");
    m.input(Input::Toggle); m.input(Input::Select); m.input(Input::Toggle);
    Check(m.page==Page::ZombieRules && m.previewRound==99, "close/reopen retains preview and ignores hidden select");
    m.input(Input::Back);
    Check(m.page==Page::Zombies && m.selected==0, "nested back returns to parent selection");
    for(int i=0;i<3;++i) { m.input(Input::Down); Check(m.input(Input::Select)==Action::None, "Zombies gameplay remains unimplemented"); }
    m.input(Input::Back);
    Check(m.page==Page::Home && m.selected==4, "second back returns to remembered home category");
    std::cout << "All signature and menu tests passed\n";
}
