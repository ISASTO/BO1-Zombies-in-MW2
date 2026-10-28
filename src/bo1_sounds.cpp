#include "bo1_sounds.hpp"
#include "bo1_assets.hpp"
#include "bo1_cache_io.hpp"
#include "bo1_sound_build.hpp"
#include "runtime.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace mod::bo1sounds {
namespace {
// Bump when the generated zone changes so cached files are rebuilt.
constexpr const char* kBuilderVersion="bo1mw2-sounds-1";
std::mutex mutex;
std::condition_variable changed;
Status status;
bool started=false;
std::wstring markerPath;
std::atomic<bool> markerOwned{false},loadPending{false};
bool staleMarker=false,unfinished=false,confirmed=false;     // under mutex

void Build() {
    const auto begin=std::chrono::steady_clock::now();
    Status result;result.state=State::Failed;
    const auto folder=bo1assets::BlackOpsFolder();
    const auto cacheDir=bo1assets::GameDirectory()+L"bo1mw2\\cache";
    const auto zonePath=cacheDir+L"\\bo1_zombie_sounds.ff",manifestPath=cacheDir+L"\\bo1_zombie_sounds.txt";
    result.file=bo1assets::Utf8(zonePath);
    std::string error;
    [&]{
        if(folder.empty()) {result.state=State::Missing;result.detail="Black Ops folder not found (set bo1mw2\\bo1_path.txt)";return;}
        const auto common=folder+L"\\zone\\Common\\common_zombie.ff";
        const auto theater=folder+L"\\zone\\Common\\zombie_theater.ff";
        std::uint64_t size=0,time=0;
        if(!bo1cache::Stat(common,size,time)) {result.state=State::Missing;result.detail="zone\\Common\\common_zombie.ff not found";return;}
        const bool haveTheater=bo1cache::Stat(theater,size,time);
        const auto iwds=bo1cache::Iwds(folder);
        std::vector<std::wstring> sources{common};
        if(haveTheater) sources.push_back(theater);
        sources.insert(sources.end(),iwds.begin(),iwds.end());
        const auto manifest=bo1cache::Manifest(kBuilderVersion,sources);
        std::string previous;
        if(bo1cache::ReadSmall(manifestPath,previous) && previous==manifest && bo1cache::FastFileHeaderOk(zonePath)) {
            std::uint64_t zoneSize=0,zoneTime=0;bo1cache::Stat(zonePath,zoneSize,zoneTime);
            result.state=State::Ready;result.cached=true;result.bytes=static_cast<std::size_t>(zoneSize);result.detail="cached";return;
        }
        std::vector<std::uint8_t> commonRaw,theaterRaw;
        if(!bo1assets::ReadFastFile(common,commonRaw,error)) {result.detail="common_zombie.ff: "+error;return;}
        std::string notes;
        if(haveTheater && !bo1assets::ReadFastFile(theater,theaterRaw,error)) {notes+=" | zombie_theater.ff: "+error;theaterRaw.clear();}
        std::vector<bo1cache::OpenArchive> open(iwds.size());
        std::vector<bo1model::Archive> archives;
        for(std::size_t n=0;n<iwds.size();++n) if(bo1cache::Open(iwds[n],open[n])) archives.push_back(open[n].archive);
        std::vector<std::uint8_t> file;bo1sound::SoundBuildReport report;
        const bool built=bo1sound::BuildSoundFastFile(commonRaw.data(),commonRaw.size(),theaterRaw.data(),theaterRaw.size(),archives,file,report,&error);
        commonRaw.clear();commonRaw.shrink_to_fit();theaterRaw.clear();theaterRaw.shrink_to_fit();
        for(const auto& n:report.notes) notes+=" | "+n;
        if(!built) {result.detail=error+notes;return;}
        CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
        CreateDirectoryW(cacheDir.c_str(),nullptr);
        if(!bo1cache::Replace(zonePath,file.data(),file.size())) {result.detail="cannot write "+result.file;return;}
        if(!bo1cache::Replace(manifestPath,manifest.data(),manifest.size())) Log("BO1_SOUNDS manifest_write_failed=1");
        result.state=State::Ready;result.bytes=file.size();
        result.lists=static_cast<unsigned>(report.lists);result.sounds=static_cast<unsigned>(report.sounds);
        char summary[256];
        std::snprintf(summary,sizeof(summary),"built lists=%zu aliases=%zu sounds=%zu pcm_bytes=%zu audio_seconds=%.1f shared=%zu "
            "skipped_aliases=%u missing_lists=%u music_found=%u music_missing=%u decode_failures=%u",
            report.lists,report.aliases,report.sounds,report.pcmBytes,report.seconds10/10.0,report.references,report.skippedAliases,
            report.missingLists,report.streamedFound,report.streamedMissing,report.decodeFailures);
        result.detail=summary+notes;
    }();
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    {std::lock_guard<std::mutex> lock(mutex);status=result;}
    changed.notify_all();
    Log("BO1_SOUNDS state=%s cached=%d bytes=%llu seconds=%.2f file=%s detail=%s",StateText(result.state),result.cached,
        static_cast<unsigned long long>(result.bytes),result.seconds,result.file.c_str(),result.detail.c_str());
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
        markerPath=bo1assets::GameDirectory()+L"bo1mw2\\cache\\bo1_zombie_sounds.loading";
        staleMarker=GetFileAttributesW(markerPath.c_str())!=INVALID_FILE_ATTRIBUTES;
        if(staleMarker) DeleteFileW(markerPath.c_str());     // skip this run only
    }
    if(staleMarker) Log("BO1_SOUNDS guard=STALE_MARKER sounds_skipped=1 this_run_only=1 marker=%s (an earlier run ended while the sounds were new)",
        bo1assets::Utf8(markerPath).c_str());
    std::thread([]{
        try {Build();}
        catch(...) {
            {std::lock_guard<std::mutex> lock(mutex);status.state=State::Failed;status.detail="exception while building";}
            changed.notify_all();Log("BO1_SOUNDS state=FAILED detail=exception");
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
    const auto text="bo1mw2 sound guard: "+level+"\r\n";
    CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
    CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2\\cache").c_str(),nullptr);
    if(bo1cache::WriteAll(markerPath,text.data(),text.size())) markerOwned=true;
    else Log("BO1_SOUNDS guard=MARKER_WRITE_FAILED");
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
    if(markerOwned.load() && !loadPending.load() && !markerPath.empty()) DeleteFileW(markerPath.c_str());
}
} // namespace mod::bo1sounds
