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
#include <cstdlib>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cwchar>

namespace mod::bo1sounds {
namespace {
// Bump when the generated zone changes so cached files are rebuilt.
constexpr const char* kBuilderVersion="bo1mw2-sounds-2";   // Test45: ambience, announcer, gib sounds
std::mutex mutex;
std::condition_variable changed;
Status status;
bool started=false;
std::wstring markerPath;
std::atomic<bool> markerOwned{false},loadPending{false};
bool staleMarker=false,unfinished=false,confirmed=false;     // under mutex

// Test45: zone\\<Language>\\<xx>_zombie_theater.ff (English first), whose
// bank "zmb_theater.<language>" holds the announcer.
bool FindLocalizedZone(const std::wstring& folder,std::wstring& path,std::string& bank) {
    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW d{};
    HANDLE find=FindFirstFileW((folder+L"\\zone\\*").c_str(),&d);
    if(find==INVALID_HANDLE_VALUE) return false;
    do {
        if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || !std::wcscmp(d.cFileName,L".") || !std::wcscmp(d.cFileName,L"..")
           || !_wcsicmp(d.cFileName,L"Common")) continue;
        dirs.push_back(d.cFileName);
    } while(FindNextFileW(find,&d));
    FindClose(find);
    std::stable_sort(dirs.begin(),dirs.end(),[](const std::wstring& a,const std::wstring& b){return !_wcsicmp(a.c_str(),L"English") && _wcsicmp(b.c_str(),L"English");});
    for(const auto& dir:dirs) {
        WIN32_FIND_DATAW f{};
        HANDLE zone=FindFirstFileW((folder+L"\\zone\\"+dir+L"\\*_zombie_theater.ff").c_str(),&f);
        if(zone==INVALID_HANDLE_VALUE) continue;
        path=folder+L"\\zone\\"+dir+L"\\"+f.cFileName;
        FindClose(zone);
        bank="zmb_theater.";
        for(const char c:bo1assets::Utf8(dir)) bank+=static_cast<char>(c>='A' && c<='Z'?c-'A'+'a':c);
        return true;
    }
    return false;
}
void Build() {
    const auto begin=std::chrono::steady_clock::now();
    Status result;result.state=State::Failed;
    const auto folder=bo1assets::BlackOpsFolder();
    const auto cacheDir=bo1assets::GameDirectory()+L"bo1mw2\\cache";
    const auto zonePath=cacheDir+L"\\bo1_zombie_sounds.ff",manifestPath=cacheDir+L"\\bo1_zombie_sounds.txt";
    const auto infoPath=cacheDir+L"\\bo1_zombie_sounds.info";
    result.file=bo1assets::Utf8(zonePath);
    std::string error;
    [&]{
        if(folder.empty()) {result.state=State::Missing;result.detail="Black Ops folder not found (set bo1mw2\\bo1_path.txt)";return;}
        const auto common=folder+L"\\zone\\Common\\common_zombie.ff";
        const auto theater=folder+L"\\zone\\Common\\zombie_theater.ff";
        std::uint64_t size=0,time=0;
        if(!bo1cache::Stat(common,size,time)) {result.state=State::Missing;result.detail="zone\\Common\\common_zombie.ff not found";return;}
        const bool haveTheater=bo1cache::Stat(theater,size,time);
        std::wstring localizedPath;std::string localizedBank;
        const bool haveLocalized=FindLocalizedZone(folder,localizedPath,localizedBank) && bo1cache::Stat(localizedPath,size,time);
        const auto iwds=bo1cache::Iwds(folder);
        std::vector<std::wstring> sources{common};
        if(haveTheater) sources.push_back(theater);
        if(haveLocalized) sources.push_back(localizedPath);
        sources.insert(sources.end(),iwds.begin(),iwds.end());
        const auto manifest=bo1cache::Manifest(kBuilderVersion,sources);
        std::string previous;
        if(bo1cache::ReadSmall(manifestPath,previous) && previous==manifest && bo1cache::FastFileHeaderOk(zonePath)) {
            std::uint64_t zoneSize=0,zoneTime=0;bo1cache::Stat(zonePath,zoneSize,zoneTime);
            result.state=State::Ready;result.cached=true;result.bytes=static_cast<std::size_t>(zoneSize);result.detail="cached";
            std::string info;
            if(bo1cache::ReadSmall(infoPath,info) && info.rfind("bed_ms=",0)==0) result.bedMilliseconds=std::atoi(info.c_str()+7);
            return;
        }
        std::vector<std::uint8_t> commonRaw,theaterRaw;
        if(!bo1assets::ReadFastFile(common,commonRaw,error)) {result.detail="common_zombie.ff: "+error;return;}
        std::string notes;
        if(haveTheater && !bo1assets::ReadFastFile(theater,theaterRaw,error)) {notes+=" | zombie_theater.ff: "+error;theaterRaw.clear();}
        std::vector<std::uint8_t> localizedRaw;
        if(haveLocalized && !bo1assets::ReadFastFile(localizedPath,localizedRaw,error)) {notes+=" | "+bo1assets::Utf8(localizedPath)+": "+error;localizedRaw.clear();}
        if(!haveLocalized) notes+=" | no zone\\<language>\\*_zombie_theater.ff (no announcer)";
        bo1sound::LocalizedZone localized{localizedRaw.data(),localizedRaw.size(),localizedBank};
        std::vector<bo1cache::OpenArchive> open(iwds.size());
        std::vector<bo1model::Archive> archives;
        for(std::size_t n=0;n<iwds.size();++n) if(bo1cache::Open(iwds[n],open[n])) archives.push_back(open[n].archive);
        std::vector<std::uint8_t> file;bo1sound::SoundBuildReport report;
        const bool built=bo1sound::BuildSoundFastFile(commonRaw.data(),commonRaw.size(),theaterRaw.data(),theaterRaw.size(),archives,file,report,&error,localized);
        commonRaw.clear();commonRaw.shrink_to_fit();theaterRaw.clear();theaterRaw.shrink_to_fit();localizedRaw.clear();localizedRaw.shrink_to_fit();
        for(const auto& n:report.notes) notes+=" | "+n;
        if(!built) {result.detail=error+notes;return;}
        CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
        CreateDirectoryW(cacheDir.c_str(),nullptr);
        if(!bo1cache::Replace(zonePath,file.data(),file.size())) {result.detail="cannot write "+result.file;return;}
        const std::string info="bed_ms="+std::to_string(report.bedMilliseconds)+"\n";
        if(!bo1cache::Replace(infoPath,info.data(),info.size())) Log("BO1_SOUNDS info_write_failed=1");
        if(!bo1cache::Replace(manifestPath,manifest.data(),manifest.size())) Log("BO1_SOUNDS manifest_write_failed=1");
        result.bedMilliseconds=report.bedMilliseconds;
        result.state=State::Ready;result.bytes=file.size();
        result.lists=static_cast<unsigned>(report.lists);result.sounds=static_cast<unsigned>(report.sounds);
        char summary[400];
        std::snprintf(summary,sizeof(summary),"built lists=%zu aliases=%zu sounds=%zu pcm_bytes=%zu audio_seconds=%.1f shared=%zu "
            "skipped_aliases=%u missing_lists=%u streamed_found=%u streamed_missing=%u decode_failures=%u ambience=%u announcer=%u localized=%s bed_ms=%d",
            report.lists,report.aliases,report.sounds,report.pcmBytes,report.seconds10/10.0,report.references,report.skippedAliases,
            report.missingLists,report.streamedFound,report.streamedMissing,report.decodeFailures,report.ambience,report.announcer,
            report.localizedBank.empty()?"-":report.localizedBank.c_str(),report.bedMilliseconds);
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
