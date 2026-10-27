#pragma once
#include "bo1_round_controller.hpp"
#include "bo1_melee_rules.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace mod::bo1 {
// Portable ports of the solo BO1 zombie rules, from the owner's local
// common_zombie_patch.ff scripts (extracted with tools/extract_bo1_scripts.py,
// never distributed). Each block names its source. Randomness is injected as
// RandomInt(n) in [0,n), like the script builtin, so fixtures are exact.
using RandomInt=std::function<int(int)>;

// ---- Score: maps/_zombiemode_score.gsc, zombie_vars in maps/_zombiemode.gsc
inline constexpr int kStartingPoints=500;   // zombie_score_start_1p (normal mp/zombiemode.csv; GSC fallback 3000)
inline constexpr int kDamagePoints=10;      // zombie_score_damage_normal/_light
inline constexpr int kKillPoints1p=50;      // zombie_score_kill_1player
inline constexpr int kBonusMelee=80,kBonusHead=50,kBonusNeck=20,kBonusTorso=10,kBonusBurn=10;
inline constexpr int kNukePoints=400;       // nuke_powerup: player_add_points("nuke_powerup", 400)
inline int RoundUpScore(int score,int value) {  // round_up_score
    int rounded=score-score%value; if(rounded<score) rounded+=value; return rounded;
}
enum class DamageKind { Bullet, Melee, Burned, Explosive, Unknown };
enum class BodyPart { None, Helmet, Head, Neck, TorsoUpper, TorsoLower, Limb };
// player_add_points_kill_bonus
inline int KillBonus(DamageKind kind,BodyPart part) {
    if(kind==DamageKind::Melee) return kBonusMelee;
    if(kind==DamageKind::Burned) return kBonusBurn;
    switch(part) {
        case BodyPart::Head: case BodyPart::Helmet: return kBonusHead;
        case BodyPart::Neck: return kBonusNeck;
        case BodyPart::TorsoUpper: case BodyPart::TorsoLower: return kBonusTorso;
        default: return 0;
    }
}
// player_add_points("damage"/"damage_light"): multiplier * round_up_score(10,5)
inline int DamagePoints(int multiplier) { return multiplier*RoundUpScore(kDamagePoints,5); }
// player_add_points("death"): insta-kill kills arrive as MOD_UNKNOWN, whose
// hit-location bonus doubles while the insta-kill HUD flag is on.
inline int KillPoints(DamageKind kind,BodyPart part,bool instaKillOn,int multiplier) {
    int bonus=KillBonus(kind,part);
    if(instaKillOn && kind==DamageKind::Unknown) bonus*=2;
    return multiplier*RoundUpScore(kKillPoints1p+bonus,5);
}
inline int NukePoints(int multiplier) { return multiplier*RoundUpScore(kNukePoints,5); }
struct ScoreLedger {
    int score=kStartingPoints,total=kStartingPoints;   // self.score, self.score_total
    void add(int points) { if(points>0) { score+=points; total+=points; } }
};

// ---- Movement: set_run_speed (_zombiemode_spawner.gsc), round_think (_zombiemode.gsc)
enum class MoveSpeed { Walk, Run, Sprint };
inline constexpr int kMoveSpeedMultiplier=8;   // zombie_move_speed_multiplier (CSV normal column)
// level.zombie_move_speed starts at 1 and becomes round*8 when that round ends.
inline int ZombieMoveSpeed(int round) { return round<=1?1:(round-1)*kMoveSpeedMultiplier; }
inline MoveSpeed RunSpeed(int levelSpeed,const RandomInt& random) {
    const int rand=levelSpeed+random(35);              // randomintrange(speed, speed+35)
    return rand<=35?MoveSpeed::Walk:rand<=70?MoveSpeed::Run:MoveSpeed::Sprint;
}
inline const char* MoveSpeedText(MoveSpeed s) { return s==MoveSpeed::Walk?"WALK":s==MoveSpeed::Run?"RUN":"SPRINT"; }
// zombie_speed_up: after round 3, the last living zombie (nothing queued) sprints.
inline bool LastZombieSpeedUp(int round,int queued,int alive) { return round>3 && queued<=0 && alive==1; }

