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
// Test41: the actor's current path (path_t at actor+0xfa4). Its point count is
// the signed byte at +0x380 that Path_Exists (0x144d30) tests; the pathgoalpos
// getter (0x13ac40) answers only when Actor_HasPath (0x127bb0) sees it > 0.
inline constexpr std::size_t kActorPath=0xfa4,kPathPointCount=0x380;
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
// Scr_AddString (0x222300) interns with SL_GetString(str, 0) at 0x21a670, which
// passes type 7: strings the DLL pins for builtins use exactly that call.
inline constexpr unsigned kScrAddStringType=7;
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
    DisableWhizby,DisableDangerReact,BattleChatterOff,EnableSprint,DisableTurnAnims,Count};
struct ScriptFunctionBinding { const char* file; const char* function; int params; };
inline constexpr std::array<ScriptFunctionBinding,static_cast<std::size_t>(SetupFunction::Count)> kSetupFunctions={{
    {"maps/_utility","gun_remove",0},{"maps/_utility","disable_arrivals",0},{"maps/_utility","disable_exits",0},
    {"maps/_utility","disable_pain",0},{"maps/_utility","disable_long_death",0},{"maps/_utility","disable_surprise",0},
    {"maps/_utility","disable_bulletwhizbyreaction",0},{"maps/_utility","disable_danger_react",0},
    {"maps/_utility","set_battlechatter",1},{"maps/_utility","enable_sprint",0},
    // Test44: self.noTurnAnims, so move.gsc's pathChange never plays MW2's
    // soldier run_turn_* anims when a zombie's path bends.
    {"maps/_utility","disable_turnanims",0}}};

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

