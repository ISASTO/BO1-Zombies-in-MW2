#pragma once
#include "core.hpp"
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
struct Snapshot {
    LevelState state = LevelState::Unresolved;
    bool supported = false;
    bool resolved = false;
    bool serverRunning = false;
    bool god = false;
    int health = 0;
    unsigned flags = 0;
    std::uintptr_t entity = 0;
    char map[96]{};
};
const char* StateText(LevelState state);
void InitializeEngine();
Snapshot ReadSnapshot();
bool ToggleGod();
void LogSnapshot();

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
    void attach(IDirect3DDevice9* d, HWND w);
    void detach();
    void invalidate();
    void frame();
    void queue(Input input);
};
void HookDirect3D(IDirect3D9* object);
void HookDevice(IDirect3DDevice9* device, HWND window);
} // namespace mod
