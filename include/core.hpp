#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "bo1_round_rules.hpp"
#include "fov_preferences.hpp"

namespace mod {
inline constexpr const char* kVersion = "0.1.14-performance-test15";
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

inline constexpr const char* kClientReadinessPattern =
    "4C 8B B1 08 01 00 00 48 8B F1 4C 89 B5 F0 00 00 00 41 83 BE A4 AC 00 00 02 0F 85 ?? ?? ?? ?? 41 8B 56 0C 45 8B 86 54 AD 00 00";
inline constexpr const char* kClientDamageGuardPattern =
    "F6 45 10 01 0F 85 ?? ?? ?? ?? F6 85 54 AD 00 00 03 0F 85 ?? ?? ?? ?? 41 83 FF 0B";
inline constexpr const char* kClientInvulnerabilityClearPattern =
    "E8 ?? ?? ?? ?? 48 8B 83 08 01 00 00 83 60 10 FE 48 83 C4 20 5B C3";
inline constexpr const char* kNoclipCommandPattern =
    "4C 8B 93 08 01 00 00 48 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 45 8B 8A 54 AD 00 00 45 8B C1 41 83 E0 01";
inline constexpr const char* kNoclipMovementPattern =
    "41 F6 C0 01 74 07 B8 02 00 00 00 EB 61 41 F6 C0 02 74 07 B8 03 00 00 00 EB 54";
inline constexpr const char* kReserveReadPattern =
    "48 8D 83 D8 02 00 00 0F 1F 44 00 00 39 10 74 15 48 FF C1 48 83 C0 08 48 83 F9 0F 7C EF 33 C0";
inline constexpr const char* kReserveWritePattern =
    "48 8D 3C C7 89 9F D8 02 00 00 89 B7 DC 02 00 00 48 8B 5C 24 30";
inline constexpr const char* kSustainRegistrationPattern =
    "33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 C4 00 00 00 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8D 05";
inline constexpr const char* kSustainConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? 45 8B F8 4D 63 F1 48 8B E9 8B DA 8B F2 80 78 10 00 0F 84 ?? ?? ?? ?? 8B CE";
inline constexpr const char* kSpeedRegistrationPattern =
    "48 8D 0D ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? BA BE 00 00 00 48 89 44 24 28 41 B9 FF FF FF 7F 41 B8 00 00 00 80 C7 44 24 20 C0 00 00 00 E8 ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kSpeedConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? F3 0F 5E C7 8B 48 10 41 89 4E 5C";
inline constexpr const char* kJumpRegistrationPattern =
    "F3 0F 10 0D ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 44 24 28 0F 57 D2 C7 44 24 20 84 00 00 00 E8 ?? ?? ?? ?? F3 0F 10 1D ?? ?? ?? ?? 48 8D 0D";
inline constexpr const char* kJumpImpulsePattern =
    "66 0F 6E 4F 54 0F 5B C9 F3 0F 59 0D ?? ?? ?? ?? 74 57";
inline constexpr const char* kJumpClampPattern =
    "F3 0F 10 91 80 00 00 00 F3 0F 58 15 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? F3 0F 5C D3";
inline constexpr const char* kJumpStepPattern =
    "F3 0F 10 81 80 00 00 00 F3 0F 10 1D ?? ?? ?? ?? F3 0F 58 C3 0F 2F 42 08";
inline constexpr const char* kJumpCeilingPattern =
    "F3 0F 10 89 80 00 00 00 F3 0F 58 0D ?? ?? ?? ?? F3 0F 10 41 24 0F 2F C1 0F 93 C0 C3";

inline constexpr const char* kGiveAllRegistrationPattern =
    "4C 8D 0D ?? ?? ?? ?? 33 D2 41 B8 84 20 00 00 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kGiveAllConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? 48 8B D9 48 63 F2 48 85 C0 74 ?? 80 78 10 00 74 ?? B8 0E 00 00 00 EB ?? 44 8B 81 B4 02 00 00";
inline constexpr const char* kExplosiveRegistrationPattern =
    "33 D2 44 8D 47 0C 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 44 0F 10 0D ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kExplosiveConsumerPattern =
    "80 B9 69 08 00 00 00 75 ?? 48 8B 05 ?? ?? ?? ?? 80 78 10 00 75 ?? 32 C0 C3 B0 01 C3";
inline constexpr const char* kTimeRegistrationPattern =
    "48 8D 0D ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 48 89 5C 24 28 48 89 05 ?? ?? ?? ?? C7 44 24 20 0C 00 00 00 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kTimeConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? 7A ?? 75 ?? 0F 2E DA 7A ?? 75 ?? F3 0F 10 40 10 0F 2E C2 7A ?? 75 ?? C7 05 ?? ?? ?? ?? 00 00 80 3F EB ?? F3 0F 59 CB 66 0F 6E F7 0F 5B F6 F3 0F 59 48 10";
inline constexpr const char* kAiRegistrationPattern =
    "4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 41 B8 84 00 00 00 48 8D 0D ?? ?? ?? ?? B2 01 E8 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kAiConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? 48 8B E9 80 78 10 00 75 ?? 8B 05 ?? ?? ?? ?? 83 C0 32 89 81 60 01 00 00 48 81 C4 A0 00 00 00 5D C3";
inline constexpr const char* kGravityRegistrationPattern =
    "F3 0F 10 35 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 0F 28 DF 48 8D 05 ?? ?? ?? ?? 41 0F 28 D4 48 89 44 24 28 0F 28 CE C7 44 24 20 80 00 00 00 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kGravityTrajectoryPattern =
    "48 8B 05 ?? ?? ?? ?? F3 0F 10 40 10 F3 0F 59 05 ?? ?? ?? ?? F3 0F 59 C3 F3 0F 59 C3 F3 0F 5C D0 F3 0F 11 57 08";
inline constexpr const char* kPlayerGravityPattern =
    "F3 0F 10 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 10 3D ?? ?? ?? ?? 33 D2 45 8B BE B0 00 00 00 41 B8 B4 00 00 00 F3 0F 2C C0 44 89 BD E0 00 00 00 F3 41 0F 10 86 C4 02 00 00 41 89 46 54 48 8B 05 ?? ?? ?? ??";
inline constexpr const char* kWeaponCatalogPattern =
    "48 83 EC 28 33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 40 06 00 00 E8 ?? ?? ?? ?? 33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 40 06 00 00 E8 ?? ?? ?? ?? 33 D2";
inline constexpr const char* kWeaponCountPattern =
    "8B 1D ?? ?? ?? ?? 4C 8D 35 ?? ?? ?? ?? 85 DB 74 ?? 8B 05 ?? ?? ?? ?? 3B D8 77 1D 49 8B 04 DE";
inline constexpr const char* kWeaponPointersPattern =
    "8B C1 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1 C3 CC CC 48 8D 05 ?? ?? ?? ?? 48 8B 04 C8 C3";
inline constexpr const char* kWeaponBindingPattern =
    "48 8B 48 08 48 89 0D ?? ?? ?? ?? 48 89 0D ?? ?? ?? ??";
inline constexpr const char* kWeaponFireScriptPattern =
    "8B CF E8 ?? ?? ?? ?? 66 0F 6E 40 3C 0F 5B C0 F3 0F 59 05 ?? ?? ?? ??";
inline constexpr const char* kWeaponFireConsumerPattern =
    "49 6B EE 1C 89 84 2F F0 01 00 00 41 8B 47 3C 89 84 2F EC 01 00 00 80 BE 6D 08 00 00 00";
inline constexpr const char* kGrenadeSustainPattern =
    "48 8B D3 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 48 8B D3 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ??";

inline constexpr const char* kDualRegistrationPattern =
    "4C 8D 0D ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 44 8D 42 0C E8 ?? ?? ?? ?? 33 D2 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kDualConsumerPattern =
    "48 8B 05 ?? ?? ?? ?? 40 38 70 10 75 ?? 8B D5 48 8B CF E8 ?? ?? ?? ?? 8B CE 85 C0 0F 95 C1 89 8F D4 02 00 00 EB ?? 41 38 B4 24 73 08 00 00 74 ?? 89 B7 D4 02 00 00 41 88 76 01 EB";
inline constexpr const char* kWeaponSlotGrantPattern =
    "48 8D 04 8E 89 94 01 5C 02 00 00 88 94 01 60 02 00 00 44 89 B4 8E 20 02 00 00 41 8B CE";
inline constexpr const char* kWeaponMetadataPattern =
    "8B C8 49 8D 81 5C 02 00 00 48 8D 04 88 48 03 C1 C3";
inline constexpr const char* kAmmoIndexPattern =
    "48 83 EC 28 48 63 C9 E8 ?? ?? ?? ?? 8B 80 48 03 00 00 48 83 C4 28 C3";
inline constexpr const char* kClipIndexPattern =
    "49 8B CE E8 ?? ?? ?? ?? 49 8B CD 8B 90 58 03 00 00 48 8B C6";
inline constexpr const char* kClipSizePattern =
    "48 83 EC 28 8B CA E8 ?? ?? ?? ?? 8B 40 34 48 83 C4 28 C3";
inline constexpr const char* kPlayableWeaponPattern =
    "8B 50 58 83 EA 08 F7 C2 FD FF FF FF 0F 84 ?? ?? ?? ?? 83 78 60 05 0F 84 ?? ?? ?? ?? 48 8B 48 08 4A 83 3C F1 00 0F 84 ?? ?? ?? ??";
inline constexpr const char* kAlternateChainPattern =
    "E8 ?? ?? ?? ?? 8B 58 58 85 DB 74 ?? 8B D3 48 8B CF E8 ?? ?? ?? ?? 85 C0 75 ??";
inline constexpr const char* kClipPoolPattern =
    "4C 8D 99 50 03 00 00 45 33 D2 49 8B C3 0F 1F 00 39 10 74 ?? 49 FF C2 48 83 C0 0C 49 83 FA 0F 7C ?? 33 C9 49 8B C3 83 38 00 74 ?? 48 FF C1 48 83 C0 0C 48 83 F9 0F 7C ?? 49 63 C0 41 89 13 45 01 4C 83 04 C3";

inline constexpr const char* kDualAnimSupportPattern =
    "48 8B 41 18 48 8B 48 08 33 C0 38 01 0F 95 C0 C3";
inline constexpr const char* kDualAnimSelectPattern =
    "49 8B 52 18 48 8B 4A 08 80 39 00 74 ?? 45 33 C0 48 8D 0C FD 00 00 00 00 85 F6 75 ?? 48 8B 04 11 44 38 00 EB ?? 49 8B 42 20 48 8B 0C 01 44 38 01 EB ?? 48 8B 40 20 45 33 C0 48 8B 14 F8 44 38 02";
inline constexpr const char* kDualAnimCacheReadPattern =
    "48 8D 05 ?? ?? ?? ?? 48 83 3C F0 00 75 ?? 8B CF E8 ?? ?? ?? ?? 4C 8B C0 48 8D 15 ?? ?? ?? ?? B9 01 00 00 00 E8 ?? ?? ?? ??";
inline constexpr const char* kDualAnimCacheResetPattern =
    "33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 C0 12 00 00 E8 ?? ?? ?? ?? 49 69 CE C0 2B 00 00";
inline constexpr const char* kDualAnimCacheStridePattern =
    "48 8D 04 6D 00 00 00 00 48 03 C5 41 83 7E 1C 00 4D 8D 24 C4 75 ?? 0F 57 C0 33 C0";
inline constexpr const char* kWeaponAnimBuildPattern =
    "BB 01 00 00 00 48 8D 7D 08 0F 1F 84 00 00 00 00 00 4C 8B 07 41 80 38 00 75 ?? 4C 8B 45 08 8B D3 48 8B CE E8 ?? ?? ?? ?? FF C3 48 83 C7 08 83 FB 25 7C ??";

inline constexpr const char* kAiActorThinkStartPattern =
    "40 55 48 81 EC A0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 8B E9 80 78 10 00 75 18 8B 05 ?? ?? ?? ?? 83 C0 32 89 81 60 01 00 00 48 81 C4 A0 00 00 00 5D C3";
inline constexpr const char* kAiThinkDispatchPattern =
    "0F B6 81 3B 01 00 00 48 89 7C 24 30 48 8D 3D ?? ?? ?? ?? C7 81 60 01 00 00 00 00 00 00 48 8D 14 80 48 03 D2 48 8B 3C D7 48 85 FF 75 0F 48 8D 15 ?? ?? ?? ?? 8D";
inline constexpr const char* kAiSpawnerArrayPattern =
    "8B 05 ?? ?? ?? ?? 33 DB 85 C0 7E 64 48 8B 15 ?? ?? ?? ?? 48 89 74 24 30 48 8D 35 ?? ?? ?? ?? 48 89 7C 24 38 8B FB 90 80 3E 00 74 2A 80 3C 17 0E 75 24 48 63 C3 48 69";
inline constexpr const char* kAiSpawnerFieldsPattern =
    "80 3F 00 74 36 80 3C 16 0E 75 30 8B 84 16 78 01 00 00 0F A3 C5 73 24 48 63 C3 48 69 C8 A0 02 00 00 48 03 CA E8 90 9A";
inline constexpr const char* kAiNormalSpawnMethodPattern =
    "44 8B CE 45 33 C0 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B 7C 24 38 48 8B 74 24 30 48 85 C0 74 19 8B 0D ?? ?? ?? ?? 89 8B 7C 01 00 00 48 8B C8 48 83 C4 20 5B E9 0B 95";
inline constexpr const char* kAiSpawnCorePattern =
    "48 89 5C 24 18 55 57 41 56 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 41 8B F9 8B DA 48 8B E9 80 78 10 00 0F 85 C4 01 00 00 83 B9 84 01 00 00 00 0F 84 B7 01 00 00 45 85 C0 75 48 44 39 05 ?? ?? ?? ?? 75 3F 48 8B D1 48 8D 0D C5 B1";
inline constexpr const char* kAiActorInitPattern =
    "40 56 57 41 57 48 81 EC A0 00 00 00 45 33 FF 48 8B F9 66 44 89 B9 84 02 00 00 48 8B 05 ?? ?? ?? ?? 44 38 78 10 0F 84 A4 05 00 00 F6 81 4C 01 00 00 01";
inline constexpr const char* kAiActorLinksPattern =
    "48 89 B7 10 01 00 00 48 89 AF 18 01 00 00 44 89 BF 60 01 00 00 8B 05 ?? ?? ?? ?? 80 8F AE 00 00 00 02 81";
inline constexpr const char* kAiActorHandlerPattern =
    "C6 87 3B 01 00 00 02 C6 87 39 01 00 00 01 C7 87 68 01 00 00 64 00 00 00 C7 87 64 01 00 00 64 00 00 00";
inline constexpr const char* kAiDeleteMethodPattern =
    "89 4C 24 08 48 83 EC 28 8B C1 C1 E8 10 66 83 F8 05 75 09 48 83 C4 28 E9 ?? ?? ?? ?? 66 83 7C 24 32 00 48 89 5C 24 20 75 16 0F B7 C1 48 69 D8 A0 02 00 00 48 8D 05 ?? ?? ?? ?? 48 03 D8 EB ?? E8 ?? ?? ?? ?? 33 DB";
inline constexpr const char* kAiDeleteGuardPattern =
    "0F B7 43 7E 39 05 ?? ?? ?? ?? 75 05 E8 ?? ?? ?? ?? 0F B7 15 ?? ?? ?? ?? 45 33 C0 48 8B CB E8 ?? ?? ?? ?? 48 8B CB 48 8B 5C 24 20 48 83 C4 28 E9 ?? ?? ?? ??";
inline constexpr const char* kAiEntitySerialPattern =
    "8B 7B 78 33 D2 41 B8 A0 02 00 00 48 8B CB E8 ?? ?? ?? ?? 48 8B C3 48 2B 05 ?? ?? ?? ?? 48";
inline constexpr const char* kAiEntitySerialCommitPattern =
    "8D 47 01 48 8B 7C 24 48 89 43 78 48 8B 5C 24 30 42 88 2C 36 48 8B";
inline constexpr const char* kAiStringConvertPattern =
    "85 C9 74 12 48 8B 05 ?? ?? ?? ?? 48 83 C0 04 C1 E1 04 48 03 C1 C3 33 C0 C3";
inline constexpr const char* kAiSpawnEnabledRegistrationPattern =
    "41 B8 86 00 00 00 48 8D 0D ?? ?? ?? ?? B2 01 E8 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";
inline constexpr const char* kAiSpawnDisabledRegistrationPattern =
    "33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 84 00 00 00 E8 ?? ?? ?? ?? F3 44 0F 10 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??";

inline constexpr const char* kAiNormalSpawnEntryPattern =
    "40 53 48 83 EC 20 E8 ?? ?? ?? ?? 48 8B D8 80 38 0E 74 05 E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 39 8B 7C 01 00 00 7D 6F";

inline constexpr const char* kAiSentientLinksPattern =
    "48 89 11 44 89 41 08 48 85 D2 74 13 48 8B 82 10 01 00 00 48 85 C0 74 07 44 89 80 78 13 00 00 33 C0";

inline constexpr const char* kAiActorTypePattern =
    "C6 07 0D 66 89 9F 80 00 00 00 F3 41 0F 10 0E F3 41 0F 10 46 04 F3 41 0F 10 56 08";

inline constexpr const char* kAiActorSelfPattern =
    "48 89 3E 33 D2 48 89 6E 08 41 B8 0C 01 00 00 E8 ?? ?? ?? ?? 48 8B 0E 4C 89 BE 84 0E 00 00";

inline constexpr const char* kAiSpawnCopyCallsPattern =
    "E8 ?? ?? ?? ?? 48 8B D5 48 8B C8 4C 8B F0 E8 ?? ?? ?? ?? 48 8B D5 49 8B CE E8 ?? ?? ?? ?? 49 8D 8E 46 01 00 00";

inline constexpr const char* kAiCopyFieldsStartPattern =
    "48 89 74 24 18 57 48 83 EC 20 48 83 3D ?? ?? ?? ?? 00 48 8B FA 48 8B F1 0F 84 91 00 00 00 48 89 5C 24 30 48 8D 1D ?? ?? ?? ?? 48 89 6C 24 38 48";

inline constexpr const char* kAiCopyDynamicStartPattern =
    "48 8B C2 45 33 C0 0F B7 51 7E 0F B7 48 7E E9 ?? ?? ?? ??";

inline constexpr const char* kAiTraceStartPattern =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 60 48 8B F2 48 8B E9 8B 94 24 90 00 00 00 48 8D 4C 24 50 49 8B D9 49 8B F8 E8 ?? ?? ?? ??";

inline constexpr const char* kAiTraceScriptCallPattern =
    "4C 8D 0D ?? ?? ?? ?? C7 44 24 28 15 00 82 00 4C 8D 44 24 30 C7 44 24 20 FF 07 00 00 48 8D 54 24 40 48 8D 4C 24 60 E8 ?? ?? ?? ??";

inline constexpr const char* kAiTraceLayoutPattern =
    "48 83 EC 48 48 8B 44 24 78 0F 57 C0 0F 11 01 48 89 44 24 30 8B 44 24 70 0F 11 41 10 89 44 24 28 0F 11 41 20 C7 01 00 00 80 3F C7 44 24 20 00 00 00 00 E8 ?? ?? ?? ??";

inline constexpr const char* kAiPositionSetterPattern =
    "8B 02 89 41 1C 8B 42 04 89 41 20 8B 42 08 89 41 24 33 C0 48 89 41 10 89 41 18 48 89 41 28 89 41 30 8B 02 89 81 E8 00 00 00 8B 42 04 89 81 EC 00 00 00 8B 42 08 89 81 F0 00 00 00 C3";

inline constexpr const char* kAiPositionMethodCallPattern =
    "48 8D 54 24 20 48 8B CB E8 ?? ?? ?? ?? 48 8B 8B 18 01 00 00 48 85 C9 74 05 E8 05 11 02";

inline constexpr const char* kAiTeamChangePattern =
    "48 8B 01 89 51 08 48 85 C0 74 12 48 8B 80 10 01 00 00 48 85 C0 74 06 89 90 78 13 00 00 C3";

inline constexpr const char* kAiTeamScriptSetterPattern =
    "40 53 48 83 EC 20 48 8B D9 33 C9 E8 ?? ?? ?? ?? 8B C8 E8 ?? ?? ?? ?? 8B D0 48 8B CB 48 83 C4 20 5B E9 ?? ?? ?? ??";

inline constexpr const char* kAiGoalPositionPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B FA 48 8B D9 48 81 C1 C0 14 00 00 33 D2 49 8B F0 E8 ?? ?? ?? ??";

inline constexpr const char* kAiGoalScriptCallPattern =
    "48 8D 54 24 20 33 C9 E8 ?? ?? ?? ?? 45 33 C0 48 8D 54 24 20 48 8B CB E8 ?? ?? ?? ?? 48 83 C4 30 5B C3";

inline constexpr const char* kAiActorCapacityPattern =
    "48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 3B 48 10 7D 24 48 8B 1D ?? ?? ?? ?? 33 FF 8B C7 40 38 BB FF 14 00 00 74 1D 48 FF C0 48 81 C3 60 15 00 00 48 83 F8 20 7C E7 33 C0";

inline constexpr const char* kAiClearEnemyTailPattern =
    "48 8B 4B 08 45 33 C9 33 D2 45 8D 41 01 48 8B 5C 24 30 48 8B 74 24 38 48 83 C4 20 5F E9 ?? ?? ?? ??";

inline constexpr const char* kWeaponAnimBuildStartPattern =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 48 8B EA 4C 8D 05 ?? ?? ?? ?? BA 26 00 00 00 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??";

inline constexpr const char* kAiActorBoundsPattern =
    "0F 10 05 ?? ?? ?? ?? 0F 11 87 B4 00 00 00 F2 0F 10 0D ?? ?? ?? ?? F2 0F 11 8F C4 00 00 00 C7 87 54 01 00 00 15 00 82 02 C7 87 CC 00 00 00 00 40 00 00";

inline constexpr const char* kAiActorTraceResultPattern =
    "48 8D 54 24 40 89 5C 24 20 48 8D 4C 24 68 F3 0F 58 C2 F3 0F 5C 15 ?? ?? ?? ?? F3 0F 11 44 24 48 0F 28 05 ?? ?? ?? ?? 0F 11 44 24 50 F3 0F 11 54 24 38 E8 ?? ?? ?? ?? 44 38 BC 24 95 00 00 00";

inline constexpr const char* kNativeWeaponHandsCallsPattern =
    "48 8B 53 18 48 8B CB E8 ?? ?? ?? ?? 49 89 44 24 08 48 8B CB 48 8B 53 20 E8 ?? ?? ?? ?? EB ?? 33 C0 49 89 44 24 08";

inline constexpr const char* kAiEnemySetterStartPattern =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 48 8B 01 45 33 F6 45 8B E9 45 8B F8 48 8B FA 48 8B F1 41 8B EE 48";

inline constexpr const char* kAiSpawnTargetnamePattern =
    "49 8D 8E 46 01 00 00 8B D3 E8 ?? ?? ?? ?? 41 83 A6 4C 01 00 00 FE";

inline constexpr int kVisibleMenuRows = 12;
inline constexpr float MenuPanelHeight(int rows) { return 200.f+26.f*rows; }
enum class Page { Home, Player, Weapons, World, AI, Zombies, Diagnostics, ZombieRules, View, ZombieController };
enum class Input { Toggle, Up, Down, Select, Back, Left, Right };
enum class Action { None, ToggleGod, ToggleHud, WriteSnapshot, WriteRoundPreview, ChangeFov, SaveFov, ResetFov,
    ToggleInfiniteAmmo, ToggleNoReload, ToggleNoclip, ToggleSuperSpeed, ToggleSuperJump, ResetPlayer,
    DecreaseSpeed, IncreaseSpeed, DecreaseJump, IncreaseJump,
    ToggleGiveAll, ToggleRapidFire, ToggleExplosiveBullets, ResetWeapons, ToggleDualWield, PreviousWeaponBank, NextWeaponBank,
    DecreaseRapidFire, IncreaseRapidFire, ToggleNoRecoil,
    ToggleSlowMotion, ToggleFreezeAI, ToggleGravity, ResetWorld,
    DecreaseTimeScale, IncreaseTimeScale, DecreaseGravity, IncreaseGravity,
    ToggleRehearsal, ChangeRehearsalRound, ToggleRehearsalAutoClear, ToggleRehearsalSpawning,
    KillRehearsalActor, ClearRehearsalActors, WriteRehearsal,
    SpawnAiEnemy, SpawnAiFriendly, PreviousEnemySpawner, NextEnemySpawner, PreviousFriendlySpawner, NextFriendlySpawner,
    ToggleAiSpawnPause, RemoveSpawnedAi, WriteAiState, KillAllEnemies };
inline bool IsRehearsalAction(Action action) {
    return action>=Action::ToggleRehearsal && action<=Action::WriteRehearsal;
}
struct MenuState {
    bool visible = false;
    bool hud = true;
    Page page = Page::Home;
    int selected = 0;
    int homeSelection = 0;
    int previewRound = bo1::kPreviewMinRound;
    int rehearsalRound = bo1::kPreviewMinRound;
    FovPreference fov;
    int count() const {
        switch (page) {
            case Page::Home: return 6;
            case Page::Player: return 6;
            case Page::Weapons: return 10;
            case Page::World: return 5;
            case Page::AI: return 11;
            case Page::Zombies: return 3;
            case Page::Diagnostics: return 2;
            case Page::ZombieRules: return 6;
            case Page::View: return 5;
            case Page::ZombieController: return 12;
        }
        return 0;
    }
    int firstVisibleRow() const { return 0; }
    Action input(Input key) {
        if (key == Input::Toggle) { visible = !visible; return Action::None; }
        if (!visible) return Action::None;
        if (key==Input::Left || key==Input::Right) {
            const bool right=key==Input::Right;
            if (page==Page::Weapons && selected==0) return right?Action::NextWeaponBank:Action::PreviousWeaponBank;
            if (page==Page::Weapons && selected==4) return right?Action::IncreaseRapidFire:Action::DecreaseRapidFire;
            if (page==Page::Player && selected==3) return right?Action::IncreaseSpeed:Action::DecreaseSpeed;
            if (page==Page::Player && selected==4) return right?Action::IncreaseJump:Action::DecreaseJump;
            if (page==Page::World && selected==0) return right?Action::IncreaseTimeScale:Action::DecreaseTimeScale;
            if (page==Page::World && selected==2) return right?Action::IncreaseGravity:Action::DecreaseGravity;
            if(page==Page::AI && selected==2) return right?Action::NextEnemySpawner:Action::PreviousEnemySpawner;
            if(page==Page::AI && selected==3) return right?Action::NextFriendlySpawner:Action::PreviousFriendlySpawner;
        }
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
        if(page==Page::ZombieController && selected==1 && (key==Input::Left || key==Input::Right || key==Input::Select)) {
            const int oldRound=rehearsalRound;
            if(key==Input::Left && rehearsalRound>bo1::kPreviewMinRound) --rehearsalRound;
            if(key!=Input::Left && rehearsalRound<bo1::kPreviewMaxRound) ++rehearsalRound;
            return rehearsalRound==oldRound?Action::None:Action::ChangeRehearsalRound;
        }
        if (key == Input::Up) selected = (selected + count() - 1) % count();
        if (key == Input::Down) selected = (selected + 1) % count();
        if (key == Input::Back) {
            if (page == Page::Home) visible = false;
            else if (page == Page::ZombieRules) { page = Page::Zombies; selected = 0; }
            else if (page == Page::ZombieController) { page=Page::Zombies; selected=1; }
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
            else if (page == Page::Player && selected == 2) return Action::ToggleNoclip;
            else if (page == Page::Player && selected == 3) return Action::ToggleSuperSpeed;
            else if (page == Page::Player && selected == 4) return Action::ToggleSuperJump;
            else if (page == Page::Player && selected == 5) return Action::ResetPlayer;
            else if (page == Page::Weapons && selected == 0) return Action::ToggleGiveAll;
            else if (page == Page::Weapons && selected == 1) return Action::ToggleInfiniteAmmo;
            else if (page == Page::Weapons && (selected == 2 || selected == 3)) return Action::ToggleNoReload;
            else if (page == Page::Weapons && selected == 4) return Action::ToggleRapidFire;
            else if (page == Page::Weapons && selected == 5) return Action::ToggleExplosiveBullets;
            else if (page == Page::Weapons && selected == 6) return Action::ToggleDualWield;
            else if (page == Page::Weapons && selected == 7) return Action::ToggleNoRecoil;
            else if (page == Page::Weapons && selected == 8) return Action::WriteSnapshot;
            else if (page == Page::Weapons && selected == 9) return Action::ResetWeapons;
            else if (page == Page::World && selected == 0) return Action::ToggleSlowMotion;
            else if (page == Page::World && selected == 1) return Action::ToggleFreezeAI;
            else if (page == Page::World && selected == 2) return Action::ToggleGravity;
            else if (page == Page::World && selected == 3) return Action::KillAllEnemies;
            else if (page == Page::World && selected == 4) return Action::ResetWorld;
            else if(page==Page::AI) {
                if(selected==0) return Action::SpawnAiEnemy;
                if(selected==1) return Action::SpawnAiFriendly;
                if(selected==4) return Action::ToggleAiSpawnPause;
                if(selected==5) return Action::ToggleFreezeAI;
                if(selected==8) return Action::RemoveSpawnedAi;
                if(selected==9) return Action::WriteAiState;
            }
            else if (page == Page::View && selected == 1) return Action::SaveFov;
            else if (page == Page::View && selected == 2) return Action::ResetFov;
            else if (page == Page::Zombies && selected == 0) {
                page = Page::ZombieRules; selected = 0; return Action::WriteRoundPreview;
            }
            else if(page==Page::Zombies && selected==1) {
                page=Page::ZombieController; selected=0; return Action::WriteRehearsal;
            }
            else if(page==Page::ZombieController) {
                if(selected==0) return Action::ToggleRehearsal;
                if(selected==2) return Action::ToggleRehearsalAutoClear;
                if(selected==3) return Action::ToggleRehearsalSpawning;
                if(selected==4) return Action::KillRehearsalActor;
                if(selected==5) return Action::ClearRehearsalActors;
                if(selected==11) return Action::WriteRehearsal;
            }
            else if (page == Page::Diagnostics && selected == 0) {
                hud = !hud; return Action::ToggleHud;
            } else if (page == Page::Diagnostics && selected == 1) return Action::WriteSnapshot;
        }
        return Action::None;
    }
};
} // namespace mod
