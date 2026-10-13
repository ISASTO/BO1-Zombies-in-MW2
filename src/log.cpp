#include "runtime.hpp"
#include <dbghelp.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include "native_hooks.hpp"
#include "script_parameters.hpp"

namespace mod {
namespace {
HANDLE logFile = INVALID_HANDLE_VALUE;
wchar_t directory[32768]{};
wchar_t logPath[32768]{};
std::mutex logMutex;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
LONG crashing = 0;
struct NativeEvent {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<const char*> operation{nullptr};
    std::atomic<DWORD> thread{0};
    std::atomic<ULONGLONG> tick{0};
    std::atomic<unsigned> entity{0};
    std::atomic<int> actors{-1};
};
std::array<NativeEvent,64> nativeEvents{};
std::atomic<std::uint64_t> nextNativeEvent{0};
struct DumpMemory {std::uintptr_t base=0;unsigned next=0;std::uintptr_t sharedPool=0;ULONG sharedBytes=0;};
BOOL CALLBACK IncludeNativeVmMemory(PVOID user,const PMINIDUMP_CALLBACK_INPUT input,PMINIDUMP_CALLBACK_OUTPUT output) {
    if(input->CallbackType!=MemoryCallback)return TRUE;
    auto& memory=*static_cast<DumpMemory*>(user);
    if(memory.next==0) {output->MemoryBase=memory.base+0x18f0000;output->MemorySize=0x20000;++memory.next;return TRUE;}
    if(memory.next==1) {output->MemoryBase=memory.base+0xd96e70;output->MemorySize=0x80;++memory.next;return TRUE;}
    if(memory.next==2) {output->MemoryBase=memory.base+kScriptStringPoolPointerRva;output->MemorySize=8;++memory.next;return TRUE;}
    if(memory.next==3 && memory.sharedBytes) {output->MemoryBase=memory.sharedPool;output->MemorySize=memory.sharedBytes;++memory.next;return TRUE;}
    return FALSE;
}

void RawWrite(HANDLE file, const char* text, DWORD length, bool durable=false) {
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, length, &written, nullptr);
    // Normal diagnostics use the OS cache. Forcing a physical disk flush for
    // every combat/input line stalls both the native and render threads.
    if (durable) FlushFileBuffers(file);
}
BOOL PreserveCurrentLog(const wchar_t* destination) {
    // Explicit sharing keeps copying compatible with the live writer handle.
    HANDLE source=CreateFileW(logPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(source==INVALID_HANDLE_VALUE)return FALSE;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(source,&size)) {const auto error=GetLastError();CloseHandle(source);SetLastError(error);return FALSE;}
    HANDLE copy=CreateFileW(destination,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(copy==INVALID_HANDLE_VALUE) {const auto error=GetLastError();CloseHandle(source);SetLastError(error);return FALSE;}
    char buffer[4096]{};DWORD error=0;
    auto remaining=size.QuadPart;
    while(remaining>0) {
        const auto amount=static_cast<DWORD>(std::min<LONGLONG>(remaining,sizeof(buffer)));DWORD received=0,written=0;
        if(!ReadFile(source,buffer,amount,&received,nullptr)) {error=GetLastError();break;}
        if(!received) {error=ERROR_HANDLE_EOF;break;}
        if(!WriteFile(copy,buffer,received,&written,nullptr)) {error=GetLastError();break;}
        if(written!=received) {error=ERROR_WRITE_FAULT;break;}
        remaining-=received;
    }
    FlushFileBuffers(copy);CloseHandle(copy);CloseHandle(source);SetLastError(error);return error?FALSE:TRUE;
}
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&crashing, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    if (logFile != INVALID_HANDLE_VALUE) FlushFileBuffers(logFile);
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t path[32768]{};
    // A later game launch overwrites latest.log. Preserve the log belonging to
    // this exception while its handle is still open, without taking logMutex.
    std::swprintf(path,32768,L"%ls\\crash-%04u%02u%02u-%02u%02u%02u.log",directory,
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    const BOOL logCopied=logPath[0]?PreserveCurrentLog(path):FALSE;
    const DWORD logCopyError=logCopied?0:GetLastError();
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
    if (size > 0) RawWrite(report, message, static_cast<DWORD>(size),true);
    size=std::snprintf(message,sizeof(message),"crash_log=%s error=%lu\r\n",logCopied?"preserved":"failed",logCopyError);
    if(size>0)RawWrite(report,message,static_cast<DWORD>(size));
    HMODULE faultModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &faultModule)) {
        wchar_t moduleName[32768]{};
        GetModuleFileNameW(faultModule, moduleName, 32768);
        size = std::snprintf(message, sizeof(message), "fault_module=%ls module_rva=0x%llx\r\n", moduleName,
            static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(faultModule)));
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size < 2048 ? size : 2047),true);
    }
    NativeScriptParameters parameters{};NativeScriptVmState vm{};int executeDepth=0,functionCount=0;std::uintptr_t frame=0;SIZE_T read=0;
    const auto process=GetCurrentProcess();
    if(ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptParametersRva),&parameters,sizeof(parameters),&read)
        && ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptExecuteDepthRva),&executeDepth,sizeof(executeDepth),&read)
        && ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptVmStateRva),&vm,sizeof(vm),&read)
        && ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptFunctionCountRva),&functionCount,sizeof(functionCount),&read)
        && ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptFramePointerRva),&frame,sizeof(frame),&read)) {
        size=std::snprintf(message,sizeof(message),"native_vm top=0x%llx incoming=%u outgoing=%u execute_depth=%d code=0x%llx stack=0x%llx start_top=0x%llx function_count=%d frame=0x%llx local_id=%u local_count=%u\r\n",
            static_cast<unsigned long long>(parameters.top),parameters.incoming,parameters.outgoing,executeDepth,
            static_cast<unsigned long long>(vm.code),static_cast<unsigned long long>(vm.top),static_cast<unsigned long long>(vm.startTop),
            functionCount,static_cast<unsigned long long>(frame),vm.localId,vm.localCount);
        if(size>0)RawWrite(report,message,static_cast<DWORD>(size));
    }
    const auto last=nextNativeEvent.load(std::memory_order_acquire);
    for(auto n=last>nativeEvents.size()?last-nativeEvents.size()+1:1;n<=last;++n) {
        const auto& event=nativeEvents[(n-1)%nativeEvents.size()];
        if(event.sequence.load(std::memory_order_acquire)!=n)continue;
        const auto operation=event.operation.load();const auto tick=event.tick.load();const auto thread=event.thread.load();
        const auto entity=event.entity.load();const auto actors=event.actors.load();
        if(!operation || event.sequence.load(std::memory_order_acquire)!=n)continue;
        size=std::snprintf(message,sizeof(message),"native_event=%llu tick=%llu thread=%lu operation=%s entity=%u actors=%d\r\n",
            static_cast<unsigned long long>(n),static_cast<unsigned long long>(tick),thread,operation,entity,actors);
        if(size>0)RawWrite(report,message,static_cast<DWORD>(size<2048?size:2047));
    }
    std::swprintf(path, 32768, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u.dmp", directory,
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    HANDLE dump = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        const auto type=static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithThreadInfo);
        DumpMemory memory{base,0};MEMORY_BASIC_INFORMATION poolRegion{};
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptStringPoolPointerRva),&memory.sharedPool,sizeof(memory.sharedPool),&read)
            && memory.sharedPool && VirtualQuery(reinterpret_cast<void*>(memory.sharedPool),&poolRegion,sizeof(poolRegion))
            && poolRegion.State==MEM_COMMIT && !(poolRegion.Protect&(PAGE_GUARD|PAGE_NOACCESS))) {
            const auto available=reinterpret_cast<std::uintptr_t>(poolRegion.BaseAddress)+poolRegion.RegionSize-memory.sharedPool;
            memory.sharedBytes=static_cast<ULONG>(std::min<std::uintptr_t>(available,0x100000));
        }
        MINIDUMP_CALLBACK_INFORMATION callbacks{IncludeNativeVmMemory,&memory};
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                                   &exception, nullptr, &callbacks);
        size = std::snprintf(message, sizeof(message), "minidump=%s error=%lu\r\n", ok ? "written" : "failed", ok ? 0 : GetLastError());
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size),true);
        CloseHandle(dump);
    }
    if (report != INVALID_HANDLE_VALUE) CloseHandle(report);
    if (previousFilter) return previousFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
const wchar_t* LogDirectory() { return directory; }
void RecordNativeOperation(const char* operation,unsigned entity,int actorCount) {
    // Only static operation names. No allocation, disk flush or game lock in
    // hot native paths; the exception handler formats the bounded ring later.
    const auto n=nextNativeEvent.fetch_add(1,std::memory_order_relaxed)+1;
    auto& event=nativeEvents[(n-1)%nativeEvents.size()];
    event.sequence.store(0,std::memory_order_release);
    event.operation.store(operation);event.thread.store(GetCurrentThreadId());event.tick.store(GetTickCount64());
    event.entity.store(entity);event.actors.store(actorCount);event.sequence.store(n,std::memory_order_release);
}
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
    std::swprintf(logPath,32768,L"%ls",file);
    Log("BO1MW2 %s | AMD64 | log=%ls", kVersion, file);
    Log("Test24: fixes actor-history displacements and bounds native shooting scratch scans to their 32-slot records. Live actor capacity stays 256; higher actors use current server poses during shots. Original MP hitmarkers retained.");
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
