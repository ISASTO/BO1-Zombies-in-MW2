#define COBJMACROS
#include <d3d9.h>
#include "diagnostic_io.h"

static HMODULE self_module;
static HMODULE system_d3d;
static INIT_ONCE initialize_once = INIT_ONCE_STATIC_INIT;
static HANDLE log_file = INVALID_HANDLE_VALUE;

static void OpenLog(void) {
    wchar_t path[MAX_PATH];
    wchar_t suffix[32] = L"\\startup-00000000.log";
    static const wchar_t digits[] = L"0123456789abcdef";
    DWORD pid = GetCurrentProcessId();
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    unsigned n;
    if (!count || count >= MAX_PATH) {
        count = GetTempPathW(MAX_PATH, path);
        if (!count || count >= MAX_PATH) return;
    }
    if (!DiagnosticAppend(path, MAX_PATH, L"\\BO1MW2")) return;
    CreateDirectoryW(path, NULL);
    if (!DiagnosticAppend(path, MAX_PATH, L"\\startup")) return;
    CreateDirectoryW(path, NULL);
    for (n = 0; n < 8; ++n) suffix[9 + n] = digits[(pid >> ((7 - n) * 4)) & 15];
    if (!DiagnosticAppend(path, MAX_PATH, suffix)) return;
    log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DiagnosticWrite(log_file, "BO1MW2 0.1.1-startup-test2 | AMD64 forwarding control\r\n");
    DiagnosticWrite(log_file, "FIRST EXPORT REACHED. No game hooks, scans, input, rendering, or memory writes.\r\n");
    DiagnosticPath(log_file, "log=", path);
}
static BOOL CALLBACK Initialize(PINIT_ONCE once, PVOID parameter, PVOID* context) {
    wchar_t path[MAX_PATH];
    UINT length;
    DWORD error;
    (void)once; (void)parameter; (void)context;
    OpenLog();
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) DiagnosticPath(log_file, "host=", path);
    if (GetModuleFileNameW(self_module, path, MAX_PATH)) DiagnosticPath(log_file, "proxy=", path);
    length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length >= MAX_PATH || !DiagnosticAppend(path, MAX_PATH, L"\\d3d9.dll")) {
        DiagnosticError(log_file, "SYSTEM PATH FAILED error=", GetLastError());
        return TRUE;
    }
    DiagnosticPath(log_file, "SYSTEM LOAD BEGIN path=", path);
    system_d3d = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    error = system_d3d ? ERROR_SUCCESS : GetLastError();
    DiagnosticHex(log_file, "SYSTEM LOAD END handle=", (uintptr_t)system_d3d);
    if (!system_d3d) DiagnosticError(log_file, "SYSTEM LOAD FAILED error=", error);
    if (system_d3d == self_module) {
        DiagnosticWrite(log_file, "SYSTEM LOAD RETURNED PROXY; forwarding blocked to prevent recursion.\r\n");
        system_d3d = NULL;
    }
    if (system_d3d && GetModuleFileNameW(system_d3d, path, MAX_PATH))
        DiagnosticPath(log_file, "system_module=", path);
    return TRUE;
}
static FARPROC Resolve(const char* name) {
    FARPROC address;
    if (!InitOnceExecuteOnce(&initialize_once, Initialize, NULL, NULL)) return NULL;
    address = system_d3d ? GetProcAddress(system_d3d, name) : NULL;
    if (!address) {
        DiagnosticWrite(log_file, "EXPORT UNAVAILABLE name=");
        DiagnosticWrite(log_file, name); DiagnosticWrite(log_file, "\r\n");
    }
    return address;
}
#define LOAD_FUNCTION(type, variable, name) union { FARPROC raw; type call; } variable; variable.raw = Resolve(name)
typedef IDirect3D9* (WINAPI* Create9Fn)(UINT);
typedef HRESULT (WINAPI* Create9ExFn)(UINT, IDirect3D9Ex**);
typedef int (WINAPI* BeginEventFn)(D3DCOLOR, LPCWSTR);
typedef int (WINAPI* EndEventFn)(void);
typedef DWORD (WINAPI* GetStatusFn)(void);
typedef BOOL (WINAPI* QueryRepeatFn)(void);
typedef void (WINAPI* SetMarkerFn)(D3DCOLOR, LPCWSTR);
typedef void (WINAPI* SetOptionsFn)(DWORD);
typedef void (WINAPI* MuteFn)(void);

IDirect3D9* WINAPI Direct3DCreate9(UINT version) {
    IDirect3D9* result;
    LOAD_FUNCTION(Create9Fn, fn, "Direct3DCreate9");
    DiagnosticHex(log_file, "Direct3DCreate9 BEGIN sdk=", version);
    result = fn.call ? fn.call(version) : NULL;
    DiagnosticHex(log_file, "Direct3DCreate9 END factory=", (uintptr_t)result);
    return result;
}
HRESULT WINAPI Direct3DCreate9Ex(UINT version, IDirect3D9Ex** output) {
    HRESULT result;
    LOAD_FUNCTION(Create9ExFn, fn, "Direct3DCreate9Ex");
    DiagnosticHex(log_file, "Direct3DCreate9Ex BEGIN sdk=", version);
    if (fn.call) result = fn.call(version, output);
    else { if (output) *output = NULL; result = D3DERR_NOTAVAILABLE; }
    DiagnosticHex(log_file, "Direct3DCreate9Ex END hr=", (DWORD)result);
    return result;
}
int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name) {
    LOAD_FUNCTION(BeginEventFn, fn, "D3DPERF_BeginEvent"); return fn.call ? fn.call(color, name) : -1;
}
int WINAPI D3DPERF_EndEvent(void) {
    LOAD_FUNCTION(EndEventFn, fn, "D3DPERF_EndEvent"); return fn.call ? fn.call() : -1;
}
DWORD WINAPI D3DPERF_GetStatus(void) {
    LOAD_FUNCTION(GetStatusFn, fn, "D3DPERF_GetStatus"); return fn.call ? fn.call() : 0;
}
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    LOAD_FUNCTION(QueryRepeatFn, fn, "D3DPERF_QueryRepeatFrame"); return fn.call ? fn.call() : FALSE;
}
void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name) {
    LOAD_FUNCTION(SetMarkerFn, fn, "D3DPERF_SetMarker"); if (fn.call) fn.call(color, name);
}
void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name) {
    LOAD_FUNCTION(SetMarkerFn, fn, "D3DPERF_SetRegion"); if (fn.call) fn.call(color, name);
}
void WINAPI D3DPERF_SetOptions(DWORD options) {
    LOAD_FUNCTION(SetOptionsFn, fn, "D3DPERF_SetOptions"); if (fn.call) fn.call(options);
}
void WINAPI DebugSetMute(void) {
    LOAD_FUNCTION(MuteFn, fn, "DebugSetMute"); if (fn.call) fn.call();
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
