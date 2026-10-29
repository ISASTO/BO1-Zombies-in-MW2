#pragma once
// Test45: BO1 zombie dismemberment rules (maps\_zombiemode_spawner.gsc
// zombie_gib_on_damage, zombie_should_gib, head_should_gib;
// animscripts\zombie_death.gsc do_gib/get_limb_data and the explosive death
// gibs), on MW2's means of death and hit locations.
//
//  * A hit may gib only when the player dealt it with a weapon (not a melee,
//    a fall, a crush...) and it took at least 10% of the health the zombie had.
//  * Head: health is at most 10% after a hit to the head, helmet or neck: the
//    head pops (and a living zombie bleeds out over a second).
//  * Otherwise, once per zombie: upper or lower torso -> the guts or the right
//    arm; an arm or hand -> that arm; a leg or foot only on the killing hit ->
//    a leg (each leg twice as likely as both).
//  * An explosive kill gibs half of the time, any of the six.
// Deviation: BO1 never pops heads with pistols. Zombies here start with the
// USP .45 and usually have nothing else, so pistols count.
#include <cstdint>
#include <functional>

namespace mod::bo1gib {
enum class Ref : int {None,RightArm,LeftArm,Guts,RightLeg,LeftLeg,NoLegs};
inline const char* RefText(Ref r) {
    switch(r) {
        case Ref::RightArm: return "right_arm";case Ref::LeftArm: return "left_arm";case Ref::Guts: return "guts";
        case Ref::RightLeg: return "right_leg";case Ref::LeftLeg: return "left_leg";case Ref::NoLegs: return "no_legs";
        default: return "none";
    }
}
// MW2 means of death (G_Damage) and hit locations (HITLOC_*).
inline constexpr int kModUnknown=0,kModPistolBullet=1,kModRifleBullet=2,kModExplosiveBullet=3,kModGrenade=4,kModGrenadeSplash=5,
    kModProjectile=6,kModProjectileSplash=7,kModMelee=8,kModHeadShot=9,kModCrush=10,kModFalling=11,kModSuicide=12,kModTriggerHurt=13,
    kModExplosive=14,kModImpact=15;
enum Hit : int {HitNone=0,Helmet=1,Head=2,Neck=3,TorsoUpper=4,TorsoLower=5,RightArmUpper=6,LeftArmUpper=7,RightArmLower=8,LeftArmLower=9,
    RightHand=10,LeftHand=11,RightLegUpper=12,LeftLegUpper=13,RightLegLower=14,LeftLegLower=15,RightFoot=16,LeftFoot=17};
struct Inputs {
    int mod=kModUnknown,hit=HitNone;
    int damage=0;                        // the damage dealt (after insta-kill)
    int healthAfter=0,maxHealth=1;
    bool byPlayer=false,headGibbed=false,gibbed=false;
};
struct Decision {bool head=false;Ref limb=Ref::None;};
using Random=std::function<int(int)>;     // [0, n)

inline bool Explosive(int mod) {
    return mod==kModGrenade || mod==kModGrenadeSplash || mod==kModProjectile || mod==kModProjectileSplash || mod==kModExplosive;
}
inline bool Bullet(int mod) {return mod==kModPistolBullet || mod==kModRifleBullet || mod==kModHeadShot || mod==kModExplosiveBullet;}
// zombie_should_gib
inline bool ShouldGib(const Inputs& in) {
    if(!in.byPlayer) return false;
    switch(in.mod) {
        case kModUnknown: case kModCrush: case kModFalling: case kModSuicide: case kModTriggerHurt: case kModMelee: case kModImpact: return false;
        default: break;
    }
    int previous=in.damage+in.healthAfter;
    if(previous<=0) previous=1;
    return static_cast<long long>(in.damage)*100>=static_cast<long long>(previous)*10;
}
inline bool HeadHit(int hit) {return hit==Helmet || hit==Head || hit==Neck;}
// head_should_gib (bullets only: grenade splash reports no hit location)
inline bool HeadShouldGib(const Inputs& in) {
    if(in.headGibbed || !in.byPlayer || in.maxHealth<=0) return false;
    if(static_cast<long long>(in.healthAfter)*100>static_cast<long long>(in.maxHealth)*10) return false;
    if(!Bullet(in.mod)) return false;
    return HeadHit(in.hit);
}
inline Decision Decide(const Inputs& in,const Random& random) {
    Decision d;
    if(!ShouldGib(in)) return d;
    if(HeadShouldGib(in)) {d.head=true;return d;}
    if(in.gibbed || HeadHit(in.hit)) return d;
    const bool dead=in.healthAfter<=0;
    if(Explosive(in.mod)) {
        // zombie_death: an explosive death throws limbs half of the time.
        if(dead && random(2)==0) {
            static constexpr Ref all[]={Ref::Guts,Ref::RightArm,Ref::LeftArm,Ref::RightLeg,Ref::LeftLeg,Ref::NoLegs};
            d.limb=all[random(6)];
        }
        return d;
    }
    switch(in.hit) {
        case TorsoUpper: case TorsoLower: d.limb=random(2)?Ref::RightArm:Ref::Guts;break;
        case RightArmUpper: case RightArmLower: case RightHand: d.limb=Ref::RightArm;break;
        case LeftArmUpper: case LeftArmLower: case LeftHand: d.limb=Ref::LeftArm;break;
        case RightLegUpper: case LeftLegUpper: case RightLegLower: case LeftLegLower: case RightFoot: case LeftFoot:
            if(dead) {
                static constexpr Ref legs[]={Ref::RightLeg,Ref::LeftLeg,Ref::RightLeg,Ref::LeftLeg,Ref::NoLegs};
                d.limb=legs[random(5)];
            }
            break;
        default: break;
    }
    return d;
}
// get_limb_data: the upper body and legs a zombie wears after the gib, and
// which pieces fly off (bits: 1 right arm, 2 left arm, 4 right leg, 8 left leg).
struct Limb {int torso=0;int legs=0;unsigned pieces=0;};   // torso: 0 upclean,1 rarmoff,2 larmoff,3 guts; legs: 0 lowclean,1 rlegoff,2 llegoff,3 legsoff
inline Limb LimbData(Ref r) {
    switch(r) {
        case Ref::RightArm: return {1,0,1u};
        case Ref::LeftArm: return {2,0,2u};
        case Ref::Guts: return {3,0,0u};
        case Ref::RightLeg: return {0,1,4u};
        case Ref::LeftLeg: return {0,2,8u};
        case Ref::NoLegs: return {0,3,12u};
        default: return {};
    }
}
} // namespace mod::bo1gib
