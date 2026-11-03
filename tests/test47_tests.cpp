// Test47: the F4 AI page's "Spawn Zombie" (one more obligation in the round
// in progress, refused between rounds, counts kept consistent) and the BO1
// powerup models' bones (every powerup carries tag_origin for its glow).
// With BO1_COMMON_DEC set to the owner's inflated common_zombie zone, the
// real powerup models are carved and checked too.
#include "bo1_round_controller.hpp"
#include "bo1_extra_models.hpp"
#include "bo1_xanim.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures=0;
void Check(bool ok,const char* what) {if(!ok){std::printf("FAIL: %s\n",what);++failures;}}

void TestMenuZombie() {
    using namespace mod::bo1;
    RoundController c;
    Check(!c.addQueued(),"nothing is added before a game starts");
    Check(c.start(1,0),"round 1 starts");
    const int total=c.status().total;
    Check(c.status().phase==RoundPhase::Intro && c.addQueued(),"added during the intro");
    Check(c.status().total==total+1 && c.status().queued==total+1 && c.invariant(),"the round grows by one and stays consistent");
    std::int64_t now=0;
    // Spawn and kill everything, the extra one included.
    int spawned=0;
    for(int guard=0;guard<100000 && c.status().phase!=RoundPhase::Between;++guard) {
        now+=50;
        if(auto r=c.tick(now)) {Check(c.confirm(r->ticket,true,now),"confirm");++spawned;c.retireOldest(Retirement::Killed);}
        Check(c.invariant(),"invariant while spawning");
    }
    Check(spawned==total+1,"the extra zombie is spawned and must be killed before the round ends");
    Check(c.status().phase==RoundPhase::Between && !c.addQueued() && c.invariant(),"refused between rounds");
    // A later round: draining (nothing queued) goes back to spawning.
    for(int guard=0;guard<100000 && c.status().phase!=RoundPhase::Draining;++guard) {
        now+=50;
        if(auto r=c.tick(now)) Check(c.confirm(r->ticket,true,now),"confirm round 2");
    }
    Check(c.status().phase==RoundPhase::Draining && c.addQueued() && c.status().phase==RoundPhase::Spawning && c.status().queued==1 && c.invariant(),
          "a drained round spawns again for the extra zombie");
    c.stop(StopReason::Manual);
    Check(!c.addQueued(),"nothing is added once stopped");
}

void TestPowerupBones() {
    using namespace mod;
    const char* path=std::getenv("BO1_COMMON_DEC");
    if(!path) {std::puts("SKIP real powerup models (set BO1_COMMON_DEC)");return;}
    std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
    const auto strings=bo1xanim::ParseZoneStrings(raw.data(),raw.size());
    Check(strings.has_value(),"common_zombie strings");
    if(!strings) return;
    std::string error;
    const auto p=bo1model::CarvePowerups(raw.data(),raw.size(),strings->strings,&error);
    Check(p.has_value(),"powerups carved");
    if(!p) {std::printf("  %s\n",error.c_str());return;}
    for(const auto& m:p->models) Check(m.bones.size()==1 && m.bones[0]=="tag_origin","every powerup's single bone is tag_origin");
    std::printf("real powerups: %s %s %s %s carry tag_origin\n",p->models[0].name.c_str(),p->models[1].name.c_str(),p->models[2].name.c_str(),p->models[3].name.c_str());
}
} // namespace

int main() {
    TestMenuZombie();TestPowerupBones();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::puts("test47_tests: all passed");
    return 0;
}
