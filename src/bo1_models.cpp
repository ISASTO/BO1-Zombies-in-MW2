#include "bo1_models.hpp"
#include "bo1_assets.hpp"
#include "bo1_model_build.hpp"
#include "runtime.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace mod::bo1models {
namespace {
// Bump when the generated zone changes so cached files are rebuilt.
constexpr const char* kBuilderVersion="bo1mw2-models-1";
std::mutex mutex;
std::condition_variable changed;
Status status;
bool started=false;
// Crash guard (see bo1_models.hpp). markerPath is written once in StartBuild,
// before the game can load a level; the atomics are read at process exit.
std::wstring markerPath;
std::atomic<bool> markerOwned{false},loadPending{false};
bool staleMarker=false,unfinished=false,confirmed=false;     // under mutex

struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
bool Stat(const std::wstring& path,std::uint64_t& size,std::uint64_t& time) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a) || (a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    size=(static_cast<std::uint64_t>(a.nFileSizeHigh)<<32)|a.nFileSizeLow;
    time=(static_cast<std::uint64_t>(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime;
    return true;
}
bool ReadSmall(const std::wstring& path,std::string& out) {
    File f;f.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f.handle==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(f.handle,&size) || size.QuadPart>1<<20) return false;
    out.assign(static_cast<std::size_t>(size.QuadPart),'\0');
    DWORD read=0;
    return !size.QuadPart || (::ReadFile(f.handle,out.data(),static_cast<DWORD>(out.size()),&read,nullptr) && read==out.size());
}
bool WriteAll(const std::wstring& path,const void* data,std::size_t size) {
    File f;f.handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.handle==INVALID_HANDLE_VALUE) return false;
    const auto* p=static_cast<const std::uint8_t*>(data);
    while(size) {
        DWORD wrote=0;const auto chunk=static_cast<DWORD>(std::min<std::size_t>(size,1u<<24));
        if(!::WriteFile(f.handle,p,chunk,&wrote,nullptr) || !wrote) return false;
        p+=wrote;size-=wrote;
    }
    return FlushFileBuffers(f.handle)!=0;
}
// Write to a temporary name, then replace: a crash never leaves half a zone.
bool Replace(const std::wstring& path,const void* data,std::size_t size) {
    const auto temporary=path+L".tmp";
    if(!WriteAll(temporary,data,size)) {DeleteFileW(temporary.c_str());return false;}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temporary.c_str());return false;}
    return true;
}
std::vector<std::wstring> Iwds(const std::wstring& folder) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW d{};
    HANDLE find=FindFirstFileW((folder+L"\\main\\*.iwd").c_str(),&d);
    if(find==INVALID_HANDLE_VALUE) return out;
    do {if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) out.push_back(folder+L"\\main\\"+d.cFileName);} while(FindNextFileW(find,&d));
    FindClose(find);
    std::sort(out.begin(),out.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    return out;
}
std::string Manifest(const std::wstring& theater,const std::vector<std::wstring>& iwds) {
    std::string m=std::string(kBuilderVersion)+"\n";
    auto line=[&](const std::wstring& path) {
        std::uint64_t size=0,time=0;Stat(path,size,time);
        char buffer[96];std::snprintf(buffer,sizeof(buffer)," %llu %llu\n",static_cast<unsigned long long>(size),static_cast<unsigned long long>(time));
        m+=bo1assets::Utf8(path)+buffer;
    };
    line(theater);for(const auto& i:iwds) line(i);
    return m;
}
bool FastFileHeaderOk(const std::wstring& path) {
    std::string head;
    File f;f.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f.handle==INVALID_HANDLE_VALUE) return false;
    std::uint8_t h[24]{};DWORD read=0;
    if(!::ReadFile(f.handle,h,sizeof(h),&read,nullptr) || read!=sizeof(h)) return false;
    return !std::memcmp(h,"IWffu100",8) && bo1model::Get<std::uint32_t>(h+8)==bo1model::mw2::kVersion && h[21]==0x78;
}
// An open .iwd for the pipeline's reads (offset reads on a synchronous handle).
struct OpenArchive {
    std::unique_ptr<File> file;
    bo1model::Archive archive;
};
bool Open(const std::wstring& path,OpenArchive& out) {
    out.file=std::make_unique<File>();
    out.file->handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_RANDOM_ACCESS,nullptr);
    LARGE_INTEGER size{};
    if(out.file->handle==INVALID_HANDLE_VALUE || !GetFileSizeEx(out.file->handle,&size)) return false;
    const HANDLE h=out.file->handle;
    out.archive.name=bo1assets::Utf8(path);out.archive.size=static_cast<std::uint64_t>(size.QuadPart);
    out.archive.read=[h](std::uint64_t at,void* buffer,std::size_t n) {
        auto* p=static_cast<std::uint8_t*>(buffer);
        while(n) {
            OVERLAPPED o{};o.Offset=static_cast<DWORD>(at);o.OffsetHigh=static_cast<DWORD>(at>>32);
            DWORD got=0;const auto chunk=static_cast<DWORD>(std::min<std::size_t>(n,1u<<24));
            if(!::ReadFile(h,p,chunk,&got,&o) || !got) return false;
            p+=got;n-=got;at+=got;
        }
        return true;
    };
    return true;
}
void Build() {
    const auto begin=std::chrono::steady_clock::now();
    Status result;result.state=State::Failed;
    const auto folder=bo1assets::BlackOpsFolder();
    const auto cacheDir=bo1assets::GameDirectory()+L"bo1mw2\\cache";
    const auto zonePath=cacheDir+L"\\bo1_zombie_models.ff",manifestPath=cacheDir+L"\\bo1_zombie_models.txt";
    result.file=bo1assets::Utf8(zonePath);
    std::string error;
    [&]{
        if(folder.empty()) {result.state=State::Missing;result.detail="Black Ops folder not found (set bo1mw2\\bo1_path.txt)";return;}
        const auto theater=folder+L"\\zone\\Common\\zombie_theater.ff";
        std::uint64_t size=0,time=0;
        if(!Stat(theater,size,time)) {result.state=State::Missing;result.detail="zone\\Common\\zombie_theater.ff not found";return;}
        const auto iwds=Iwds(folder);
        if(iwds.empty()) {result.state=State::Missing;result.detail="no main\\*.iwd archives";return;}
        const auto manifest=Manifest(theater,iwds);
        std::string previous;
        if(ReadSmall(manifestPath,previous) && previous==manifest && FastFileHeaderOk(zonePath)) {
            std::uint64_t zoneSize=0,zoneTime=0;Stat(zonePath,zoneSize,zoneTime);
            result.state=State::Ready;result.cached=true;result.bytes=static_cast<std::size_t>(zoneSize);result.detail="cached";return;
        }
        std::vector<std::uint8_t> raw;
        if(!bo1assets::ReadFastFile(theater,raw,error)) {result.detail="zombie_theater.ff: "+error;return;}
        std::vector<OpenArchive> open(iwds.size());
        std::vector<bo1model::Archive> archives;
        for(std::size_t n=0;n<iwds.size();++n) if(Open(iwds[n],open[n])) archives.push_back(open[n].archive);
        std::vector<std::uint8_t> file;bo1model::BuildReport report;
        const bool built=bo1model::BuildCharacterFastFile(raw.data(),raw.size(),archives,file,report,&error);
        result.textures=report.textures;result.fallbacks=static_cast<unsigned>(report.fallbacks.size());
        std::string notes;
        for(const auto& n:report.notes) notes+=" | "+n;
        for(const auto& f:report.fallbacks) notes+=" | fallback "+f;
        if(!built) {result.detail=error+notes;return;}
        CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
        CreateDirectoryW(cacheDir.c_str(),nullptr);
        if(!Replace(zonePath,file.data(),file.size())) {result.detail="cannot write "+result.file;return;}
        if(!Replace(manifestPath,manifest.data(),manifest.size())) Log("BO1_MODELS manifest_write_failed=1");
        result.state=State::Ready;result.bytes=file.size();
        char summary[160];
        std::snprintf(summary,sizeof(summary),"built models=%zu images=%zu image_bytes=%zu strings=%zu",
            report.models,report.images,report.imageBytes,report.scriptStrings);
        result.detail=summary+notes;
    }();
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    {std::lock_guard<std::mutex> lock(mutex);status=result;}
    changed.notify_all();
    Log("BO1_MODELS state=%s cached=%d textures=%u fallbacks=%u bytes=%llu seconds=%.2f file=%s detail=%s",StateText(result.state),result.cached,
        result.textures,result.fallbacks,static_cast<unsigned long long>(result.bytes),result.seconds,result.file.c_str(),result.detail.c_str());
}
} // namespace

