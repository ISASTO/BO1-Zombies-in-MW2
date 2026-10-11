#include "runtime.hpp"
#include <dbghelp.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>

namespace mod {
namespace {
HANDLE logFile = INVALID_HANDLE_VALUE;
wchar_t directory[32768]{};
std::mutex logMutex;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
LONG crashing = 0;

void RawWrite(HANDLE file, const char* text, DWORD length) {
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, length, &written, nullptr);
    FlushFileBuffers(file);
}
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&crashing, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t path[32768]{};
    std::swprintf(path, 32768, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u.txt", directory,
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    HANDLE report = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    char message[2048]{};
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    int size = std::snprintf(message, sizeof(message),
        "BO1MW2 %s\r\nexception=0x%08lx address=0x%llx engine_base=0x%llx engine_relative=0x%llx\r\n"
        "thread=%lu rip=0x%llx rsp=0x%llx\r\n",
        kVersion, info->ExceptionRecord->ExceptionCode, static_cast<unsigned long long>(address),
        static_cast<unsigned long long>(base), static_cast<unsigned long long>(address - base),
        GetCurrentThreadId(), static_cast<unsigned long long>(info->ContextRecord->Rip),
        static_cast<unsigned long long>(info->ContextRecord->Rsp));
    if (size > 0) RawWrite(report, message, static_cast<DWORD>(size));
    HMODULE faultModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &faultModule)) {
        wchar_t moduleName[32768]{};
        GetModuleFileNameW(faultModule, moduleName, 32768);
        size = std::snprintf(message, sizeof(message), "fault_module=%ls module_rva=0x%llx\r\n", moduleName,
            static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(faultModule)));
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size < 2048 ? size : 2047));
    }
    std::swprintf(path, 32768, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u.dmp", directory,
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    HANDLE dump = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal,
                                   &exception, nullptr, nullptr);
        size = std::snprintf(message, sizeof(message), "minidump=%s error=%lu\r\n", ok ? "written" : "failed", ok ? 0 : GetLastError());
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size));
        CloseHandle(dump);
    }
    if (report != INVALID_HANDLE_VALUE) CloseHandle(report);
    if (previousFilter) return previousFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
const wchar_t* LogDirectory() { return directory; }
void InitializeLog() {
    wchar_t game[32768]{};
    GetModuleFileNameW(nullptr, game, 32768);
    wchar_t* end = std::wcsrchr(game, L'\\');
    if (end) *end = 0;
    std::swprintf(directory, 32768, L"%ls\\bo1mw2", game);
    CreateDirectoryW(directory, nullptr);
    std::swprintf(directory, 32768, L"%ls\\bo1mw2\\logs", game);
    CreateDirectoryW(directory, nullptr);
    wchar_t file[32768]{};
    std::swprintf(file, 32768, L"%ls\\latest.log", directory);
    logFile = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        wchar_t local[32768]{};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) {
            std::swprintf(directory, 32768, L"%ls\\BO1MW2", local);
            CreateDirectoryW(directory, nullptr);
            std::swprintf(file, 32768, L"%ls\\latest.log", directory);
            logFile = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
        }
    }
    Log("BO1MW2 %s | AMD64 | log=%ls", kVersion, file);
    Log("PLAYER test 7: session God reconciliation, reserve ammo, no reload, noclip, speed, compatible jump and compact menu. Saved FOV retained.");
}
void Log(const char* format, ...) {
    char body[4096]{};
    va_list args; va_start(args, format);
    std::vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    SYSTEMTIME time{}; GetLocalTime(&time);
    char line[4300]{};
    std::snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [t%lu] %s\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId(), body);
    std::lock_guard<std::mutex> lock(logMutex);
    RawWrite(logFile, line, static_cast<DWORD>(std::strlen(line)));
    OutputDebugStringA(line);
}
void InstallCrashHandler() {
    previousFilter = SetUnhandledExceptionFilter(CrashFilter);
    Log("Unhandled exception handler installed; existing filter preserved. No first-chance interception.");
}
} // namespace mod
