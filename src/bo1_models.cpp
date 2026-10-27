#include "bo1_models.hpp"
#include "bo1_assets.hpp"
#include "bo1_model.hpp"
#include "zip_reader.hpp"
#include "runtime.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace mod::bo1models {
namespace {
// Bump when the generated zone changes so cached files are rebuilt.
constexpr const char* kBuilderVersion="bo1mw2-models-1";
std::mutex mutex;
std::condition_variable changed;
Status status;
bool started=false;

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
// images/<name>.iwi for every wanted name, from the last archive that has it.
std::map<std::string,bo1model::Texture> ReadTextures(const std::vector<std::wstring>& iwds,const std::set<std::string>& wanted,std::string& notes) {
    std::map<std::string,bo1model::Texture> found;
    for(auto it=iwds.rbegin();it!=iwds.rend() && found.size()<wanted.size();++it) {
        File f;f.handle=CreateFileW(it->c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_RANDOM_ACCESS,nullptr);
        if(f.handle==INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(f.handle,&size)) continue;
        const HANDLE h=f.handle;
        zip::ReadAt read=[h](std::uint64_t at,void* out,std::size_t n) {
            OVERLAPPED o{};o.Offset=static_cast<DWORD>(at);o.OffsetHigh=static_cast<DWORD>(at>>32);
            auto* p=static_cast<std::uint8_t*>(out);
            while(n) {
                DWORD got=0;const auto chunk=static_cast<DWORD>(std::min<std::size_t>(n,1u<<24));
                if(!::ReadFile(h,p,chunk,&got,&o) || !got) return false;
                p+=got;n-=got;at+=got;o.Offset=static_cast<DWORD>(at);o.OffsetHigh=static_cast<DWORD>(at>>32);
            }
            return true;
        };
        std::vector<zip::Entry> entries;std::string error;
        if(!zip::ReadDirectory(read,static_cast<std::uint64_t>(size.QuadPart),entries,&error)) {notes+=" "+bo1assets::Utf8(*it)+":"+error;continue;}
        for(const auto& e:entries) {
            if(e.name.size()<12) continue;
            for(const auto& name:wanted) {
                if(found.count(name) || !zip::SameName(e.name,"images/"+name+".iwi")) continue;
                std::vector<std::uint8_t> bytes;bo1model::Texture t;
                if(!zip::ReadEntry(read,static_cast<std::uint64_t>(size.QuadPart),e,bytes,&error)) {notes+=" "+name+":"+error;continue;}
                if(!bo1model::ParseIwi13(bytes.data(),bytes.size(),t,&error)) {notes+=" "+name+":"+error;continue;}
                found.emplace(name,std::move(t));
            }
        }
    }
    return found;
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
        const auto strings=bo1xanim::ParseZoneStrings(raw.data(),raw.size());
        if(!strings) {result.detail="zombie_theater.ff string table unreadable";return;}
        auto character=bo1model::CarveCharacter(raw.data(),raw.size(),strings->strings,strings->assetsOffset,&error);
        if(!character) {result.detail=error;return;}
        raw.clear();raw.shrink_to_fit();
        const auto& c=*character;
        std::set<std::string> wanted{c.headColor,c.headNormal,c.headSpec,c.eyeColor,c.bodyColor,c.bodyNormal,c.bodySpec};
        if(!c.headGloss.empty()) wanted.insert(c.headGloss);
        std::string notes;
        auto textures=ReadTextures(iwds,wanted,notes);
        result.textures=static_cast<unsigned>(textures.size());
        // MW2 keeps gloss in the specular map's alpha.
        const auto spec=textures.find(c.headSpec);
        if(spec!=textures.end() && spec->second.format==bo1model::kD3dDxt1) {
            bo1model::Texture combined;const auto gloss=textures.find(c.headGloss);
            if(gloss!=textures.end() && bo1model::CombineSpecGloss(spec->second,gloss->second,combined)) spec->second=std::move(combined);
            else if(bo1model::WithGloss(spec->second,0x60,combined)) {spec->second=std::move(combined);notes+=" head gloss: constant";}
        }
        bo1model::ZoneSpec zone;std::vector<std::string> fallbacks;
        if(!bo1model::CharacterSpec(c,textures,zone,&error,&fallbacks)) {result.detail=error+(notes.empty()?"":" |"+notes);return;}
        result.fallbacks=static_cast<unsigned>(fallbacks.size());
        for(const auto& f:fallbacks) notes+=" fallback "+f;
        bo1model::ZoneResult built;
        if(!bo1model::BuildZone(zone,built,&error)) {result.detail=error;return;}
        const auto file=bo1model::FastFile(built);
        CreateDirectoryW((bo1assets::GameDirectory()+L"bo1mw2").c_str(),nullptr);
        CreateDirectoryW(cacheDir.c_str(),nullptr);
        if(!Replace(zonePath,file.data(),file.size())) {result.detail="cannot write "+result.file;return;}
        if(!Replace(manifestPath,manifest.data(),manifest.size())) Log("BO1_MODELS manifest_write_failed=1");
        result.state=State::Ready;result.bytes=file.size();
        char summary[160];
        std::snprintf(summary,sizeof(summary),"built models=%zu images=%zu image_bytes=%zu strings=%zu",
            zone.models.size(),zone.images.size(),built.imageBytes,built.scriptStrings);
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
    }
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
Status GetStatus() {std::lock_guard<std::mutex> lock(mutex);return status;}
} // namespace mod::bo1models