// ---- Pursuit: find_flesh / zombie_follow_enemy (_zombiemode_spawner.gsc)
inline constexpr float kZombieGoalRadius=32.f;
inline constexpr float kZombiePathEnemyFightDist=64.f,kZombieMeleeAttackDist=64.f;
inline constexpr float kZombieMaxSightDistSq=128.f*128.f;
// Extra wait before the next SetGoalPos(favoriteenemy.origin), plus the 0.1 s loop wait.
inline std::int64_t FollowRepathMilliseconds(float distanceSquared,const RandomInt& random) {
    std::int64_t extra=0;
    if(distanceSquared>3200.f*3200.f) extra=2000+random(1000);       // 2.0 + randomFloat(1.0)
    else if(distanceSquared>2200.f*2200.f) extra=1000+random(500);   // 1.0 + randomFloat(0.5)
    else if(distanceSquared>1200.f*1200.f) extra=500+random(500);    // 0.5 + randomFloat(0.5)
    return extra+100;
}

// ---- Melee: zombie_melee.gsc; damage is self.meleeDamage (zombie_spawn_init)
inline constexpr int kZombieMeleeDamage=60,kDogMeleeDamage=40;
// When no BO1 attack animation is available these stand in for the "fire"
// and "end" notetracks of the attack animation (adapter values).
inline constexpr std::int64_t kAttackFireMilliseconds=450,kAttackEndMilliseconds=1100;
inline constexpr float kAttackHitRangeSquared=72.f*72.f;
// MeleeCombat: every "fire" notetrack of the attack anim is one melee() hit
// check; the anim ends at "end" (one cycle for looping run attacks). Times
// in milliseconds from the attack start, in notetrack order, at most `limit`.
template<class Notes> std::size_t AttackFireTimes(const Notes& notes,int lengthMs,int* out,std::size_t limit) {
    std::size_t count=0;
    if(lengthMs<=0) return 0;
    for(const auto& [name,time]:notes) {
        if(count>=limit) break;
        if(name=="fire" && time>=0.f && time<=1.f) out[count++]=static_cast<int>(time*static_cast<float>(lengthMs));
    }
    std::sort(out,out+count);
    return count;
}
// The engine ends the melee animscript when the enemy leaves melee range
// (zombie_melee.gsc loops only while melee continues): an attack is broken
// off once the target is beyond the melee distance plus a 32-unit margin,
// after a short grace period so a swing is not cancelled on its first frame.
inline constexpr float kAttackBreakOffSquared=(kZombieMeleeAttackDist+32.f)*(kZombieMeleeAttackDist+32.f);
inline constexpr int kAttackBreakOffGraceMs=250;
inline bool BreakOffAttack(int elapsedMs,float distanceSquared) {
    return elapsedMs>=kAttackBreakOffGraceMs && distanceSquared>kAttackBreakOffSquared;
}
// do_zombie_rise: version 2 (walkers only, 50%) starts 14 units under the
// spot, version 1 45 units; hide_pop shows the zombie after 0.5 s.
inline float RiseDepth(int version) {return version==2?14.f:45.f;}
inline constexpr int kRiseShowMilliseconds=500;

// ---- Failsafe: round_spawn_failsafe (_zombiemode.gsc)
inline constexpr std::int64_t kFailsafeIntervalMilliseconds=30000;
inline constexpr float kFailsafeMinimumMoveSquared=24.f*24.f;

