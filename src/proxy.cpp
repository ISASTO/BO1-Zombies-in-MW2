#include "runtime.hpp"
#include <array>
#include <cwchar>
#include <memory>
#include <mutex>
#include <vector>

namespace {
HMODULE systemD3D = nullptr;
std::once_flag initializeOnce;
bool targetHost = false;

void Initialize() {
    wchar_t host[32768]{};
    GetModuleFileNameW(nullptr, host, 32768);
    const wchar_t* file = std::wcsrchr(host, L'\\');
    file = file ? file + 1 : host;
    targetHost = _wcsicmp(file, L"iw4sp.exe") == 0;
    if (targetHost) {
        mod::InitializeLog();
        mod::InstallCrashHandler();
        mod::InitializeEngine();
    }
    wchar_t path[MAX_PATH]{};
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length && length + 10 < MAX_PATH) {
        std::wcscat(path, L"\\d3d9.dll");
        systemD3D = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (targetHost) mod::Log("PROXY system_d3d9=%p path=%ls", systemD3D, path);
}
template<class T> T SystemFunction(const char* name) {
    std::call_once(initializeOnce, Initialize);
    const FARPROC address = systemD3D ? GetProcAddress(systemD3D, name) : nullptr;
    T function = nullptr;
    static_assert(sizeof(function) == sizeof(address), "Windows function pointer ABI mismatch");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
}

namespace mod {
namespace {
struct FactoryRecord {
    IDirect3D9* object;
    void** original;
    std::array<void*, 17> table;
};
struct DeviceRecord {
    IDirect3DDevice9* object;
    void** original;
    std::array<void*, 119> table;
    Overlay overlay;
    std::atomic<bool> retired{false};
};
std::mutex recordsMutex;
// Records and their shadow vtables live until process exit. Hot-unload is unsupported.
std::vector<std::unique_ptr<FactoryRecord>> factories;
std::vector<std::unique_ptr<DeviceRecord>> devices;
FactoryRecord* Factory(IDirect3D9* object) {
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (auto i = factories.rbegin(); i != factories.rend(); ++i) if ((*i)->object == object) return i->get();
    return nullptr;
}
DeviceRecord* Device(IDirect3DDevice9* object) {
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (auto i = devices.rbegin(); i != devices.rend(); ++i)
        if ((*i)->object == object && !(*i)->retired) return i->get();
    return nullptr;
}
bool ReplaceVtable(void* object, void** table) {
    DWORD protection = 0;
    if (!VirtualProtect(object, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(object), table);
    DWORD ignored = 0;
    VirtualProtect(object, sizeof(void*), protection, &ignored);
    return true;
}
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using EndSceneFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using ReleaseFn = ULONG (STDMETHODCALLTYPE*)(IDirect3DDevice9*);

HRESULT STDMETHODCALLTYPE OnCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND window, DWORD behavior,
                                         D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** output) {
    auto* r = Factory(self);
    if (!r) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<CreateDeviceFn>(r->original[16]);
    HRESULT result = original(self, adapter, type, window, behavior, params, output);
    Log("D3D CreateDevice hr=0x%08lx adapter=%u flags=0x%08lx window=%p device=%p", result, adapter, behavior,
        window, SUCCEEDED(result) && output ? *output : nullptr);
    if (SUCCEEDED(result) && output && *output) HookDevice(*output, params && params->hDeviceWindow ? params->hDeviceWindow : window);
    return result;
}
bool BackbufferBound(IDirect3DDevice9* device) {
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* back = nullptr;
    const bool ok = SUCCEEDED(device->GetRenderTarget(0, &target)) &&
                    SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && target == back;
    if (target) target->Release();
    if (back) back->Release();
    return ok;
}
HRESULT STDMETHODCALLTYPE OnEndScene(IDirect3DDevice9* self) {
    auto* r = Device(self);
    if (!r) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<EndSceneFn>(r->original[42]);
    ++r->overlay.endScenes;
    if (r->overlay.endScenes == 1) Log("HOOK EndScene reached device=%p thread=%lu", self, GetCurrentThreadId());
    if (BackbufferBound(self)) {
        r->overlay.frame();
        ++r->overlay.drawnSincePresent;
    }
    return original(self);
}
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9* self, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
    auto* r = Device(self);
    if (!r) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<PresentFn>(r->original[17]);
    ++r->overlay.presents;
    if (r->overlay.presents == 1) Log("HOOK Present reached device=%p thread=%lu", self, GetCurrentThreadId());
    // Fallback if the engine's last EndScene was on an offscreen target, or it skipped EndScene.
    if (!r->overlay.drawnSincePresent) {
        IDirect3DSurface9* savedTarget = nullptr;
        IDirect3DSurface9* savedDepth = nullptr;
        IDirect3DSurface9* back = nullptr;
        D3DVIEWPORT9 viewport{};
        HRESULT targetResult = self->GetRenderTarget(0, &savedTarget);
        const HRESULT depthResult = self->GetDepthStencilSurface(&savedDepth);
        HRESULT viewportResult = self->GetViewport(&viewport);
        if (SUCCEEDED(targetResult) && SUCCEEDED(viewportResult) &&
            SUCCEEDED(self->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
            self->SetDepthStencilSurface(nullptr);
            if (SUCCEEDED(self->SetRenderTarget(0, back)) && SUCCEEDED(self->BeginScene())) {
                r->overlay.frame();
                reinterpret_cast<EndSceneFn>(r->original[42])(self);
            }
            self->SetRenderTarget(0, savedTarget);
            if (SUCCEEDED(depthResult)) self->SetDepthStencilSurface(savedDepth);
            self->SetViewport(&viewport);
        }
        if (savedTarget) savedTarget->Release();
        if (savedDepth) savedDepth->Release();
        if (back) back->Release();
    }
    r->overlay.drawnSincePresent = 0;
    return original(self, source, destination, window, dirty);
}
HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params) {
    auto* r = Device(self);
    if (!r) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<ResetFn>(r->original[16]);
    r->overlay.invalidate();
    ++r->overlay.resets;
    Log("D3D Reset begin count=%u", r->overlay.resets);
    HRESULT result = original(self, params);
    r->overlay.drawnSincePresent = 0;
    Log("D3D Reset end hr=0x%08lx", result);
    if (SUCCEEDED(result) && params && params->hDeviceWindow && params->hDeviceWindow != r->overlay.window)
        r->overlay.attach(self, params->hDeviceWindow);
    return result;
}
ULONG STDMETHODCALLTYPE OnRelease(IDirect3DDevice9* self) {
    auto* r = Device(self);
    if (!r) return 0;
    auto original = reinterpret_cast<ReleaseFn>(r->original[2]);
    ULONG count = original(self);
    if (!count) {
        r->retired = true;
        r->overlay.detach();
        // Do not access a COM object after final Release. Process-lifetime record owns no AddRef.
        r->overlay.font = nullptr;
        Log("D3D device final Release device=%p", self);
    }
    return count;
}
}
void HookDirect3D(IDirect3D9* object) {
    if (auto* existing = Factory(object); existing && *reinterpret_cast<void***>(object) == existing->table.data()) return;
    auto r = std::make_unique<FactoryRecord>();
    r->object = object;
    r->original = *reinterpret_cast<void***>(object);
    std::memcpy(r->table.data(), r->original, sizeof(r->table));
    r->table[16] = reinterpret_cast<void*>(OnCreateDevice);
    FactoryRecord* raw = r.get();
    { std::lock_guard<std::mutex> lock(recordsMutex); factories.push_back(std::move(r)); }
    Log("HOOK IDirect3D9 shadow vtable installed=%d object=%p", ReplaceVtable(object, raw->table.data()), object);
}
void HookDevice(IDirect3DDevice9* object, HWND window) {
    if (Device(object)) return;
    auto r = std::make_unique<DeviceRecord>();
    r->object = object;
    r->original = *reinterpret_cast<void***>(object);
    std::memcpy(r->table.data(), r->original, sizeof(r->table));
    r->table[2] = reinterpret_cast<void*>(OnRelease);
    r->table[16] = reinterpret_cast<void*>(OnReset);
    r->table[17] = reinterpret_cast<void*>(OnPresent);
    r->table[42] = reinterpret_cast<void*>(OnEndScene);
    DeviceRecord* raw = r.get();
    { std::lock_guard<std::mutex> lock(recordsMutex); devices.push_back(std::move(r)); }
    if (!ReplaceVtable(object, raw->table.data())) { Log("HOOK device failed error=%lu", GetLastError()); return; }
    Log("HOOK device shadow vtable installed object=%p Reset[16] Present[17] EndScene[42]", object);
    raw->overlay.attach(object, window);
}
} // namespace mod

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT version) {
    auto fn = SystemFunction<IDirect3D9* (WINAPI*)(UINT)>("Direct3DCreate9");
    IDirect3D9* object = fn ? fn(version) : nullptr;
    if (targetHost && object) mod::HookDirect3D(object);
    return object;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT version, IDirect3D9Ex** output) {
    auto fn = SystemFunction<HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)>("Direct3DCreate9Ex");
    return fn ? fn(version, output) : D3DERR_NOTAVAILABLE;
}
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name) {
    auto fn = SystemFunction<int (WINAPI*)(D3DCOLOR, LPCWSTR)>("D3DPERF_BeginEvent"); return fn ? fn(color, name) : -1;
}
extern "C" int WINAPI D3DPERF_EndEvent() {
    auto fn = SystemFunction<int (WINAPI*)()>("D3DPERF_EndEvent"); return fn ? fn() : -1;
}
extern "C" DWORD WINAPI D3DPERF_GetStatus() {
    auto fn = SystemFunction<DWORD (WINAPI*)()>("D3DPERF_GetStatus"); return fn ? fn() : 0;
}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    auto fn = SystemFunction<BOOL (WINAPI*)()>("D3DPERF_QueryRepeatFrame"); return fn ? fn() : FALSE;
}
extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name) {
    auto fn = SystemFunction<void (WINAPI*)(D3DCOLOR, LPCWSTR)>("D3DPERF_SetMarker"); if (fn) fn(color, name);
}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD options) {
    auto fn = SystemFunction<void (WINAPI*)(DWORD)>("D3DPERF_SetOptions"); if (fn) fn(options);
}
extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name) {
    auto fn = SystemFunction<void (WINAPI*)(D3DCOLOR, LPCWSTR)>("D3DPERF_SetRegion"); if (fn) fn(color, name);
}
extern "C" void WINAPI DebugSetMute() {
    auto fn = SystemFunction<void (WINAPI*)()>("DebugSetMute"); if (fn) fn();
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
