#include "hang_watch.hpp"
#include "runtime.hpp"
#include "checked_memory.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace mod::hangwatch {
namespace {
std::atomic<std::uint64_t> serverFrames{0},renderFrames{0};
std::atomic<DWORD> mainThread{0},renderThread{0};
std::atomic<bool> armed{false},started{false};
std::uintptr_t exeBase=0,exeEnd=0,dllBase=0,dllEnd=0;

// ---- first-chance faults (record only) ----
struct Fault {
    std::atomic<std::uint64_t> sequence{0};
    DWORD code=0,thread=0;
    std::uint64_t rip=0,access=0,address=0;
    std::array<std::uint64_t,8> returns{};
    std::atomic<unsigned> repeats{0};
};
std::array<Fault,16> faults;
std::atomic<std::uint64_t> nextFault{0};
std::atomic<unsigned> droppedFaults{0};
std::atomic<std::uint64_t> printedFaults{0};
bool Interesting(DWORD code) {
    switch(code) {
        case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case 0xE06D7363u: return true;
        default: return false;
    }
}
bool Ours(std::uint64_t v) {return (v>=exeBase && v<exeEnd) || (v>=dllBase && v<dllEnd);}
LONG CALLBACK FaultRecorder(PEXCEPTION_POINTERS info) {
    const auto* record=info->ExceptionRecord;
    if(!Interesting(record->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    const auto rip=reinterpret_cast<std::uint64_t>(record->ExceptionAddress);
    const auto used=std::min<std::uint64_t>(nextFault.load(std::memory_order_acquire),faults.size());
    for(std::uint64_t n=0;n<used;++n)
        if(faults[n].sequence.load(std::memory_order_acquire)==n+1 && faults[n].rip==rip && faults[n].code==record->ExceptionCode) {
            faults[n].repeats.fetch_add(1,std::memory_order_relaxed);return EXCEPTION_CONTINUE_SEARCH;
        }
    const auto n=nextFault.fetch_add(1,std::memory_order_acq_rel)+1;
    if(n>faults.size()) {droppedFaults.fetch_add(1,std::memory_order_relaxed);return EXCEPTION_CONTINUE_SEARCH;}
    auto& f=faults[n-1];
    f.code=record->ExceptionCode;f.thread=GetCurrentThreadId();f.rip=rip;
    if((f.code==EXCEPTION_ACCESS_VIOLATION || f.code==EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters>=2) {
        f.access=record->ExceptionInformation[0];f.address=record->ExceptionInformation[1];
    }
    if(f.code!=EXCEPTION_STACK_OVERFLOW) {
        // Stay inside the committed region holding the stack pointer.
        MEMORY_BASIC_INFORMATION region{};std::uint64_t top=0;
        if(VirtualQuery(reinterpret_cast<void*>(info->ContextRecord->Rsp),&region,sizeof(region)) && region.State==MEM_COMMIT)
            top=reinterpret_cast<std::uint64_t>(region.BaseAddress)+region.RegionSize;
        const auto* stack=reinterpret_cast<const std::uint64_t*>(info->ContextRecord->Rsp);
        unsigned found=0;
        for(unsigned i=0;i<256 && found<f.returns.size() && reinterpret_cast<std::uint64_t>(stack+i+1)<=top;++i)
            if(Ours(stack[i])) f.returns[found++]=stack[i];
    }
    f.sequence.store(n,std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}
std::string Where(std::uint64_t v) {
    char text[48];
    if(v>=exeBase && v<exeEnd) std::snprintf(text,sizeof(text),"iw4sp+0x%llx",static_cast<unsigned long long>(v-exeBase));
    else if(v>=dllBase && v<dllEnd) std::snprintf(text,sizeof(text),"d3d9+0x%llx",static_cast<unsigned long long>(v-dllBase));
    else std::snprintf(text,sizeof(text),"0x%llx",static_cast<unsigned long long>(v));
    return text;
}
std::string FaultText(const Fault& f,std::uint64_t n) {
    std::string returns;
    for(const auto r:f.returns) if(r) {if(!returns.empty())returns+=',';returns+=Where(r);}
    char line[512];
    std::snprintf(line,sizeof(line),"FIRST_CHANCE n=%llu code=0x%08lx thread=%lu at=%s access=%llu address=0x%llx repeats=%u returns=%s",
        static_cast<unsigned long long>(n),f.code,f.thread,Where(f.rip).c_str(),static_cast<unsigned long long>(f.access),
        static_cast<unsigned long long>(f.address),f.repeats.load(),returns.empty()?"-":returns.c_str());
    return line;
}
void PrintFaults(bool urgent) {
    const auto last=std::min<std::uint64_t>(nextFault.load(std::memory_order_acquire),faults.size());
    auto from=printedFaults.load();
    for(auto n=from+1;n<=last;++n) {
        const auto& f=faults[n-1];
        if(f.sequence.load(std::memory_order_acquire)!=n) break;
        if(urgent) LogUrgent("%s",FaultText(f,n).c_str()); else Log("%s",FaultText(f,n).c_str());
        printedFaults.store(n);
    }
}

// ---- the watchdog ----
struct WindowScan {std::string text;std::vector<HWND> dialogs;};
std::string Utf8(const wchar_t* w) {
    char out[512]{};WideCharToMultiByte(CP_UTF8,0,w,-1,out,sizeof(out)-1,nullptr,nullptr);
    for(char* c=out;*c;++c) if(*c=='\r' || *c=='\n') *c=' ';
    return out;
}
BOOL CALLBACK ChildText(HWND child,LPARAM user) {
    // InternalGetWindowText never sends a message to the (possibly stalled) owner.
    wchar_t text[512]{};
    if(InternalGetWindowText(child,text,512)>0) {
        auto& scan=*reinterpret_cast<WindowScan*>(user);
        scan.text+="HANG_DIALOG_TEXT "+Utf8(text)+"\r\n";
    }
    return TRUE;
}
BOOL CALLBACK TopWindow(HWND window,LPARAM user) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    if(process!=GetCurrentProcessId()) return TRUE;
    auto& scan=*reinterpret_cast<WindowScan*>(user);
    wchar_t cls[128]{},title[256]{};
    GetClassNameW(window,cls,128);InternalGetWindowText(window,title,256);
    const bool visible=IsWindowVisible(window)!=0;
    char line[700];
    std::snprintf(line,sizeof(line),"HANG_WINDOW class=%s visible=%d iconic=%d title=%s\r\n",Utf8(cls).c_str(),visible,IsIconic(window)!=0,Utf8(title).c_str());
    scan.text+=line;
    if(visible && !std::wcscmp(cls,L"#32770")) {scan.dialogs.push_back(window);EnumChildWindows(window,ChildText,user);}
    return TRUE;
}
bool ForegroundIsOurs() {
    DWORD process=0;const HWND w=GetForegroundWindow();
    if(!w) return false;
    GetWindowThreadProcessId(w,&process);
    return process==GetCurrentProcessId();
}
// Suspend, read the instruction pointer and return-address candidates from
// the stack, resume. Nothing between SuspendThread and ResumeThread may take
// a lock the suspended thread could hold: no heap, no logging; the stack is
// copied with kernel-checked reads into fixed arrays and formatted afterwards.
std::string Sample(DWORD id,const char* label,CONTEXT* keep) {
    if(!id) return {};
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,id);
    char line[160];
    if(!thread) {std::snprintf(line,sizeof(line),"HANG_THREAD %s=%lu open_failed=%lu\r\n",label,id,GetLastError());return line;}
    std::array<std::uint64_t,5> rips{};unsigned ripCount=0;
    std::array<std::uint64_t,32> returns{};unsigned found=0;
    std::uint64_t rsp=0,rbp=0;
    for(int round=0;round<5;++round) {
        if(SuspendThread(thread)==static_cast<DWORD>(-1)) break;
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
        if(GetThreadContext(thread,&context)) {
            rips[ripCount++]=context.Rip;
            if(round==0) {
                if(keep) *keep=context;
                rsp=context.Rsp;rbp=context.Rbp;
                std::uint64_t chunk[64];
                for(std::uint64_t at=rsp;found<returns.size() && at<rsp+0x10000;at+=sizeof(chunk)) {
                    if(!CheckedReadMemory(at,chunk,sizeof(chunk))) break;
                    for(const auto v:chunk) if(Ours(v) && found<returns.size()) returns[found++]=v;
                }
            }
        }
        ResumeThread(thread);
        Sleep(100);
    }
    CloseHandle(thread);
    std::string text;
    std::snprintf(line,sizeof(line),"HANG_THREAD %s=%lu rsp=0x%llx rbp=0x%llx returns=",label,id,
        static_cast<unsigned long long>(rsp),static_cast<unsigned long long>(rbp));
    text+=line;
    for(unsigned n=0;n<found;++n) {if(n)text+=',';text+=Where(returns[n]);}
    if(!found) text+='-';
    text+="\r\nHANG_THREAD_RIPS "+std::string(label)+" ";
    for(unsigned n=0;n<ripCount;++n) {if(n)text+=',';text+=Where(rips[n]);}
    text+="\r\n";
    return text;
}
void Report(unsigned long long stalled) {
    LogUrgent("HANG_DETECTED stalled_ms=%llu server_frames=%llu render_frames=%llu main_thread=%lu render_thread=%lu",
        stalled,static_cast<unsigned long long>(serverFrames.load()),static_cast<unsigned long long>(renderFrames.load()),
        mainThread.load(),renderThread.load());
    PrintFaults(true);
    std::string extra;
    for(std::uint64_t n=1;n<=std::min<std::uint64_t>(nextFault.load(),faults.size());++n)
        if(faults[n-1].sequence.load()==n) extra+=FaultText(faults[n-1],n)+"\r\n";
    WindowScan scan;EnumWindows(TopWindow,reinterpret_cast<LPARAM>(&scan));
    extra+=scan.text;
    CONTEXT main{};
    extra+=Sample(mainThread.load(),"main",&main);
    if(renderThread.load()!=mainThread.load()) extra+=Sample(renderThread.load(),"render",nullptr);
    // One line at a time into the session log as well.
    for(std::size_t at=0;at<extra.size();) {
        auto end=extra.find("\r\n",at);if(end==std::string::npos)end=extra.size();
        if(end>at) LogUrgent("%s",extra.substr(at,end-at).c_str());
        at=end+2;
    }
    WriteHangDiagnostics(main.Rip?&main:nullptr,mainThread.load(),extra.c_str());
    LogUrgent("HANG_REPORT written=1 (bo1mw2\\logs\\hang-*.txt and .dmp)");
    // A fatal error box hidden behind the fullscreen game: bring it forward
    // (asynchronous, so a stalled owner thread cannot block the watchdog).
    for(const auto dialog:scan.dialogs) {
        SetWindowPos(dialog,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW|SWP_ASYNCWINDOWPOS);
        SetForegroundWindow(dialog);
        LogUrgent("HANG_DIALOG raised=1");
    }
}
// Test45: the main thread waits inside the graphics driver, called through
// this DLL's Direct3D proxy (a device reset after alt-tab, a slow present).
// Test44's two "hangs" were this: the game went on afterwards.
bool DriverWait(DWORD id,std::uint64_t& rip) {
    rip=0;
    if(!id) return false;
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,id);
    if(!thread) return false;
    std::array<std::uint64_t,64> stack{};bool read=false;
    if(SuspendThread(thread)!=static_cast<DWORD>(-1)) {
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
        if(GetThreadContext(thread,&context)) {rip=context.Rip;read=CheckedReadMemory(context.Rsp,stack.data(),sizeof(stack));}
        ResumeThread(thread);
    }
    CloseHandle(thread);
    if(!read || !rip || Ours(rip)) return false;
    unsigned ours=0;
    for(const auto v:stack) {
        if(!Ours(v)) continue;
        if(v>=dllBase && v<dllEnd) return true;      // our d3d9 proxy among the first frames of the game
        if(++ours>=4) break;
    }
    return false;
}
DWORD WINAPI Watch(void*) {
    std::uint64_t lastServer=0,lastRender=0;
    ULONGLONG progress=GetTickCount64();
    unsigned reports=0,driverWaits=0;bool reported=false,driverNoted=false;
    for(;;) {
        Sleep(250);
        PrintFaults(true);
        const auto s=serverFrames.load(std::memory_order_relaxed),r=renderFrames.load(std::memory_order_relaxed);
        const auto now=GetTickCount64();
        if(s!=lastServer || r!=lastRender) {
            if(driverNoted) Log("HANG_DRIVER_WAIT ended stalled_ms=%llu",static_cast<unsigned long long>(now-progress));
            lastServer=s;lastRender=r;progress=now;reported=false;driverNoted=false;continue;
        }
        if(!armed.load() || reported || reports>=kMaxReports || now-progress<kHangMilliseconds || !ForegroundIsOurs()) continue;
        std::uint64_t rip=0;
        if(now-progress<kDriverWaitMilliseconds && DriverWait(mainThread.load(),rip)) {
            if(!driverNoted && driverWaits<16) {
                ++driverWaits;driverNoted=true;
                Log("HANG_DRIVER_WAIT stalled_ms=%llu at=%s (inside the graphics driver through the Direct3D proxy; reported only after %llu ms)",
                    static_cast<unsigned long long>(now-progress),Where(rip).c_str(),static_cast<unsigned long long>(kDriverWaitMilliseconds));
            }
            continue;
        }
        reported=true;++reports;
        Report(now-progress);
    }
}
} // namespace

void Start() {
    if(started.exchange(true)) return;
    exeBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
    exeEnd=exeBase+reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase+dos->e_lfanew)->OptionalHeader.SizeOfImage;
    HMODULE self=nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Start),&self)) {
        dllBase=reinterpret_cast<std::uintptr_t>(self);
        const auto* d=reinterpret_cast<const IMAGE_DOS_HEADER*>(dllBase);
        dllEnd=dllBase+reinterpret_cast<const IMAGE_NT_HEADERS64*>(dllBase+d->e_lfanew)->OptionalHeader.SizeOfImage;
    }
    const bool recorder=AddVectoredExceptionHandler(0,FaultRecorder)!=nullptr;
    HANDLE thread=CreateThread(nullptr,0,Watch,nullptr,0,nullptr);
    if(thread) CloseHandle(thread);
    Log("HANG_WATCH started=%d fault_recorder=%d hang_ms=%llu (records faults, never handles them)",thread!=nullptr,recorder,kHangMilliseconds);
}
void ServerFrame() {
    serverFrames.fetch_add(1,std::memory_order_relaxed);
    if(!mainThread.load(std::memory_order_relaxed)) mainThread.store(GetCurrentThreadId());
}
void RenderFrame() {
    renderFrames.fetch_add(1,std::memory_order_relaxed);
    if(!renderThread.load(std::memory_order_relaxed)) renderThread.store(GetCurrentThreadId());
}
void SetArmed(bool zombiesRunning) {armed.store(zombiesRunning,std::memory_order_relaxed);}
void LogFaults() {PrintFaults(false);}
} // namespace mod::hangwatch
