#include <utility>
#include <vector>
#include <string>
#include "bo1_zombie_rules.hpp"
#include <cstdlib>
#include <deque>
#include <iostream>

void Check(bool ok,const char* why) {
    if(!ok) { std::cerr<<"FAIL: "<<why<<'\n'; std::exit(1); }
}
// Scripted RandomInt: returns queued values (clamped into range), then 0.
struct Script {
    std::deque<int> values;
    mod::bo1::RandomInt fn() { return [this](int n) { int v=values.empty()?0:values.front(); if(!values.empty()) values.pop_front(); return n>0?std::clamp(v,0,n-1):0; }; }
};
int main() {
    using namespace mod::bo1;
    // Score: 10 per hit, kill 50 + location bonus, rounded to 5, doubled by double points.
    Check(DamagePoints(1)==10 && DamagePoints(2)==20,"hit points");
    Check(KillPoints(DamageKind::Bullet,BodyPart::Head,false,1)==100,"headshot kill 100");
    Check(KillPoints(DamageKind::Bullet,BodyPart::Neck,false,1)==70,"neck kill 70");
    Check(KillPoints(DamageKind::Bullet,BodyPart::TorsoUpper,false,1)==60,"torso kill 60");
    Check(KillPoints(DamageKind::Bullet,BodyPart::Limb,false,1)==50,"limb kill 50");
    Check(KillPoints(DamageKind::Melee,BodyPart::Head,false,1)==130,"knife kill 130");
    Check(KillPoints(DamageKind::Bullet,BodyPart::Head,false,2)==200,"double points doubles the kill");
    Check(KillPoints(DamageKind::Unknown,BodyPart::Head,true,1)==150,"insta-kill doubles the location bonus");
    Check(KillPoints(DamageKind::Unknown,BodyPart::Limb,true,1)==50,"insta-kill limb kill");
    Check(NukePoints(1)==400 && NukePoints(2)==800,"nuke points");
    Check(RoundUpScore(12,5)==15 && RoundUpScore(60,5)==60,"round_up_score");
    ScoreLedger ledger; ledger.add(60); ledger.add(-5);
    Check(ledger.score==560 && ledger.total==560,"score and score_total start at 500 and only add");
    // Movement speed thresholds.
    Script s;
    Check(ZombieMoveSpeed(1)==1 && ZombieMoveSpeed(2)==8 && ZombieMoveSpeed(6)==40,"level.zombie_move_speed");
    s.values={34}; Check(RunSpeed(1,s.fn())==MoveSpeed::Walk,"round one always walks");
    s.values={0}; Check(RunSpeed(40,s.fn())==MoveSpeed::Run,"round six can run");
    s.values={34}; Check(RunSpeed(40,s.fn())==MoveSpeed::Sprint,"round six can sprint");
    s.values={34}; Check(RunSpeed(8,s.fn())==MoveSpeed::Run,"round two: 8+34=42 runs");
    s.values={27}; Check(RunSpeed(8,s.fn())==MoveSpeed::Walk,"round two: 8+27=35 walks");
    Check(LastZombieSpeedUp(4,0,1) && !LastZombieSpeedUp(3,0,1) && !LastZombieSpeedUp(5,1,1),"last zombie speed-up");
    // Pursuit cadence.
    s.values={0}; Check(FollowRepathMilliseconds(100.f,s.fn())==100,"near: 0.1 s loop");
    s.values={999}; Check(FollowRepathMilliseconds(4000.f*4000.f,s.fn())==3099,"far: up to 3 s extra");
    s.values={0}; Check(FollowRepathMilliseconds(1500.f*1500.f,s.fn())==600,"mid: 0.5 s extra");
    // Powerup lifetime and blinking.
    Check(PowerupLifetimeMilliseconds()==26500,"26.5 s pickup lifetime");
    Check(PowerupVisible(14999) && PowerupVisible(15000) && !PowerupVisible(15500) && PowerupVisible(16000) && !PowerupVisible(26500),"blink sequence");
    // Powerup drops: 2500 total to arm, one drop per arm, four per round, 3% free drops.
    PowerupDirector d; s.values={}; d.start(500,s.fn());
    Check(d.scoreToDrop()==2500 && !d.dropItem(),"first drop at 2000 earned");
    d.watch(2500); Check(!d.dropItem(),"strictly greater than the threshold");
    d.watch(2510); Check(d.dropItem() && d.scoreToDrop()==2510+2280,"arm and raise threshold by 2000*1.14");
    s.values={50}; auto p=d.zombieDied(s.fn());
    Check(p.has_value() && *p!=Powerup::Carpenter && *p!=Powerup::FireSale && !d.dropItem(),"armed kill drops and disarms");
    s.values={50}; Check(!d.zombieDied(s.fn()),"no drop when disarmed");
    for(int i=0;i<3;++i) { s.values={0}; Check(d.zombieDied(s.fn()).has_value(),"3% chance drops while disarmed"); }
    s.values={0}; Check(!d.zombieDied(s.fn()) && d.dropsThisRound()==4,"four drops per round");
    d.roundStart(); s.values={1}; Check(d.zombieDied(s.fn()).has_value(),"new round resets the drop count");
    // Every valid powerup appears in each cycle of the randomized array.
    PowerupDirector cyc; s.values={}; cyc.start(500,s.fn());
    int seen[7]{};
    for(int i=0;i<4;++i) { s.values={}; ++seen[static_cast<int>(cyc.next(s.fn()))]; }
    Check(seen[1]==1 && seen[2]==1 && seen[3]==1 && seen[4]==1,"one cycle holds nuke, insta-kill, double points and max ammo");
    // Hellhounds.
    DogRounds dogs; s.values={1}; dogs.start(s.fn());
    Check(dogs.next()==6,"first dog round in 5..7");
    s.values={}; Check(!dogs.beginRound(5,s.fn()),"round five normal");
    s.values={1}; Check(dogs.beginRound(6,s.fn()) && dogs.next()==11,"dog round then next in 4..5 rounds");
    Check(dogs.total()==6 && dogs.health()==1600 && dogs.baseWait()==3.f,"first dog round: 6 dogs, 400*4 health, 3 s wait");
    s.values={}; Check(!dogs.beginRound(7,s.fn()) && dogs.count()==2,"leaving a dog round advances the count");
    s.values={0}; Check(dogs.beginRound(11,s.fn()) && dogs.total()==6 && dogs.health()==3600,"second dog round");
    s.values={}; dogs.beginRound(12,s.fn()); s.values={0}; dogs.beginRound(15,s.fn());
    Check(dogs.count()==3 && dogs.total()==8 && dogs.health()==5200,"third dog round: 8 dogs");
    const auto plan=dogs.plan();
    Check(plan.dogs && plan.concurrent==2 && plan.endDelay==8000 && plan.capacityPoll==2000,"dog round plan");
    // Controller with a dog-round plan.
    RoundController c;
    c.setPlanner([&](int round,bool) -> std::optional<RoundPlan> { if(round==2) return plan; return std::nullopt; });
    Check(c.start(2,0) && c.status().dogs && c.status().total==8 && c.status().concurrent==2,"planned dog round");
    auto request=c.tick(2500);
    Check(request && request->dogs && request->health==5200,"dog spawn request carries health");
    Check(c.confirm(request->ticket,true,2500),"first dog");
    Check(!c.tick(2500+1874) && c.tick(2500+1875).has_value(),"wait = 2 - 1/8 s");
    c.confirm(c.status().pendingTicket,true,4375);
    Check(!c.tick(9000) && c.status().waitMilliseconds==2000,"two alive: 2 s capacity poll");
    c.retireAll(Retirement::Killed);
    std::int64_t now=9000;
    while(c.status().queued>0 || c.status().alive>0) {
        now+=100; request=c.tick(now);
        if(request) { c.confirm(request->ticket,true,now); c.retireOldest(Retirement::Killed); }
    }
    now+=1000; c.tick(now);
    Check(c.status().phase==RoundPhase::Between && c.status().waitMilliseconds==18000,"8 s aftermath plus 10 s break");
    Check(c.invariant(),"dog round conserves obligations");
    RoundController q; q.start(1,0);
    Check(q.cancelQueued()==6 && q.status().queued==0 && q.status().killed==6 && q.invariant(),"cancelled obligations count as killed");
    q.tick(1000); q.tick(9250); q.tick(10250);
    Check(q.status().phase==RoundPhase::Between,"a cancelled round ends at the next completion poll");
    // zombie_melee: one hit per "fire" notetrack (BO1 attack_v2 has five,
    // attack_v4 one, walk attacks a footstep after the swing).
    {
        std::vector<std::pair<std::string,float>> v2{{"sndnt#zmb_attack_whoosh",.07f},{"fire",.1f},{"fire",.33f},{"fire",.53f},
            {"fire",.62f},{"fire",.74f},{"end",1.f}};
        int fires[8]{};
        Check(AttackFireTimes(v2,7166,fires,8)==5 && fires[0]==716 && fires[4]==5302,"attack_v2 swings five times");
        Check(AttackFireTimes(v2,7166,fires,2)==2,"fire list is bounded");
        std::vector<std::pair<std::string,float>> walk{{"fire",.19f},{"footstep_right_small",.56f},{"end",1.f}};
        Check(AttackFireTimes(walk,1066,fires,8)==1 && fires[0]==202,"walk attack swings once");
        Check(AttackFireTimes(walk,0,fires,8)==0,"no length, no swings");
        std::vector<std::pair<std::string,float>> unsorted{{"fire",.6f},{"fire",.2f}};
        Check(AttackFireTimes(unsorted,1000,fires,8)==2 && fires[0]==200 && fires[1]==600,"fire times are in time order");
    }
    Check(!BreakOffAttack(100,500.f*500.f),"a swing is never cancelled in its first 250 ms");
    Check(BreakOffAttack(300,97.f*97.f) && !BreakOffAttack(300,95.f*95.f),"break off beyond melee distance + 32");
    Check(RiseDepth(1)==45.f && RiseDepth(2)==14.f && kRiseShowMilliseconds==500,"do_zombie_rise offsets and hide_pop");
    // Test41: unreachable spawn points (Favela rooftops).
    {
        ReachInputs in;in.distanceSquared=600.f*600.f;in.noPathMs=3000;in.otherZombieHasPath=true;
        Check(JudgeReach(in)==Reach::RecycleAndBlacklist,"no path while another zombie paths: blacklist the point");
        in.pointKnownGood=true;
        Check(JudgeReach(in)==Reach::Wait,"a point that pathed before is never blacklisted");
        in.noPathMs=12000;
        Check(JudgeReach(in)==Reach::Recycle,"a zombie without a path for 12 s is recycled");
        in.pointKnownGood=false;in.otherZombieHasPath=false;in.noPathMs=11999;
        Check(JudgeReach(in)==Reach::Wait,"alone, no blacklisting: the player may be unreachable for a moment");
        in.hasPath=true;in.noPathMs=20000;
        Check(JudgeReach(in)==Reach::Ok,"a path is fine");
        in.hasPath=false;in.distanceSquared=100.f*100.f;
        Check(JudgeReach(in)==Reach::Ok,"next to the player needs no path");
        in.distanceSquared=600.f*600.f;in.attacking=true;
        Check(JudgeReach(in)==Reach::Ok,"attacking zombies are left alone");
        Check(MayBlacklist(0,4) && MayBlacklist(2,4) && !MayBlacklist(3,4) && !MayBlacklist(0,1),"at most three quarters blacklisted");
    }
    std::cout<<"BO1 zombie rules fixtures passed\n";
}
