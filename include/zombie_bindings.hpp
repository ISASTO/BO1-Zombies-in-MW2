#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace mod {
// Test38 exact-retail bindings for squad following and the zombies adapter.
// Every RVA here is re-checked at startup (fail closed) and independently by
// tools/verify_zombie_bindings.py against the SHA256-gated executable.

// Actor fields, from the native script field table (name, offset, type).
inline constexpr std::size_t kActorRequestArrival=0x2a0;   // requestarrivalnotify + goingtoruntopos
inline constexpr std::size_t kActorKeepClaimedNode=0x2d9;  // keepclaimednode, keepclaimednodeifvalid
inline constexpr std::size_t kActorMaxSightDistSq=0x300;
inline constexpr std::size_t kActorFixedNodeCheck=0xd40;   // native fixednode setter writes 0x80000000
inline constexpr std::size_t kActorPacifist=0xd64;
inline constexpr std::size_t kActorGrenadeAwareness=0xdd8,kActorGrenadeAmmo=0xdf0;
inline constexpr std::size_t kActorWalkDist=0xf94;
inline constexpr std::size_t kActorGoalPosition=0x1498,kActorGoalRadius=0x14a4,kActorGoalHeight=0x14a8;
inline constexpr std::size_t kActorGoalNode=0x14b0,kActorGoalVolume=0x14b8,kActorGoalEntity=0x14c0;
inline constexpr std::size_t kActorGoalEntityInterval=0x14c8;
inline constexpr std::size_t kActorPathEnemyLookahead=0x14cc,kActorPathEnemyFightDist=0x14d0,kActorMeleeAttackDist=0x14d4;
inline constexpr std::size_t kActorFixedNode=0x14dd,kActorDropWeapon=0x14ec;
inline constexpr std::size_t kActorDesiredAngle=0x100;
inline constexpr std::size_t kActorAllowPain=0x164,kActorIgnoreSuppression=0xd28,kActorNoDodgeMove=0x13e0;
inline constexpr std::size_t kEntityMaxHealth=0x168;
inline constexpr std::size_t kEntityAngles=0xf4;        // entity field "angles" (yaw at +4)
inline constexpr std::size_t kClientViewAngles=0x108,kClientViewHeight=0x118;
inline constexpr float kGoalRadiusMinimum=4.f;          // native setter clamp (0x385abc)

// Native entity-handle setter used by setgoalentity/setgoalpos.
inline constexpr std::uintptr_t kEntityHandleSetRva=0x15b190;
inline constexpr std::array<std::uint8_t,15> kEntityHandleSetPrologue={
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18};
inline constexpr std::uintptr_t kSetGoalEntityRva=0x14f070,kSetGoalEntityHandleCallRva=0x14f0c9;
inline constexpr std::uintptr_t kGoalPositionHandleCallRva=0x150501;

// Script VM value pushes (IncInParam then type/value) and builtins.
inline constexpr std::uintptr_t kScrAddIntRva=0x222140,kScrAddFloatRva=0x222250,kScrAddStringRva=0x222300;
inline constexpr std::uintptr_t kScrAddVectorRva=0x2223d0,kScrAddEntityNumRva=0x2221a0,kScrAddUndefinedRva=0x222380;
inline constexpr std::uintptr_t kScrIncInParamRva=0x221ff0;
struct BuiltinBinding { const char* name; std::uintptr_t rva; };
inline constexpr BuiltinBinding kBuiltinGiveWeapon{"giveweapon",0x15fba0};
inline constexpr BuiltinBinding kBuiltinTakeAllWeapons{"takeallweapons",0x15fd70};
inline constexpr BuiltinBinding kBuiltinSwitchToWeapon{"switchtoweapon",0x1603c0};
inline constexpr BuiltinBinding kBuiltinGiveMaxAmmo{"givemaxammo",0x1606c0};
inline constexpr BuiltinBinding kBuiltinSetWeaponAmmoClip{"setweaponammoclip",0x161b30};
inline constexpr BuiltinBinding kBuiltinSetWeaponAmmoStock{"setweaponammostock",0x161cb0};
inline constexpr BuiltinBinding kBuiltinIPrintLnBold{"iprintlnbold",0x190ad0};
inline constexpr BuiltinBinding kBuiltinOrientMode{"orientmode",0x14e560};
inline constexpr BuiltinBinding kBuiltinGetWeaponAmmoClip{"getweaponammoclip",0x161e10};
inline constexpr BuiltinBinding kBuiltinMin{"min",0x189d50};
inline constexpr BuiltinBinding kBuiltinDistance{"distance",0x18a250};
inline constexpr std::array<BuiltinBinding,11> kZombieBuiltins={kBuiltinGiveWeapon,kBuiltinTakeAllWeapons,
    kBuiltinSwitchToWeapon,kBuiltinGiveMaxAmmo,kBuiltinSetWeaponAmmoClip,kBuiltinSetWeaponAmmoStock,
    kBuiltinIPrintLnBold,kBuiltinOrientMode,kBuiltinGetWeaponAmmoClip,kBuiltinMin,kBuiltinDistance};

// Console command buffer (critical-section protected) and the native caller
// that queues "spmap %s" from the mission flow.
inline constexpr std::uintptr_t kCbufAddTextRva=0x1ee890,kCbufAddTextCallerRva=0x171060;

// G_Damage stack arguments: mod (arg 8), weapon (arg 9), hit location (arg 10).
inline constexpr int kModUnknown=0,kModPistolBullet=1,kModRifleBullet=2,kModExplosiveBullet=3,kModGrenade=4,
    kModGrenadeSplash=5,kModProjectile=6,kModProjectileSplash=7,kModMelee=8,kModHeadShot=9,kModCrush=10,
    kModFalling=11,kModSuicide=12,kModTriggerHurt=13,kModExplosive=14,kModImpact=15;
inline constexpr int kHitNone=0,kHitHelmet=1,kHitHead=2,kHitNeck=3,kHitTorsoUpper=4,kHitTorsoLower=5,kHitShield=19;
} // namespace mod
