#include "runtime.hpp"
#include <dbghelp.h>
#include <algorithm>
#include <cstdarg>
#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include "native_hooks.hpp"
#include "script_parameters.hpp"
#include "crowd_resources.hpp"
#include "script_vm.hpp"

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
// Exact supported Com_Error formats a 0x1000-byte message at this address
// before shutdown. Preserve it if shutdown itself faults, hiding the cause.
constexpr std::uintptr_t kEngineErrorDepthRva=0x1326134,kEngineErrorMessageRva=0x13261a0;
constexpr std::size_t kEngineErrorMessageBytes=0x1000;
struct DumpRange {std::uintptr_t at=0;ULONG bytes=0;};
struct DumpMemory {
    std::array<DumpRange,48> ranges{};unsigned count=0,next=0;
    void Add(std::uintptr_t at,std::size_t bytes) {
        if(!at || !bytes || count==ranges.size())return;
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)) ||
           region.State!=MEM_COMMIT || region.Protect&(PAGE_GUARD|PAGE_NOACCESS))return;
        const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(at<begin || at-begin>=region.RegionSize)return;
        const auto available=region.RegionSize-(at-begin);
        ranges[count++]={at,static_cast<ULONG>(std::min<std::size_t>(bytes,available))};
    }
};
BOOL CALLBACK IncludeNativeVmMemory(PVOID user,const PMINIDUMP_CALLBACK_INPUT input,PMINIDUMP_CALLBACK_OUTPUT output) {
    if(input->CallbackType!=MemoryCallback)return TRUE;
    auto& memory=*static_cast<DumpMemory*>(user);
    if(memory.next==memory.count)return FALSE;
    const auto& range=memory.ranges[memory.next++];
    output->MemoryBase=range.at;output->MemorySize=range.bytes;return TRUE;
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
// Shared by the crash filter and the Test41 hang watchdog: preserve the log,
// write <kind>-<time>.txt with the engine error, VM state and native event
// ring, and <kind>-<time>.dmp. For a hang `info` is null and `hung` is the
// stalled thread's context (captured while it was suspended).
void WriteDiagnostics(const wchar_t* kind,EXCEPTION_POINTERS* info,const CONTEXT* hung,DWORD hungThread,const char* extra) {
    if (logFile != INVALID_HANDLE_VALUE) FlushFileBuffers(logFile);
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t path[32768]{};
    // A later game launch overwrites latest.log. Preserve the log belonging to
    // this exception while its handle is still open, without taking logMutex.
    std::swprintf(path,32768,L"%ls\\%ls-%04u%02u%02u-%02u%02u%02u.log",directory,kind,
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    const BOOL logCopied=logPath[0]?PreserveCurrentLog(path):FALSE;
    const DWORD logCopyError=logCopied?0:GetLastError();
    std::swprintf(path, 32768, L"%ls\\%ls-%04u%02u%02u-%02u%02u%02u.txt", directory, kind,
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    HANDLE report = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    char message[2048]{};
    const CONTEXT* registers=info?info->ContextRecord:hung;
    const auto address = info?reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress):(hung?hung->Rip:0);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    int size = info?std::snprintf(message, sizeof(message),
        "BO1MW2 %s\r\nexception=0x%08lx address=0x%llx engine_base=0x%llx engine_relative=0x%llx\r\n"
        "thread=%lu rip=0x%llx rsp=0x%llx\r\n",
        kVersion, info->ExceptionRecord->ExceptionCode, static_cast<unsigned long long>(address),
        static_cast<unsigned long long>(base), static_cast<unsigned long long>(address - base),
        GetCurrentThreadId(), static_cast<unsigned long long>(info->ContextRecord->Rip),
        static_cast<unsigned long long>(info->ContextRecord->Rsp))
        :std::snprintf(message,sizeof(message),"BO1MW2 %s\r\nhang thread=%lu rip=0x%llx rsp=0x%llx engine_base=0x%llx engine_relative=0x%llx\r\n",
        kVersion,hungThread,static_cast<unsigned long long>(address),static_cast<unsigned long long>(hung?hung->Rsp:0),
        static_cast<unsigned long long>(base),static_cast<unsigned long long>(address>=base?address-base:0));
    if (size > 0) RawWrite(report, message, static_cast<DWORD>(size),true);
    size=std::snprintf(message,sizeof(message),"crash_log=%s error=%lu\r\n",logCopied?"preserved":"failed",logCopyError);
    if(size>0)RawWrite(report,message,static_cast<DWORD>(size));
    if(extra)RawWrite(report,extra,static_cast<DWORD>(std::strlen(extra)));
    HMODULE faultModule = nullptr;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &faultModule)) {
        wchar_t moduleName[32768]{};
        GetModuleFileNameW(faultModule, moduleName, 32768);
        size = std::snprintf(message, sizeof(message), "fault_module=%ls module_rva=0x%llx\r\n", moduleName,
            static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(faultModule)));
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size < 2048 ? size : 2047),true);
    }
    NativeScriptParameters parameters{};NativeScriptVmState vm{};int executeDepth=0,functionCount=0;std::uintptr_t frame=0;SIZE_T read=0;
    const auto process=GetCurrentProcess();
    int nativeErrorDepth=0;
    if(ReadProcessMemory(process,reinterpret_cast<void*>(base+kEngineErrorDepthRva),&nativeErrorDepth,sizeof(nativeErrorDepth),&read)
        && nativeErrorDepth>0) {
        char engineError[kEngineErrorMessageBytes]{};
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+kEngineErrorMessageRva),engineError,sizeof(engineError),&read)) {
            engineError[sizeof(engineError)-1]=0;
            size=std::snprintf(message,sizeof(message),"native_engine_error_depth=%d message=",nativeErrorDepth);
            if(size>0)RawWrite(report,message,static_cast<DWORD>(size));
            RawWrite(report,engineError,static_cast<DWORD>(std::strlen(engineError)));
            RawWrite(report,"\r\n",2);
        }
    }
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
    if(const auto vm=ReadScriptVmUsage();vm.valid) {
        size=std::snprintf(message,sizeof(message),"script_vm children=%u/%u peak_pool=%u/%u parents=%u/%u string_nodes=%u/%u pointer_heap=%llu entities=%u/%u dobj_transfers=%u/%u entity_handles=%u/%u model_lighting=%u,%u/%u\r\n",
            vm.childTotal,vm.childCapacity,vm.childPeakPool,vm.poolCapacity,vm.parents,vm.parentCapacity,vm.mtNativeUsedNodes,vm.mtNativeNodes,
            static_cast<unsigned long long>(vm.heapUsed),vm.entitiesInUse,vm.entityCapacity,vm.dobjTransfers,vm.dobjCapacity,
            vm.entityHandles,vm.entityHandleCapacity,vm.modelLighting[0],vm.modelLighting[1],vm.modelLightingCapacity);
        if(size>0)RawWrite(report,message,static_cast<DWORD>(size<2048?size:2047));
    }
    {
        char watch[2048];const int used=FormatStaticWatchHits(watch,sizeof(watch));
        if(used>0)RawWrite(report,watch,static_cast<DWORD>(used));
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
        DumpMemory memory;
        memory.Add(base+0x18f0000,0x20000);memory.Add(base+0xd96e70,0x80);
        memory.Add(base+kScriptStringPoolPointerRva,8);memory.Add(base+0x1326100,0x1100);
        memory.Add(base+0xcdb118,0xa8);memory.Add(base+0x548cf0,8);
        memory.Add(base+0x19c386c,8);memory.Add(base+0x19ada30,16);
        memory.Add(base+0x410fd00,8);memory.Add(base+0x4116588,48);
        for(unsigned n=0;n<2;++n)
            memory.Add(base+expansion::kRenderFrameBaseRva+n*expansion::kRenderFrameStride+0x41990,0x60);
        const auto crowdAddress=ExpandedResourceStateAddress();
        memory.Add(crowdAddress,expansion::kCrowdStateBytes);
        expansion::CrowdResourceState crowd;
        if(crowdAddress && ReadProcessMemory(process,reinterpret_cast<void*>(crowdAddress),&crowd,sizeof(crowd),&read)) {
            // Retain packed-ID mappings and their records only on a crash.
            // Corrupt counters cannot make the capture exceed owned bounds.
            for(const auto& frame:crowd.frames) {
                const auto used=std::min<std::size_t>(frame.used,expansion::kCrowdSurfaceBytes);
                memory.Add(frame.table,expansion::kSurfaceTableBytes);
                memory.Add(frame.reverse,(used/4)*sizeof(std::uint16_t));
                memory.Add(frame.surfaces,used);
            }
            if(crowd.queryClaim==2) {
                std::uintptr_t reference=0;std::memcpy(&reference,crowd.query.data()+0x90,8);
                memory.Add(reference,16);
                if(reference>=0x70)memory.Add(reference-0x70,0x100);
            }
        }
        std::uintptr_t sharedPool=0;
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+kScriptStringPoolPointerRva),&sharedPool,sizeof(sharedPool),&read))
            memory.Add(sharedPool,0x100000);
        // Capture the missing glass ownership/free-link evidence, only on a
        // crash. The normal game loop does no additional copying or disk I/O.
        std::array<std::uint8_t,0xa8> glass{};
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+0xcdb118),glass.data(),glass.size(),&read)) {
            unsigned pieces=0;std::memcpy(&pieces,glass.data()+0xc,4);
            if(pieces && pieces<=0xffff)for(const auto& field:std::array<std::pair<unsigned,std::size_t>,3>{{{0x38,pieces*32ull},{0x40,pieces*32ull},{0x58,((pieces+31ull)/32)*4}}}) {
                std::uintptr_t pointer=0;std::memcpy(&pointer,glass.data()+field.first,8);
                memory.Add(pointer,std::min<std::size_t>(field.second,0x100000));
            }
        }
        // Test32: client per-place glass states and the server place records
        // that drive shatter requests, plus image data any faulting register
        // references (a corrupted static command entry was not captured).
        memory.Add(base+0x3fef50,0x1400);
        std::uintptr_t serverGlass=0;
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+0xcf75a8),&serverGlass,sizeof(serverGlass),&read) && serverGlass) {
            memory.Add(serverGlass,0x40);
            std::uintptr_t records=0;unsigned places=0;
            if(ReadProcessMemory(process,reinterpret_cast<void*>(serverGlass),&records,sizeof(records),&read)
               && ReadProcessMemory(process,reinterpret_cast<void*>(serverGlass+8),&places,sizeof(places),&read)
               && places && places<=0x400)
                memory.Add(records,places*12ull+0x40);
        }
        const CONTEXT empty{};
        const auto* context=registers?registers:&empty;
        for(const auto value:{context->Rax,context->Rbx,context->Rcx,context->Rdx,context->Rsi,context->Rdi,context->Rbp,
                              context->R8,context->R9,context->R10,context->R11,context->R12,context->R13,context->R14,context->R15}) {
            if(value>=base+0x3bc000 && value<base+0x4000000)memory.Add((value&~0xffull)-0x100,0x300);
        }
        MINIDUMP_CALLBACK_INFORMATION callbacks{IncludeNativeVmMemory,&memory};
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                                   info?&exception:nullptr, nullptr, &callbacks);
        size = std::snprintf(message, sizeof(message), "minidump=%s error=%lu\r\n", ok ? "written" : "failed", ok ? 0 : GetLastError());
        if (size > 0) RawWrite(report, message, static_cast<DWORD>(size),true);
        CloseHandle(dump);
    }
    if (report != INVALID_HANDLE_VALUE) CloseHandle(report);
}
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&crashing, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    WriteDiagnostics(L"crash",info,nullptr,0,nullptr);
    if (previousFilter) return previousFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void WriteHangDiagnostics(const CONTEXT* context,DWORD thread,const char* extra) {
    // A crash being written takes precedence; never two reports at once.
    if(InterlockedCompareExchange(&crashing,1,0)!=0)return;
    WriteDiagnostics(L"hang",nullptr,context,thread,extra);
    InterlockedExchange(&crashing,0);
}
void LogUrgent(const char* format,...) {
    // The watchdog's log: never blocks behind a stalled thread holding logMutex.
    char body[4096]{};
    va_list args; va_start(args, format);
    std::vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    SYSTEMTIME time{}; GetLocalTime(&time);
    char line[4300]{};
    std::snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [t%lu] %s\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId(), body);
    std::unique_lock<std::mutex> lock(logMutex,std::defer_lock);
    for(int i=0;i<50 && !lock.try_lock();++i)Sleep(10);
    RawWrite(logFile, line, static_cast<DWORD>(std::strlen(line)),true);
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
// Keep the previous runs: an engine error dialog is not a crash, so the
// next launch used to overwrite the only log of the failing run.
void RotateLogs(const wchar_t* folder) {
    wchar_t latest[4096]{};std::swprintf(latest,4096,L"%ls\\latest.log",folder);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(GetFileAttributesExW(latest,GetFileExInfoStandard,&info)) {
        FILETIME local{};SYSTEMTIME t{};
        FileTimeToLocalFileTime(&info.ftLastWriteTime,&local);FileTimeToSystemTime(&local,&t);
        wchar_t kept[4096]{};
        std::swprintf(kept,4096,L"%ls\\session-%04u%02u%02u-%02u%02u%02u.log",folder,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        MoveFileExW(latest,kept,MOVEFILE_REPLACE_EXISTING);
    }
    std::vector<std::wstring> sessions;
    wchar_t pattern[4096]{};std::swprintf(pattern,4096,L"%ls\\session-*.log",folder);
    WIN32_FIND_DATAW found{};HANDLE search=FindFirstFileW(pattern,&found);
    if(search!=INVALID_HANDLE_VALUE) {
        do sessions.emplace_back(found.cFileName); while(FindNextFileW(search,&found));
        FindClose(search);
    }
    std::sort(sessions.begin(),sessions.end());          // names sort by time
    for(std::size_t n=0;n+8<sessions.size();++n) {
        wchar_t old[4096]{};std::swprintf(old,4096,L"%ls\\%ls",folder,sessions[n].c_str());DeleteFileW(old);
    }
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
    RotateLogs(directory);
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
    Log("Test29: coordinates 72 MiB native CPU/GPU skin caches, bounded atomic reservations and independent guarded 2 MiB skinned scene records per frame slot. Captures renderer overflows and rejects non-finite glass poses/queries and invalid triangle feature selectors. First corrupting physics update remains unproven; native 256/274 capacity and MP hitmarkers are retained.");
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
