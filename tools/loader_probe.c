#define COBJMACROS
#include <d3d9.h>
#include <shellapi.h>
#include "diagnostic_io.h"

static HANDLE log_file = INVALID_HANDLE_VALUE;
static LONG WINAPI ProbeException(EXCEPTION_POINTERS* info) {
    DiagnosticHex(log_file, "UNHANDLED exception=", info->ExceptionRecord->ExceptionCode);
    DiagnosticHex(log_file, "fault_address=", (uintptr_t)info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
void WINAPI ProbeEntry(void) {
    static const char* exports[] = {
        "Direct3DCreate9", "Direct3DCreate9Ex", "D3DPERF_BeginEvent", "D3DPERF_EndEvent",
        "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame", "D3DPERF_SetMarker",
        "D3DPERF_SetOptions", "D3DPERF_SetRegion", "DebugSetMute"
    };
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    HMODULE module;
    wchar_t path[MAX_PATH];
    union { FARPROC raw; IDirect3D9* (WINAPI* call)(UINT); } create;
    IDirect3D9* factory;
    unsigned n;
    BOOL all_exports = TRUE;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!argv || argc != 3) ExitProcess(2);
    log_file = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE) ExitProcess(3);
    SetUnhandledExceptionFilter(ProbeException);
    DiagnosticWrite(log_file, "BO1MW2 startup probe | AMD64 | no CRT | separate process\r\n");
    DiagnosticPath(log_file, "requested_dll=", argv[1]);
    DiagnosticWrite(log_file, "LoadLibraryEx BEGIN\r\n");
    module = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        DiagnosticError(log_file, "LoadLibraryEx FAILED error=", GetLastError());
        ExitProcess(4);
    }
    DiagnosticHex(log_file, "LoadLibraryEx END module=", (uintptr_t)module);
    if (GetModuleFileNameW(module, path, MAX_PATH)) DiagnosticPath(log_file, "loaded_path=", path);
    for (n = 0; n < sizeof(exports) / sizeof(exports[0]); ++n) {
        if (!GetProcAddress(module, exports[n])) {
            all_exports = FALSE;
            DiagnosticWrite(log_file, "MISSING EXPORT "); DiagnosticWrite(log_file, exports[n]); DiagnosticWrite(log_file, "\r\n");
        }
    }
    DiagnosticWrite(log_file, all_exports ? "All ten exports resolved.\r\n" : "Export check failed.\r\n");
    create.raw = GetProcAddress(module, "Direct3DCreate9");
    if (!create.raw) ExitProcess(5);
    DiagnosticWrite(log_file, "Direct3DCreate9 BEGIN\r\n");
    factory = create.call(D3D_SDK_VERSION);
    DiagnosticHex(log_file, "Direct3DCreate9 END factory=", (uintptr_t)factory);
    if (!factory) ExitProcess(6);
    DiagnosticHex(log_file, "adapter_count=", IDirect3D9_GetAdapterCount(factory));
    DiagnosticHex(log_file, "factory_release_count=", IDirect3D9_Release(factory));
    DiagnosticWrite(log_file, all_exports ?
        "PROBE PASSED (DLL/export/factory only; no game or render hook validation).\r\n" :
        "Factory test passed; ancillary export check failed. No game or hook validation.\r\n");
    ExitProcess(all_exports ? 0 : 5);
}