// ---- Powerups: maps/_zombiemode_powerups.gsc
enum class Powerup { None, Nuke, InstaKill, DoublePoints, MaxAmmo, Carpenter, FireSale };
inline const char* PowerupText(Powerup p) {
    switch(p) {
        case Powerup::Nuke: return "KABOOM";
        case Powerup::InstaKill: return "INSTA-KILL";
        case Powerup::DoublePoints: return "DOUBLE POINTS";
        case Powerup::MaxAmmo: return "MAX AMMO";
        case Powerup::Carpenter: return "CARPENTER";
        case Powerup::FireSale: return "FIRE SALE";
        default: return "NONE";
    }
}
inline constexpr int kPowerupDropIncrement=2000;     // zombie_powerup_drop_increment
inline constexpr float kPowerupDropIncrementScale=1.14f;
inline constexpr int kPowerupDropMaxPerRound=4;      // zombie_powerup_drop_max_per_round
inline constexpr std::int64_t kPowerupWatchMilliseconds=500;
inline constexpr std::int64_t kPowerupDurationMilliseconds=30000;   // insta-kill and double points
inline constexpr float kPowerupGrabRangeSquared=64.f*64.f;
inline constexpr float kPowerupHeight=40.f;          // drop_point + (0,0,40)
// powerup_timeout: 15 s, then 40 hide/show toggles (15 x 0.5, 10 x 0.25, 15 x 0.1 s).
inline constexpr std::int64_t kPowerupSolidMilliseconds=15000;
inline std::int64_t PowerupLifetimeMilliseconds() { return kPowerupSolidMilliseconds+15*500+10*250+15*100; }
inline bool PowerupVisible(std::int64_t age) {
    if(age<kPowerupSolidMilliseconds) return true;
    std::int64_t t=kPowerupSolidMilliseconds;
    for(int i=0;i<40;++i) {
        const std::int64_t step=i<15?500:i<25?250:100;
        if(age<t+step) return !(i%2);
        t+=step;
    }
    return false;
}
// array_randomize (common_scripts/utility.gsc): swap each slot with RandomInt(size).
template<std::size_t N> void ArrayRandomize(std::array<Powerup,N>& a,const RandomInt& random) {
    for(std::size_t i=0;i<N;++i) std::swap(a[i],a[static_cast<std::size_t>(random(static_cast<int>(N)))]);
}
// Kino's included set. Carpenter needs 5 destroyed windows and fire sale a
// moved box: neither exists in a campaign map, so get_valid_powerup skips them.
class PowerupDirector {
public:
    void start(int startingTotal,const RandomInt& random) {
        *this={};
        increment_=static_cast<float>(kPowerupDropIncrement);
        scoreToDrop_=startingTotal+kPowerupDropIncrement;   // players * score_start + increment
        cycle_={Powerup::Nuke,Powerup::InstaKill,Powerup::DoublePoints,Powerup::MaxAmmo,Powerup::Carpenter,Powerup::FireSale};
        ArrayRandomize(cycle_,random); index_=0; ArrayRandomize(cycle_,random);
    }
    void roundStart() { drops_=0; }                         // powerup_round_start
    // watch_for_drop, every 0.5 s against the summed score_total.
    void watch(int totalScore) {
        if(totalScore>scoreToDrop_) {
            increment_*=kPowerupDropIncrementScale;
            scoreToDrop_=totalScore+static_cast<int>(increment_);
            dropItem_=true;
        }
    }
    // powerup_drop: rand <= 2 drops regardless of the score flag.
    std::optional<Powerup> zombieDied(const RandomInt& random) {
        if(drops_>=kPowerupDropMaxPerRound) return {};
        const int roll=random(100);
        if(roll>2 && !dropItem_) return {};
        ++drops_;
        const auto p=next(random);
        dropItem_=false;
        return p;
    }
    Powerup next(const RandomInt& random) {                  // get_valid_powerup
        for(int guard=0;guard<64;++guard) {
            const auto p=cycle_[index_];
            if(++index_>=cycle_.size()) { index_=0; ArrayRandomize(cycle_,random); }
            if(p!=Powerup::Carpenter && p!=Powerup::FireSale) return p;
        }
        return Powerup::MaxAmmo;
    }
    int scoreToDrop() const { return scoreToDrop_; }
    bool dropItem() const { return dropItem_; }
    int dropsThisRound() const { return drops_; }
private:
    std::array<Powerup,6> cycle_{};
    std::size_t index_=0;
    float increment_=static_cast<float>(kPowerupDropIncrement);
    int scoreToDrop_=0,drops_=0;
    bool dropItem_=false;
};

// ---- Hellhounds: maps/_zombiemode_ai_dogs.gsc
inline constexpr float kDogHealthWalkMultiplier=4.f; // scr_dog_health_walk_multiplier default
class DogRounds {
public:
    void start(const RandomInt& random) { count_=1; next_=5+random(3); active_=false; }  // randomintrange(5,8)
    // dog_round_tracker at "between_round_over": the round number is final.
    bool beginRound(int round,const RandomInt& random) {
        if(round==next_) {
            if(active_) ++count_;          // consecutive dog rounds still advance the count
            active_=true; next_=round+4+random(2);  // randomintrange(4,6)
            return true;
        }
        if(active_) { active_=false; ++count_; }
        return false;
    }
    int count() const { return count_; }
    int next() const { return next_; }
    // dog_round_spawning: players*6 for the first two dog rounds, then players*8.
    int total(int players=1) const { return players*(count_<3?6:8); }
    // dog_health_increase then dog_init (* walk multiplier).
    int health() const {
        const int base=count_==1?400:count_==2?900:count_==3?1300:1600;
        return static_cast<int>(static_cast<float>(base)*kDogHealthWalkMultiplier);
    }
    float baseWait() const { return count_==1?3.f:count_==2?2.5f:count_==3?2.f:1.5f; }  // waiting_for_next_dog_spawn
    RoundPlan plan(int players=1) const {
        RoundPlan p; p.total=total(players); p.health=health(); p.concurrent=2*players; p.dogs=true;
        p.dogBaseWait=baseWait(); p.capacityPoll=2000; p.endDelay=8000;
        return p;
    }
private:
    int count_=1,next_=5;
    bool active_=false;
};
} // namespace mod::bo1
