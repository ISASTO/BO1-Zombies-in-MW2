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

// ---- Test39: Spec Ops level preparation and the BO1 animation import ----
// GScr_LoadScripts (0x195200) ends with a tail jump to Scr_EndLoadScripts;
// that jump is redirected so the level's compiled scripts can be queried
// while the compiler's file/function table (scriptsPos) still exists.
inline constexpr std::uintptr_t kLevelScriptsLoadedJumpRva=0x1953f8,kScrEndLoadScriptsRva=0x2166d0;
inline constexpr std::array<std::uint8_t,15> kScrEndLoadScriptsPrologue={
    0x40,0x53,0x48,0x83,0xec,0x20,0xe8,0xd5,0x3c,0x00,0x00,0xb9,0x02,0x00,0x00};
// Scr_GetFunctionHandle(filename, name) -> offset of the compiled function in
// the script program buffer (0 when absent); valid only until Scr_EndLoadScripts.
inline constexpr std::uintptr_t kScrGetFunctionHandleRva=0x216920;
inline constexpr std::array<std::uint8_t,15> kScrGetFunctionHandlePrologue={
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
inline constexpr std::uintptr_t kScrProgramBufferRva=0x15a03e0,kScrProgramSizeRva=0x1463bc8;
// VM_Execute: opcode byte from fs.pos, 0x8c opcodes; opcode 0 (OP_End) kills
// the thread's locals and unwinds the stack to the caller's code position.
inline constexpr std::uintptr_t kVmDispatchRva=0x2249c4,kVmOpcodeTableRva=0x227650,kVmOpEndHandlerRva=0x2249f7;
inline constexpr std::uint8_t kVmOpEnd=0x00;
struct ScriptFunctionRef { const char* file; const char* function; };
// The Spec Ops end-of-mission paths. Entered with OP_End while zombies run,
// so objectives, timers and kill counters can no longer end the level.
inline constexpr std::array<ScriptFunctionRef,3> kSpecOpsEndFunctions={{
    {"maps/_specialops","fade_challenge_out"},
    {"maps/_specialops_code","specialops_mission_over_setup"},
    {"maps/_specialops_code","failure_summary_display"}}};

// Asset database: DB_FindXAssetEntry(type, name) -> entry {type, header@8}.
inline constexpr std::uintptr_t kDbFindXAssetEntryRva=0xef500;
inline constexpr std::array<std::uint8_t,15> kDbFindXAssetEntryPrologue={
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
inline constexpr int kAssetTypeXAnimParts=2;            // g_assetNames[2] == "xanim"
inline constexpr std::uintptr_t kAssetNamesRva=0x3bf360;
inline constexpr std::size_t kAssetEntryHeader=0x8;
// SL_GetString(str, user, type) -> script string id (adds a reference).
inline constexpr std::uintptr_t kSlGetStringRva=0x21b2d0;
inline constexpr std::array<std::uint8_t,10> kSlGetStringPrologue={0x45,0x8b,0xc8,0x48,0xc7,0xc0,0xff,0xff,0xff,0xff};
inline constexpr unsigned kSlGetStringType=4;
// Scr_AddAnim(value): VAR_ANIMATION (13) push used by animscripted's anim argument.
inline constexpr std::uintptr_t kScrAddAnimRva=0x222070;
inline constexpr std::array<std::uint8_t,9> kScrAddAnimPrologue={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
// Entity anim tree (XAnimTree_s*, first field XAnim_s*) and the script animtree table.
inline constexpr std::uintptr_t kEntAnimTreeRva=0x19fd90,kAnimTreeByIndexRva=0x20b0d0,kAnimTreeIndexRva=0x20b0e0;
inline constexpr std::array<std::uint8_t,9> kEntAnimTreePrologue={0x40,0x53,0x48,0x83,0xec,0x20,0x0f,0xb6,0x11};
// XAnim_s: u32 size, then 16-byte entries {u16 numAnims, u16 parent, pad, XAnimParts* @8}
// from +8 (XAnimIsLeafNode 0x29d110, XAnimGetLength 0x29c920).
inline constexpr std::size_t kXAnimEntries=0x8,kXAnimEntryStride=0x10,kXAnimEntryParts=0x8;
inline constexpr BuiltinBinding kBuiltinAnimScripted{"animscripted",0x181c60};
inline constexpr BuiltinBinding kBuiltinStopAnimScripted{"stopanimscripted",0x181c40};
inline constexpr BuiltinBinding kBuiltinHide{"hide",0x192860};
inline constexpr BuiltinBinding kBuiltinShow{"show",0x192830};
inline constexpr std::array<BuiltinBinding,4> kAnimBuiltins={kBuiltinAnimScripted,kBuiltinStopAnimScripted,kBuiltinHide,kBuiltinShow};

// ---- Test39: running MW2's own actor-setup script functions on zombies ----
// Scr_ExecEntThreadNum(entnum, classnum 0, handle, paramcount) runs a compiled
// script function as a thread on an entity and returns the thread id, which
// Scr_FreeThread releases. animscripted itself calls them (0x183365 calls
// Scr_ExecEntThread 0x19ae00, which tail-jumps here; 0x18336d frees).
inline constexpr std::uintptr_t kScrExecEntThreadNumRva=0x2226f0,kScrExecEntThreadRva=0x19ae00,kScrFreeThreadRva=0x222830;
inline constexpr std::array<std::uint8_t,15> kScrExecEntThreadNumPrologue={
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
inline constexpr std::array<std::uint8_t,12> kScrExecEntThreadPrologue={0x0f,0xb7,0x49,0x7e,0x45,0x8b,0xc8,0x44,0x8b,0xc2,0x33,0xd2};
inline constexpr std::array<std::uint8_t,4> kScrFreeThreadPrologue={0x0f,0xb7,0xc9,0xe9};
inline constexpr std::uintptr_t kAnimScriptedExecCallRva=0x183365,kAnimScriptedFreeCallRva=0x18336d;
// actor.allowdeath: without it an actor inside animscripted (rising,
// attacking) cannot drop below 1 health (BO1 zombie_spawn_init sets it).
inline constexpr std::size_t kActorAllowDeath=0x165;
// maps/_utility functions BO1's zombie_spawn_init equivalents map to. Each
// takes no argument except set_battlechatter(false).
enum class SetupFunction : int {
    GunRemove,DisableArrivals,DisableExits,DisablePain,DisableLongDeath,DisableSurprise,
    DisableWhizby,DisableDangerReact,BattleChatterOff,EnableSprint,Count};
struct ScriptFunctionBinding { const char* file; const char* function; int params; };
inline constexpr std::array<ScriptFunctionBinding,static_cast<std::size_t>(SetupFunction::Count)> kSetupFunctions={{
    {"maps/_utility","gun_remove",0},{"maps/_utility","disable_arrivals",0},{"maps/_utility","disable_exits",0},
    {"maps/_utility","disable_pain",0},{"maps/_utility","disable_long_death",0},{"maps/_utility","disable_surprise",0},
    {"maps/_utility","disable_bulletwhizbyreaction",0},{"maps/_utility","disable_danger_react",0},
    {"maps/_utility","set_battlechatter",1},{"maps/_utility","enable_sprint",0}}};

// ---- Test40: BO1 zombie character models ----
// Com_LoadLevelFastFiles (0xf09b0) ends with DB_LoadXAssets({level zone,
// allocFlags 4, freeFlags 0}, 1, async). That call is redirected so the
// generated BO1 model fastfile loads with the level, and is freed with it.
inline constexpr std::uintptr_t kLevelZoneLoadCallRva=0xf0ac3,kDbLoadXAssetsRva=0xf0ae0,kLoadLevelFastFilesRva=0xf09b0;
inline constexpr std::array<std::uint8_t,15> kDbLoadXAssetsPrologue={
    0x48,0x8b,0xc4,0x44,0x89,0x40,0x18,0x89,0x50,0x10,0x48,0x89,0x48,0x08,0x48};
inline constexpr int kLevelZoneAllocFlags=4;
// G_ModelIndex(name): finds or adds the model configstring (0x897+index,
// index 1..0x1ff); adding is only legal while level.initializing.
inline constexpr std::uintptr_t kGModelIndexRva=0x1a02b0,kLevelInitializingRva=0xd96eb8;
inline constexpr std::array<std::uint8_t,12> kGModelIndexPrologue={0x40,0x56,0x48,0x83,0xec,0x20,0x80,0x39,0x00,0x48,0x8b,0xf1};
inline constexpr std::uintptr_t kConfigstringsRva=0x19b58f2,kEmptyStringIdRva=0x1157900;
inline constexpr unsigned kModelConfigstringBase=0x897,kMaxModels=0x200;
inline constexpr int kAssetTypeXModel=4;
inline constexpr BuiltinBinding kBuiltinSetModel{"setmodel",0x1872e0};
inline constexpr BuiltinBinding kBuiltinAttach{"attach",0x187540};
inline constexpr BuiltinBinding kBuiltinDetachAll{"detachall",0x187800};
inline constexpr std::array<BuiltinBinding,3> kModelBuiltins={kBuiltinSetModel,kBuiltinAttach,kBuiltinDetachAll};
// animscripts\shared::HandleNoteTrack(note): the "swap taghelmet to tagleft"
// case moves the MW2 helmet (self.hatModel) to TAG_WEAPON_LEFT and clears
// self.hatModel, so death/pain helmetPop() never looks for it afterwards.
inline constexpr ScriptFunctionRef kHelmetSwapFunction{"animscripts/shared","handlenotetrack"};
inline constexpr const char* kHelmetSwapNote="swap taghelmet to tagleft";
} // namespace mod
