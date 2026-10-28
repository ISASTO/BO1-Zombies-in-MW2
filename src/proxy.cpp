#include "runtime.hpp"
#include "vtable_patch.hpp"
#include "script_vm.hpp"
#include "bo1_models.hpp"
#include "hang_watch.hpp"
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
        mod::LogScriptVm(false);
        mod::InstallCrashHandler();
        mod::hangwatch::Start();
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
struct FactoryTable {
    void** table;
    std::array<SlotPatch, 1> slots;
    bool installed = false;
};
struct DeviceTable {
    void** table;
    // Release, Reset, Present, EndScene. Preserve every other public/private slot.
    std::array<SlotPatch, 4> slots;
    bool installed = false;
};
struct DeviceRecord {
    IDirect3DDevice9* object;
    DeviceTable* hooks;
    Overlay overlay;
    std::atomic<bool> retired{false};
};
std::mutex recordsMutex;
// Records live until process exit. The real objects keep their original vptrs.
// Tables are shared by native objects, so untracked objects must forward too.
std::vector<std::unique_ptr<FactoryTable>> factories;
std::vector<std::unique_ptr<DeviceTable>> deviceTables;
std::vector<std::unique_ptr<DeviceRecord>> devices;
FactoryTable* Factory(IDirect3D9* object) {
    void** table = *reinterpret_cast<void***>(object);
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (const auto& record : factories) if (record->table == table) return record.get();
    return nullptr;
}
DeviceRecord* Device(IDirect3DDevice9* object) {
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (auto i = devices.rbegin(); i != devices.rend(); ++i)
        if ((*i)->object == object && !(*i)->retired) return i->get();
    return nullptr;
}
DeviceTable* Table(IDirect3DDevice9* object) {
    void** table = *reinterpret_cast<void***>(object);
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (const auto& record : deviceTables) if (record->table == table) return record.get();
    return nullptr;
}
bool ExchangeSlot(void** slot, void* replacement, void* expected) {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    void* observed = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot), replacement, expected);
    DWORD ignored = 0;
    if (!VirtualProtect(slot, sizeof(void*), protection, &ignored)) Log("HOOK slot protection restore failed slot=%p error=%lu", slot, GetLastError());
    if (observed != expected) { SetLastError(ERROR_RETRY); return false; }
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
    auto original = reinterpret_cast<CreateDeviceFn>(r->slots[0].original);
    HRESULT result = original(self, adapter, type, window, behavior, params, output);
    Log("D3D CreateDevice hr=0x%08lx adapter=%u flags=0x%08lx window=%p device=%p", result, adapter, behavior,
        window, SUCCEEDED(result) && output ? *output : nullptr);
    if (r->installed && SUCCEEDED(result) && output && *output) HookDevice(*output, params && params->hDeviceWindow ? params->hDeviceWindow : window);
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
    auto* hooks = r ? r->hooks : Table(self);
    if (!hooks || !hooks->slots[3].original) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<EndSceneFn>(hooks->slots[3].original);
    if (!r) return original(self);
    ++r->overlay.endScenes;
    hangwatch::RenderFrame();
    if (r->overlay.endScenes == 1) Log("HOOK EndScene reached device=%p thread=%lu", self, GetCurrentThreadId());
    if (BackbufferBound(self)) {
        r->overlay.frame();
        ++r->overlay.drawnSincePresent;
    }
    return original(self);
}
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9* self, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
    auto* r = Device(self);
    auto* hooks = r ? r->hooks : Table(self);
    if (!hooks || !hooks->slots[2].original) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<PresentFn>(hooks->slots[2].original);
    if (!r) return original(self, source, destination, window, dirty);
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
                reinterpret_cast<EndSceneFn>(hooks->slots[3].original)(self);
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
    auto* hooks = r ? r->hooks : Table(self);
    if (!hooks || !hooks->slots[1].original) return D3DERR_INVALIDCALL;
    auto original = reinterpret_cast<ResetFn>(hooks->slots[1].original);
    if (!r) return original(self, params);
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
    auto* hooks = r ? r->hooks : Table(self);
    if (!hooks || !hooks->slots[0].original) return 0;
    auto original = reinterpret_cast<ReleaseFn>(hooks->slots[0].original);
    ULONG count = original(self);
    if (r && !count) {
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
    void** table = *reinterpret_cast<void***>(object);
    std::lock_guard<std::mutex> lock(recordsMutex);
    for (const auto& record : factories) if (record->table == table) return;
    auto record = std::make_unique<FactoryTable>();
    record->table = table;
    record->slots[0] = {16, reinterpret_cast<void*>(OnCreateDevice), nullptr};
    FactoryTable* raw = record.get();
    factories.push_back(std::move(record));
    const auto result = InstallVtableSlots(table, raw->slots.data(), raw->slots.size(), ExchangeSlot);
    raw->installed = result.installed;
    Log("HOOK factory public slot installed=%d rollback=%d object=%p table=%p vptr_unchanged=%d",
        result.installed, result.rollbackComplete, object, table, *reinterpret_cast<void***>(object) == table);
}
void HookDevice(IDirect3DDevice9* object, HWND window) {
    DeviceRecord* instance = nullptr;
    void** table = *reinterpret_cast<void***>(object);
    {
        std::lock_guard<std::mutex> lock(recordsMutex);
        for (const auto& record : devices) if (record->object == object && !record->retired) return;
        DeviceTable* hooks = nullptr;
        for (const auto& record : deviceTables) if (record->table == table) { hooks = record.get(); break; }
        if (!hooks) {
            auto record = std::make_unique<DeviceTable>();
            record->table = table;
            record->slots = {{{2, reinterpret_cast<void*>(OnRelease), nullptr},
                              {16, reinterpret_cast<void*>(OnReset), nullptr},
                              {17, reinterpret_cast<void*>(OnPresent), nullptr},
                              {42, reinterpret_cast<void*>(OnEndScene), nullptr}}};
            hooks = record.get();
            deviceTables.push_back(std::move(record));
            const auto result = InstallVtableSlots(table, hooks->slots.data(), hooks->slots.size(), ExchangeSlot);
            hooks->installed = result.installed;
            Log("HOOK device public slots installed=%d rollback=%d object=%p table=%p vptr_unchanged=%d error=%lu",
                result.installed, result.rollbackComplete, object, table, *reinterpret_cast<void***>(object) == table,
                result.installed ? 0 : GetLastError());
        }
        if (!hooks->installed) return;
        auto record = std::make_unique<DeviceRecord>();
        record->object = object;
        record->hooks = hooks;
        instance = record.get();
        devices.push_back(std::move(record));
    }
    Log("HOOK device tracked=%p Reset[16] Present[17] EndScene[42]; native private table preserved", object);
    instance->overlay.attach(object, window);
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
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Static import: runs before iw4sp.exe initializes script variables,
        // the memory tree or checkpoint slots. Kernel32-only, fails closed.
        mod::InstallScriptVmExpansion();
    } else if (reason == DLL_PROCESS_DETACH) {
        mod::bo1models::GuardProcessExit();
    }
    return TRUE;
}
