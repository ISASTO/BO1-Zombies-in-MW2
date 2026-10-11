#pragma once
#include "core.hpp"
#include "player_fields.hpp"
#include "player_controls.hpp"
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <deque>
#include <mutex>

namespace mod {
void InitializeLog();
void Log(const char* format, ...);
void InstallCrashHandler();
const wchar_t* LogDirectory();

enum class LevelState { Unsupported, Unresolved, NoLevel, Transition, PlayerDead, Ready };
struct FovSnapshot { bool resolved=false; bool valid=false; float current=0.f; std::uintptr_t dvar=0; };
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
};
const char* StateText(LevelState state);
void InitializeEngine();
Snapshot ReadSnapshot();
bool FeatureAvailable(const Snapshot& snapshot, PlayerFeature feature);
bool TogglePlayerFeature(PlayerFeature feature);
void ResetPlayerFeatures();
void UpdatePlayerFeatures(ULONGLONG now);
PlayerPreferences GetPlayerPreferences();
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
    void attach(IDirect3DDevice9* d, HWND w);
    void detach();
    void invalidate();
    void frame();
    void queue(Input input);
};
void HookDirect3D(IDirect3D9* object);
void HookDevice(IDirect3DDevice9* device, HWND window);
} // namespace mod
