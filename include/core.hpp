#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "bo1_round_rules.hpp"
#include "fov_preferences.hpp"

namespace mod {
inline constexpr const char* kVersion = "0.1.5-fov-test6";
inline constexpr const char* kExeSha256 = "fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777";

// -1 is a wildcard. Reject malformed patterns rather than matching everything.
inline std::vector<int> ParsePattern(const char* text) {
    std::vector<int> result;
    while (*text) {
        while (*text == ' ') ++text;
        if (!*text) break;
        if (*text == '?') {
            ++text;
            if (*text == '?') ++text;
            result.push_back(-1);
        } else {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(text[0]);
            if (hi < 0 || !text[1]) return {};
            int lo = hex(text[1]);
            if (lo < 0) return {};
            result.push_back(hi * 16 + lo);
            text += 2;
        }
        if (*text && *text != ' ') return {};
    }
    return result;
}
inline std::vector<std::size_t> FindPattern(const std::uint8_t* data, std::size_t length,
                                          const std::vector<int>& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > length) return matches;
    for (std::size_t i = 0; i <= length - pattern.size(); ++i) {
        bool equal = true;
        for (std::size_t j = 0; j < pattern.size(); ++j) {
            if (pattern[j] >= 0 && data[i + j] != pattern[j]) { equal = false; break; }
        }
        if (equal) matches.push_back(i);
    }
    return matches;
}
inline std::uintptr_t RipTarget(std::uintptr_t instruction, const std::uint8_t* bytes,
                                std::size_t displacementOffset, std::size_t instructionLength) {
    std::int32_t relative = 0;
    std::memcpy(&relative, bytes + displacementOffset, sizeof(relative));
    return instruction + instructionLength + relative;
}
// Profile byte sequences live in one place. Displacements are resolved at runtime.
inline constexpr const char* kSvRunningPattern =
    "C7 44 24 20 00 00 01 00 E8 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 00 20 00 00 E8 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 45 33 C0";
inline constexpr const char* kMapNamePattern =
    "41 B8 80 20 00 00 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2 48 89 05 ?? ?? ?? ?? 4C 8D 0D";
inline constexpr const char* kEntityPattern =
    "48 8B 3D ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 49 03 FE 49 8B D7 89 AF 50 01 00 00";
inline constexpr const char* kGodDamagePattern =
    "F6 83 50 01 00 00 01 0F 85 ?? ?? ?? ?? 48 8B 8B 10 01 00 00 83 FF 01";
inline constexpr const char* kHealthGuardPattern =
    "83 B9 64 01 00 00 00 7F 09 48 8D 0D ?? ?? ?? ?? EB AD";

inline constexpr const char* kFovRegistrationPattern =
    "F3 44 0F 10 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? F3 0F 10 1D ?? ?? ?? ?? 41 0F 28 D2 F3 0F 10 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 44 24 28 C7 44 24 20 44 00 00 00 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kFovCameraPattern =
    "48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 8B D7 F3 0F 10 78 10 E8 ?? ?? ?? ?? 85 C0 74 ?? F3 0F 10 76 28";

enum class Page { Home, Player, Weapons, World, AI, Zombies, Diagnostics, ZombieRules, View };
enum class Input { Toggle, Up, Down, Select, Back, Left, Right };
enum class Action { None, ToggleGod, ToggleHud, WriteSnapshot, WriteRoundPreview, ChangeFov, SaveFov, ResetFov };
struct MenuState {
    bool visible = false;
    bool hud = true;
    Page page = Page::Home;
    int selected = 0;
    int homeSelection = 0;
    int previewRound = bo1::kPreviewMinRound;
    FovPreference fov;
    int count() const {
        switch (page) {
            case Page::Home: return 6;
            case Page::Player: return 7;
            case Page::Weapons: return 5;
            case Page::World: return 4;
            case Page::AI: return 3;
            case Page::Zombies: return 4;
            case Page::Diagnostics: return 2;
            case Page::ZombieRules: return 6;
            case Page::View: return 5;
        }
        return 0;
    }
    int firstVisibleRow() const { return selected>=6 ? selected-5 : 0; }
    Action input(Input key) {
        if (key == Input::Toggle) { visible = !visible; return Action::None; }
        if (!visible) return Action::None;
        if (page == Page::View && selected == 0
            && (key == Input::Left || key == Input::Right || key == Input::Select))
            return fov.step(key==Input::Left?-1:1) ? Action::ChangeFov : Action::None;
        if (page == Page::ZombieRules && selected == 0
            && (key == Input::Left || key == Input::Right || key == Input::Select)) {
            const int oldRound = previewRound;
            if (key == Input::Left && previewRound > bo1::kPreviewMinRound) --previewRound;
            if (key != Input::Left && previewRound < bo1::kPreviewMaxRound) ++previewRound;
            return previewRound == oldRound ? Action::None : Action::WriteRoundPreview;
        }
        if (key == Input::Up) selected = (selected + count() - 1) % count();
        if (key == Input::Down) selected = (selected + 1) % count();
        if (key == Input::Back) {
            if (page == Page::Home) visible = false;
            else if (page == Page::ZombieRules) { page = Page::Zombies; selected = 0; }
            else if (page == Page::View) { page = Page::Player; selected = 1; }
            else { page = Page::Home; selected = homeSelection; }
        }
        if (key == Input::Select) {
            if (page == Page::Home) {
                homeSelection = selected;
                page = static_cast<Page>(selected + 1);
                selected = 0;
            } else if (page == Page::Player && selected == 0) return Action::ToggleGod;
            else if (page == Page::Player && selected == 1) { page=Page::View; selected=0; }
            else if (page == Page::View && selected == 1) return Action::SaveFov;
            else if (page == Page::View && selected == 2) return Action::ResetFov;
            else if (page == Page::Zombies && selected == 0) {
                page = Page::ZombieRules; selected = 0; return Action::WriteRoundPreview;
            }
            else if (page == Page::Diagnostics && selected == 0) {
                hud = !hud; return Action::ToggleHud;
            } else if (page == Page::Diagnostics && selected == 1) return Action::WriteSnapshot;
        }
        return Action::None;
    }
};
} // namespace mod
