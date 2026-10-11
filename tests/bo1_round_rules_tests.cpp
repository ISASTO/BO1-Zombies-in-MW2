#include "bo1_round_rules.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main() {
    using namespace mod::bo1;
    // Independently calculated from private source, with T5 float division and
    // int truncation confirmed in the uploaded BlackOps.exe. No game code here.
    struct Fixture { int round, total, health; };
    constexpr Fixture fixtures[] = {
        {1,6,150}, {2,8,250}, {3,13,350}, {4,18,450}, {5,24,550},
        {6,27,650}, {7,28,750}, {8,28,850}, {9,29,950}, {10,33,1045},
        {11,34,1149}, {12,36,1263}, {15,44,1679}, {20,60,2701},
        {30,105,7000}, {64,392,178744}, {100,924,5525295}
    };
    for (const auto f : fixtures) {
        const auto p = EvaluateNormalSoloRound(f.round, f.round == 1);
        Check(p.has_value(), "valid preview round produces a result");
        Check(p->round == f.round && p->total == f.total, "source-derived population fixture");
        Check(p->health == f.health, "stepwise health fixture");
        Check(p->concurrentLimit == 24 && p->betweenRoundSeconds == 10, "separate capacity and configured delay");
    }
    // first_round is an independent script flag, not always round_number == 1.
    Check(EvaluateNormalSoloRound(20,true)->total == 15, "first-round callback applies to a debug start at round 20");
    Check(EvaluateNormalSoloRound(1,false)->total == 8, "round one without first-round flag uses the next branch");
    Check(!EvaluateNormalSoloRound(0,false) && !EvaluateNormalSoloRound(-1,false)
        && !EvaluateNormalSoloRound(101,false), "preview rejects unsupported bounds");
    Check(std::fabs(EvaluateNormalSoloRound(10,false)->spawnInterval - 1.26049864f) < 1e-7f, "float spawn recurrence");
    const float r63 = EvaluateNormalSoloRound(63,false)->spawnInterval;
    const float r64 = EvaluateNormalSoloRound(64,false)->spawnInterval;
    const float r65 = EvaluateNormalSoloRound(65,false)->spawnInterval;
    Check(r63 > .08f && r64 < .08f && r65 == .08f, "prior-value branch preserves one-round undershoot");
    Check(StepSpawnInterval(.08f) == .08f && StepSpawnInterval(.07f) == .08f,
          "equal lower bound stays unchanged; below resets to it");
    Check(std::fabs(StepSpawnInterval(.081f) - .07695f) < 1e-7f, "no premature clamp above lower bound");
    std::cout << "BO1 script-default round fixtures and boundary tests passed\n";
}
