#pragma once
#include <optional>
#include <limits>

namespace mod::bo1 {
// Main normal-column values recovered from the owner's common_zombie.ff
// StringTable match these patched GSC defaults. Capacity/inter-round time stay
// script values. Special rounds/map/runtime overrides are not simulated here.
inline constexpr int kPreviewMinRound = 1;
inline constexpr int kPreviewMaxRound = 100;
inline constexpr int kBasePopulation = 24;
inline constexpr int kPerPlayerPopulation = 6;
inline constexpr int kInitialHealth = 150;
inline constexpr int kAdditiveHealth = 100;
inline constexpr float kHealthMultiplier = .1f;
inline constexpr int kConcurrentLimit = 24;
inline constexpr float kInitialSpawnInterval = 2.f;
inline constexpr int kBetweenRoundSeconds = 10;

struct RoundPreview {
    int round;
    int total;
    int health;
    int concurrentLimit;
    float spawnInterval;
    int betweenRoundSeconds;
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "The source VM calculations use IEEE 754 binary32");
static_assert(sizeof(int) == 4, "The preview uses 32-bit source integers");

inline float StepSpawnInterval(float previous) {
    if (previous > .08f) return previous * .95f;
    if (previous < .08f) return .08f;
    return previous;
}
inline std::optional<RoundPreview> EvaluateNormalSoloRound(int round, bool firstRound) {
    if (round < kPreviewMinRound || round > kPreviewMaxRound) return {};
    // T5 Scr_EvalDivide promotes integer operands to float; never use round/5
    // as C++ integer division. Preserve the script's individual int conversions.
    float multiplier = static_cast<float>(round) / 5.f;
    if (multiplier < 1.f) multiplier = 1.f;
    if (round >= 10) multiplier *= static_cast<float>(round) * .15f;
    int total = kBasePopulation + static_cast<int>((.5f * kPerPlayerPopulation) * multiplier);
    if (firstRound) total = static_cast<int>(static_cast<float>(total) * .25f);
    else if (round < 3) total = static_cast<int>(static_cast<float>(total) * .3f);
    else if (round < 4) total = static_cast<int>(static_cast<float>(total) * .5f);
    else if (round < 5) total = static_cast<int>(static_cast<float>(total) * .7f);
    else if (round < 6) total = static_cast<int>(static_cast<float>(total) * .9f);

    int health = kInitialHealth;
    float interval = kInitialSpawnInterval;
    for (int step = 2; step <= round; ++step) {
        if (step >= 10) health += static_cast<int>(static_cast<float>(health) * kHealthMultiplier);
        else health += kAdditiveHealth;
        interval = StepSpawnInterval(interval);
    }
    return RoundPreview{round, total, health, kConcurrentLimit, interval, kBetweenRoundSeconds};
}
} // namespace mod::bo1