const char* StateText(State state) {
    switch(state) {
        case State::Idle: return "IDLE";
        case State::Building: return "BUILDING";
        case State::Ready: return "READY";
        case State::Missing: return "MISSING";
        case State::Failed: return "FAILED";
    }
    return "?";
}
void StartBuild() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(started) return;
        started=true;status.state=State::Building;
        markerPath=bo1assets::GameDirectory()+L"bo1mw2\\cache\\bo1_zombie_models.loading";
        staleMarker=GetFileAttributesW(markerPath.c_str())!=INVALID_FILE_ATTRIBUTES;
        // Test42: skip the models for this run only. A freeze that had
        // nothing to do with them (Test41) must not disable them for good;
        // a real model crash still alternates instead of looping.
        if(staleMarker) DeleteFileW(markerPath.c_str());
    }
    if(staleMarker) Log("BO1_MODELS guard=STALE_MARKER models_skipped=1 this_run_only=1 marker=%s (an earlier run ended while the models were new)",
        bo1assets::Utf8(markerPath).c_str());
    std::thread([]{
        try {Build();}
        catch(...) {
            {std::lock_guard<std::mutex> lock(mutex);status.state=State::Failed;status.detail="exception while building";}
            changed.notify_all();Log("BO1_MODELS state=FAILED detail=exception");
        }
    }).detach();
}
bool WaitReady(unsigned milliseconds) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock,std::chrono::milliseconds(milliseconds),[]{return status.state!=State::Building;});
    return status.state==State::Ready;
}
Status GetStatus() {
    std::lock_guard<std::mutex> lock(mutex);
    auto s=status;
    if(!confirmed && staleMarker) {s.skipped=true;s.skipReason="marker from an earlier run";}
    else if(!confirmed && unfinished) {s.skipped=true;s.skipReason="earlier load did not finish";}
    return s;
}
bool GuardAllowsLoad(std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex);
    if(confirmed) return true;
    if(staleMarker) {reason="STALE_MARKER";return false;}
    if(loadPending) {reason="EARLIER_LOAD_UNFINISHED";unfinished=true;return false;}
    return true;
}
void GuardBegin(const std::string& level) {
    std::lock_guard<std::mutex> lock(mutex);
    if(confirmed || markerPath.empty()) return;
    loadPending=true;
    if(markerOwned) return;
    const auto text="bo1mw2 model guard: "+level+"\r\n";
    CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
    CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2\\cache").c_str(),nullptr);
    if(WriteAll(markerPath,text.data(),text.size())) markerOwned=true;
    else Log("BO1_MODELS guard=MARKER_WRITE_FAILED");
}
void GuardLoaded() {loadPending=false;}
void GuardUnused() {
    std::lock_guard<std::mutex> lock(mutex);
    if(markerOwned && !markerPath.empty()) DeleteFileW(markerPath.c_str());
    markerOwned=false;
}
bool GuardConfirm() {
    std::lock_guard<std::mutex> lock(mutex);
    if(confirmed) return false;
    confirmed=true;
    if(markerOwned && !markerPath.empty()) DeleteFileW(markerPath.c_str());
    markerOwned=false;
    return true;
}
void GuardProcessExit() {
    // A normal exit is not a crash; an unfinished load (error dialog) keeps
    // its marker so the next run skips the models.
    if(markerOwned.load() && !loadPending.load() && !markerPath.empty()) DeleteFileW(markerPath.c_str());
}
} // namespace mod::bo1models
