#include "bo1_melee_rules.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
void Check(bool ok,const char* why) { if(!ok) { std::cerr<<"FAIL: "<<why<<'\n'; std::exit(1); } }
int main() {
    using namespace mod::bo1;
    MeleeInputs i;
    i.sentient=true; i.alive=true; i.distanceSquared=64.f*64.f; i.pathClear=true;
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Allowed,"verified 64-unit normal range admits exact boundary");
    i.pathClear.reset(); Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::NeedsPathAdapter,"missing collision result cannot silently allow melee");
    i.pathClear=false; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::BlockedPath,"close melee still requires goal/path checks");
    i.pathClear=true; i.yawDegrees=60;
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Allowed,"60-degree admission boundary");
    i.yawDegrees=61; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Facing,"new attack facing gate");
    Check(CanMeleePlayer(i,MeleeCheck::AlreadyStarted)==MeleeDecision::Allowed,"ongoing attack has wider facing tolerance");
    i.yawDegrees=110; Check(CanMeleePlayer(i,MeleeCheck::AlreadyStarted)==MeleeDecision::Allowed,"110-degree ongoing boundary");
    i.yawDegrees=111; Check(CanMeleePlayer(i,MeleeCheck::AlreadyStarted)==MeleeDecision::Facing,"ongoing attack yaw limit");
    i.yawDegrees=0; i.sentient=false; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::TargetInvalid,"point-of-interest is not a melee victim");
    i.sentient=true; i.alive=false; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::TargetDead,"dead targets excluded");
    i.alive=true; i.disableMeleeDefined=true; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Disabled,"defined disableMelee veto");
    i.disableMeleeDefined=false; i.magicBulletShield=true; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Shielded,"normal zombie shield exclusion");
    i.magicBulletShield=false; i.closestEnemySquared=i.distanceSquared+1;
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::EnemyDistanceGuard,"source epsilon inequality preserved even if its comment seems counterintuitive");
    i.closestEnemySquared.reset(); i.distanceSquared=200.f*200.f;
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Allowed,"normal charging distance boundary");
    Check(CanMeleePlayer(i,MeleeCheck::AlreadyStarted)==MeleeDecision::RequiresCharge,"ongoing attack may not continue by charging");
    i.distanceSquared+=1; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::OutOfRange,"normal charge bound");
    Check(CanMeleePlayer(i,MeleeCheck::LongRange)==MeleeDecision::Allowed,"desperate check uses 512-unit bound");
    i.distanceSquared=512.f*512.f+1;
    Check(CanMeleePlayer(i,MeleeCheck::LongRange)==MeleeDecision::OutOfRange,"long-range bound");
    Check(CanMeleePlayer(i,MeleeCheck::AnyRange)==MeleeDecision::Allowed,"any-range omits charge distance bound only");
    i.millisecondsSinceMiss=2999; Check(CanMeleePlayer(i,MeleeCheck::AnyRange)==MeleeDecision::RecentMiss,"miss blocks charging for three seconds");
    i.millisecondsSinceMiss=3000; Check(CanMeleePlayer(i,MeleeCheck::AnyRange)==MeleeDecision::Allowed,"miss timeout exact boundary");
    i.targetAvailable=false; Check(CanMeleePlayer(i,MeleeCheck::AnyRange)==MeleeDecision::TargetReserved,"charging checks target reservation");
    i.distanceSquared=16; Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::Allowed,"reservation and recent-miss restrictions apply to charging, not close attacks");
    i.distanceSquared=10000; i.targetAvailable=true; i.checkChargePath=true; i.pathClear.reset();
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::NeedsPathAdapter,"conditional charging collision check requires adapter");
    i.distanceSquared=std::numeric_limits<float>::quiet_NaN();
    Check(CanMeleePlayer(i,MeleeCheck::Normal)==MeleeDecision::InvalidInput,"compatibility boundary rejects corrupt native input");
    std::cout<<"All scoped BO1 player-melee admission fixtures passed; no actor or damage adapter invoked\n";
}
