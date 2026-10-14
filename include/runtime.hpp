#pragma once
#include "core.hpp"
#include "player_fields.hpp"
#include "player_controls.hpp"
#include "control_extensions.hpp"
#include "weapon_inventory.hpp"
#include "zombie_rehearsal.hpp"
#include "ai_controls.hpp"
#include "native_animation.hpp"
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <deque>
#include <mutex>

namespace mod {
void InitializeLog();
void Log(const char* format, ...);
void InstallCrashHandler();
void RecordNativeOperation(const char* operation,unsigned entity=0,int actorCount=-1);
const wchar_t* LogDirectory();

enum class LevelState { Unsupported, Unresolved, NoLevel, Transition, PlayerDead, Ready };
struct FloatDvarSnapshot { bool resolved=false; bool valid=false; float current=0.f; std::uintptr_t dvar=0; };
using FovSnapshot = FloatDvarSnapshot;
struct BoolDvarSnapshot { bool resolved=false; bool valid=false; std::uint8_t current=0; std::uintptr_t dvar=0; };
struct IntDvarSnapshot { bool resolved=false; bool valid=false; int current=0; std::uintptr_t dvar=0; };
struct JumpSnapshot { bool resolved=false; bool valid=false; float height=0.f; float twiceHeight=0.f; };
struct Snapshot {
    LevelState state = LevelState::Unresolved;
    bool supported = false;
    bool resolved = false;
    bool serverRunning = false;
    bool god = false;
    int health = 0;
    int entityNumber = -1;
    PlayerReadStatus playerRead = PlayerReadStatus::NotRead;
    unsigned flags = 0;
    std::uintptr_t entity = 0;
    char map[96]{};
    FovSnapshot fov;
    bool damageRead=false;
    bool takesDamage=false;
    bool clientResolved=false;
    ClientFields client;
    bool ammoResolved=false;
    bool ammoValid=false;
    ReserveTable ammo{};
    BoolDvarSnapshot sustain;
    IntDvarSnapshot speed;
    JumpSnapshot jump;
    BoolDvarSnapshot giveAll;
    BoolDvarSnapshot explosive;
    BoolDvarSnapshot ai;
    FloatDvarSnapshot timeScale;
    FloatDvarSnapshot gravity;
    FloatDvarSnapshot friendlyFireDistance,friendlyFireScale;
    bool gravityLiteralValid=false;
    float gravityLiteral=0.f;
    bool playerGravityRead=false;
    int playerGravity=0;
    bool grenadeSustainResolved=false;
    bool weaponResolved=false;
    unsigned weaponCount=0;
    WeaponFields weapon;
    BoolDvarSnapshot dualWield;
    bool inventoryResolved=false,inventoryValid=false;
    InventoryData inventory;
    bool dualCurrentSupported=false;
    int lastWeaponHand=-1;
    bool recoilResolved=false,recoilValid=false;
    unsigned weaponFlags=0;
    int recoilScale=0;
};
const char* StateText(LevelState state);
void InitializeEngine();
Snapshot ReadSnapshot();
bool FeatureAvailable(const Snapshot& snapshot, PlayerFeature feature);
bool TogglePlayerFeature(PlayerFeature feature);
bool AdjustPlayerFeature(PlayerFeature feature,int direction);
void ResetPlayerFeatures();
void UpdatePlayerFeatures(ULONGLONG now);
PlayerPreferences GetPlayerPreferences();
bool ExtraAvailable(const Snapshot& snapshot,ExtraFeature feature);
bool ToggleExtraFeature(ExtraFeature feature);
bool AdjustExtraFeature(ExtraFeature feature,int direction);
ExtraPreferences GetExtraPreferences();
void ResetWeaponFeatures();
void ResetWorldFeatures();
void SuspendRecoilForCheckpoint(bool suspend);
bool RapidFireApplied(const WeaponFields& weapon);
bool SemiAutoApplied(const WeaponFields& weapon);
struct WeaponModeStatus {
    bool owned=false; int page=0,pages=1,added=0;
    bool dualOwned=false,dualReady=false,dualFallback=false,reconciling=false,needsBaseline=false;
    DualBlocked dualBlocked=DualBlocked::None;
    bool nativeHandsReady=false;
    NativeHandBuildState nativeHandState=NativeHandBuildState::Unknown;
};
WeaponModeStatus GetWeaponModeStatus();
bool AdjustWeaponBank(int direction);
void UpdateZombieRehearsal(const Snapshot& snapshot);
void HandleZombieRehearsal(Action action,const Snapshot& snapshot,int startingRound);
ZombieRehearsalStatus GetZombieRehearsalStatus();
void LogZombieRehearsal();
int NativeActorCapacity();
void LogExpandedResources();
void CheckStockCheckpoint(std::uintptr_t);
bool InstallExpandedCapacity(std::uintptr_t,bool);
bool ExpandedArenaContains(std::uintptr_t,std::size_t);
void InitializeAiEngine(std::uintptr_t base,std::size_t size,std::uintptr_t text,std::size_t textSize,bool supported);
void UpdateAiControls(const Snapshot& snapshot,ULONGLONG now,bool detailed=false);
void RequestAiOperation(AiOperation operation,const Snapshot& snapshot);
void AdjustAiSpawner(bool friendly,int direction);
void ToggleAiSpawnPause();
bool FriendlyFireHookReady();
bool NativeDamageHookReady();
void SetAllowFriendlyFire(bool enabled);
bool MpFeedbackReady();
float MpCrosshairOffset();
void PrepareMpFeedback(struct Overlay&);
void InvalidateMpFeedback();
void PlayMpHitAlert();
void SetHitmarkers(bool enabled);
void SetSpawnedFriendliesInvincible(bool enabled);
std::uint8_t GetHitmarkerAlpha(ULONGLONG now);
AiStatus GetAiStatus();
void LogAiState();
std::uint64_t RequestNativeHandDefinitions(const WeaponFields&,std::uintptr_t names,std::uintptr_t normalTree,
    std::uintptr_t cache,const Snapshot&);
NativeHandBuildResult GetNativeHandBuild(std::uint64_t ticket);
bool SetFov(int value);
void LogSnapshot();
void LoadFovPreference(FovPreference& preference);
bool SaveFovPreference(int value);
bool ClearFovPreference();

struct Overlay {
    IDirect3DDevice9* device = nullptr;
    HWND window = nullptr;
    WNDPROC previousWndProc = nullptr;
    MenuState menu;
    std::atomic<bool> visible{false};
    std::mutex inputMutex;
    std::deque<Input> inputQueue;
    IDirect3DTexture9* font = nullptr;
    bool fontAttempted = false;
    IDirect3DTexture9* hitTexture=nullptr;
    bool hitAttempted=false;
    unsigned presents = 0;
    unsigned endScenes = 0;
    unsigned drawnSincePresent = 0;
    unsigned resets = 0;
    unsigned draws = 0;
    unsigned inputs = 0;
    ULONGLONG lastHeartbeat = 0;
    DWORD lastRenderThread = 0;
    bool settingsLoaded = false;
    const char* settingsResult = "";
    ULONGLONG nextFovAttempt = 0;
    ULONGLONG nextSnapshotRead = 0;
    Snapshot displaySnapshot;
    void attach(IDirect3DDevice9* d, HWND w);
    void detach();
    void invalidate();
    void frame();
    void queue(Input input);
};
void HookDirect3D(IDirect3D9* object);
void HookDevice(IDirect3DDevice9* device, HWND window);
} // namespace mod