// ---- Test43: BO1 zombie sounds ----
// The generated sound fastfile loads with the level zone like the models.
// A sound plays the way the "playsound" method plays it with one argument
// (0x195d80): G_SoundAliasIndex(name) (0x1a0750, a 256-slot ring of sound
// alias configstrings, never an error) then G_AddEvent(ent, EV_SOUND_ALIAS 3,
// index) (0x19e570); the client plays the alias on the entity.
inline constexpr int kAssetTypeSound=11;
inline constexpr std::uintptr_t kGSoundAliasIndexRva=0x1a0750,kGAddEventRva=0x19e570;
inline constexpr std::array<std::uint8_t,10> kGSoundAliasIndexPrologue={0x40,0x56,0x48,0x83,0xec,0x20,0x80,0x39,0x00,0x48};
inline constexpr std::array<std::uint8_t,15> kGAddEventPrologue={0x40,0x53,0x48,0x83,0xec,0x20,0x41,0x8b,0xc0,0x44,0x8b,0xd2,0x4c,0x8b,0x81};
inline constexpr int kEventSoundAlias=3;
// The "playsound" method is a 16-byte thunk: mov edx, 3 (EV_SOUND_ALIAS);
// jmp 0x195d80. Its first 5 bytes are redirected so MW2's own animscripts
// play BO1 sounds for zombies: footsteps ("step_run_<surface>" ->
// "fly_step_zombie_<surface>") and no soldier pain/death voices.
inline constexpr std::uintptr_t kPlaySoundMethodRva=0x192160,kPlaySoundImplRva=0x195d80;
inline constexpr std::array<std::uint8_t,16> kPlaySoundMethodBytes={0xba,0x03,0x00,0x00,0x00,0xe9,0x16,0x3c,0x00,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
inline constexpr std::array<std::uint8_t,15> kPlaySoundImplPrologue={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x56,0x48};
inline constexpr BuiltinBinding kBuiltinPlaySound{"playsound",0x192160};
inline constexpr std::array<BuiltinBinding,1> kSoundBuiltins={kBuiltinPlaySound};
// Scr_GetString(index) (0x223440: Scr_GetConstString then SL_ConvertToString)
// and Scr_GetNumParam (0x223180: the VM's outparamcount).
inline constexpr std::uintptr_t kScrGetStringRva=0x223440,kScrGetNumParamRva=0x223180;
inline constexpr std::array<std::uint8_t,15> kScrGetStringPrologue={0x48,0x83,0xec,0x28,0xe8,0xf7,0xf7,0xff,0xff,0x8b,0xc8,0x48,0x83,0xc4,0x28};
inline constexpr std::array<std::uint8_t,7> kScrGetNumParamBytes={0x8b,0x05,0x36,0x13,0x6d,0x01,0xc3};

// ---- Test44: a larger loaded_sound asset pool ----
// Favela and Terminal stopped with "Exceeded limit of 1350 'loaded_sound'
// assets" once the BO1 sounds joined the level. The asset pools are static
// free lists named by two .data tables: DB_XAssetPool[type] (0x3bfe00, the
// pool: free head + entries) and g_poolSize[type] (0x3bf930). DB_Init
// (0xeff10, called once from 0x1fce6f) sets 0xb69438 and links every pool
// with its type's init routine (table 0x3bf9e0; loaded_sound 0xeec00 links
// count entries of 0x40 bytes after the 8-byte head). Allocation (0xeee20)
// pops the head; freeing (0xee6c0) pushes it. Nothing else addresses the
// loaded_sound pool, so from DllMain, before DB_Init, the two table entries
// may name a larger, zeroed pool.
inline constexpr std::uintptr_t kPoolSizeRva=0x3bf930,kXAssetPoolRva=0x3bfe00;   // names: kAssetNamesRva (0x3bf360)
inline constexpr std::uintptr_t kPoolInitTableRva=0x3bf9e0,kPoolAllocTableRva=0x3bfb40,kPoolFreeTableRva=0x3bfca0;
inline constexpr std::uintptr_t kDbInitRva=0xeff10,kDbInitCallRva=0x1fce6f,kDbInitDoneRva=0xb69438;
inline constexpr int kAssetTypeLoadedSound=13,kLoadedSoundStockCount=1350,kLoadedSoundExpandedCount=4096;
inline constexpr std::uintptr_t kLoadedSoundPoolRva=0x983740,kLoadedSoundPoolInitRva=0xeec00,kPoolAllocRva=0xeee20,kPoolFreeRva=0xee6c0;
inline constexpr std::size_t kLoadedSoundEntryBytes=0x40;
inline constexpr std::array<std::uint8_t,76> kLoadedSoundPoolInitBytes={
    0x4c,0x63,0xd2,0x4c,0x8d,0x41,0x08,0x49,0xff,0xca,0x4c,0x89,0x01,0x4d,0x85,0xd2,0x7e,0x2c,0xba,0x01,0x00,0x00,0x00,0x4d,
    0x8b,0xca,0x66,0x0f,0x1f,0x44,0x00,0x00,0x48,0x63,0xc2,0x4d,0x8d,0x40,0x40,0x48,0xc1,0xe0,0x06,0xff,0xc2,0x48,0x83,0xc0,
    0x08,0x48,0x03,0xc1,0x49,0x89,0x40,0xc0,0x49,0x83,0xe9,0x01,0x75,0xe2,0x49,0xc1,0xe2,0x06,0x49,0xc7,0x44,0x0a,0x08,0x00,
    0x00,0x00,0x00,0xc3};
inline constexpr std::array<std::uint8_t,15> kPoolAllocBytes={0x48,0x8b,0x01,0x48,0x85,0xc0,0x74,0x07,0x48,0x8b,0x10,0x48,0x89,0x11,0xc3};
inline constexpr std::array<std::uint8_t,10> kPoolFreeBytes={0x48,0x8b,0x01,0x48,0x89,0x11,0x48,0x89,0x02,0xc3};

// ---- Test44: the AI navigation graph (include/nav_graph.hpp) ----
// Path_ConvertIndexToNode (0x1a6320): nodes + index * 0xa8, the node array
// pointer at 0xcf7090 (game_map_sp path data: node capacity at 0xcf7088).
// Path_LoadNode (0x1a5730) counts loaded nodes in 0xf56484 (limit 0x2000);
// getallnodes (0x1aa8d0) iterates that count. Path_NodesInRadius (0x1a5940)
// reads the origin at +0x14 and skips nodes whose wLinkCount (+0x6c) is 0;
// the A* (e.g. 0x13fef0) walks links[0 .. wLinkCount) of 0xc bytes from
// +0x40, target node u16 at +4, cost float at +0; the link check (0x146330)
// reads totalLinkCount (+0x38) and disconnectCount (+6).
inline constexpr std::uintptr_t kPathNodeIndexRva=0x1a6320,kPathNodesRva=0xcf7090,kPathNodeCapacityRva=0xcf7088,kPathNodeCountRva=0xf56484;
inline constexpr std::array<std::uint8_t,18> kPathNodeIndexBytes={0x48,0x63,0xc1,0x48,0x69,0xc0,0xa8,0x00,0x00,0x00,0x48,0x03,0x05,0x5f,0x0d,0xb5,0x00,0xc3};

// ---- Test45: mission music, world models (powerups, gib pieces) ----
// The "musicplay" builtin (0x192ef0, function table entry 0x3c2a78) starts a
// level's music: Scr_GetNumParam, the alias, then SV_GameSendServerCommand
// "mu_play %s %i". Its first 16 bytes (aligned) are exchanged for a jump that
// returns at once while a zombies level is loaded. "musicstop" (0x192fa0)
// sends "mu_stop" (an optional fade time).
inline constexpr std::uintptr_t kMusicPlayRva=0x192ef0;
inline constexpr std::array<std::uint8_t,16> kMusicPlayPrologue={0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0x75,0x96,0xc0,0x00,0x80,0x78,0x10,0x00,0x74};
inline constexpr BuiltinBinding kBuiltinMusicPlay{"musicplay",0x192ef0};
inline constexpr BuiltinBinding kBuiltinMusicStop{"musicstop",0x192fa0};
// spawn("script_model", origin) (function), then the methods a script uses on
// a script model: setmodel, rotateto(angles, time, accel, decel),
// rotatevelocity(velocity, time), movegravity(velocity, time), hide/show and
// delete; gettagorigin/gettagangles(tag) on an actor; detach(model, tag).
inline constexpr BuiltinBinding kBuiltinSpawn{"spawn",0x187370};
inline constexpr BuiltinBinding kBuiltinDelete{"delete",0x189240};
inline constexpr BuiltinBinding kBuiltinRotateTo{"rotateto",0x196cb0};
inline constexpr BuiltinBinding kBuiltinRotateVelocity{"rotatevelocity",0x1971c0};
inline constexpr BuiltinBinding kBuiltinMoveGravity{"movegravity",0x196950};
inline constexpr BuiltinBinding kBuiltinGetTagOrigin{"gettagorigin",0x18d080};
inline constexpr BuiltinBinding kBuiltinGetTagAngles{"gettagangles",0x18d150};
inline constexpr BuiltinBinding kBuiltinDetach{"detach",0x1876c0};
// stopsounds (0x185cd0): G_AddEvent(ent, EV_STOP_SOUNDS 5, 0); the ambience
// bed and announcer on the player stop when the game ends.
inline constexpr BuiltinBinding kBuiltinStopSounds{"stopsounds",0x185cd0};
inline constexpr std::array<BuiltinBinding,11> kWorldBuiltins={kBuiltinMusicPlay,kBuiltinMusicStop,kBuiltinSpawn,kBuiltinDelete,kBuiltinRotateTo,
    kBuiltinRotateVelocity,kBuiltinMoveGravity,kBuiltinGetTagOrigin,kBuiltinGetTagAngles,kBuiltinDetach,kBuiltinStopSounds};
// The entity array's in-use bytes (+0xb0 of each 0x2a0-byte gentity at
// 0xd9e070): a spawned script model is the one entity that became in use
// during the spawn call. Script return values are object ids, not numbers.
inline constexpr std::uintptr_t kEntityPoolRva=0xd9e070;
// Disconnect to the main menu after GAME OVER ("disconnect" through the
// command buffer, as the in-game menus do).
inline constexpr const char* kDisconnectCommand="disconnect\n";
} // namespace mod
