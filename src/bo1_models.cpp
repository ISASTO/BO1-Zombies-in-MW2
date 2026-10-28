#include "bo1_models.hpp"
#include "bo1_assets.hpp"
#include "bo1_model_build.hpp"
#include "bo1_cache_io.hpp"
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
constexpr const char* kBuilderVersion="bo1mw2-models-2";   // Test43: packed bone translations
std::mutex mutex;
std::condition_variable changed;
Status status;
bool started=false;
// Crash guard (see bo1_models.hpp). markerPath is written once in StartBuild,
// before the game can load a level; the atomics are read at process exit.
std::wstring markerPath;
std::atomic<bool> markerOwned{false},loadPending{false};
bool staleMarker=false,unfinished=false,confirmed=false;     // under mutex

using bo1cache::File;
using bo1cache::Stat;
using bo1cache::ReadSmall;
using bo1cache::WriteAll;
using bo1cache::Replace;
using bo1cache::Iwds;
using bo1cache::FastFileHeaderOk;
using bo1cache::OpenArchive;
using bo1cache::Open;
std::string Manifest(const std::wstring& theater,const std::vector<std::wstring>& iwds) {
    std::vector<std::wstring> sources{theater};
    sources.insert(sources.end(),iwds.begin(),iwds.end());
    return bo1cache::Manifest(kBuilderVersion,sources);
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
