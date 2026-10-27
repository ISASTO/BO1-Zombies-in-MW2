#include "bo1_assets.hpp"
#include "runtime.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
extern "C" {
#include "../third_party/miniz/miniz_tinfl.h"
}

namespace mod::bo1assets {
namespace {
std::mutex mutex;
std::condition_variable changed;
Status status;
std::vector<bo1xanim::Anim> anims;
bool started=false;

std::wstring GameDirectory() {
    wchar_t exe[32768]{};
    const auto count=GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path(exe,count);
    const auto slash=path.find_last_of(L"\\/");
    return slash==std::wstring::npos?std::wstring():path.substr(0,slash+1);
}
std::string Utf8(const std::wstring& text) {
    if(text.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<std::size_t>(size>0?size:0),'\0');
    if(size>0) WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
std::wstring Widen(const std::string& text) {
    if(text.empty()) return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(static_cast<std::size_t>(size>0?size:0),L'\0');
    if(size>0) MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size);
    return out;
}
bool IsFile(const std::wstring& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring Trim(std::wstring s) {
    while(!s.empty() && (s.back()==L'\r' || s.back()==L'\n' || s.back()==L' ' || s.back()==L'"' || s.back()==L'\\' || s.back()==L'/')) s.pop_back();
    std::size_t start=0;while(start<s.size() && (s[start]==L' ' || s[start]==L'"' || s[start]==0xfeff)) ++start;
    return s.substr(start);
}
std::wstring ZoneFor(const std::wstring& folder) {return folder+L"\\zone\\Common\\common_zombie.ff";}
std::wstring ReadRegistryPath(HKEY root,const wchar_t* key,const wchar_t* value) {
    wchar_t buffer[32768]{};DWORD size=sizeof(buffer);
    if(RegGetValueW(root,key,value,RRF_RT_REG_SZ,nullptr,buffer,&size)!=ERROR_SUCCESS) return {};
    return Trim(buffer);
}
// bo1mw2\bo1_path.txt (written by the installer), then the usual install
// locations next to MW2, in Steam (app 42700) and common retail folders.
std::wstring FindZone() {
    const auto game=GameDirectory();
    std::vector<std::wstring> folders;
    if(FILE* f=_wfopen((game+L"bo1mw2\\bo1_path.txt").c_str(),L"rb")) {
        char line[4096]{};const auto read=std::fread(line,1,sizeof(line)-1,f);std::fclose(f);
        line[read]=0;std::string text(line);
        const auto end=text.find_first_of("\r\n");if(end!=std::string::npos)text.resize(end);
        if(text.size()>=3 && static_cast<unsigned char>(text[0])==0xef)text.erase(0,3);
        const auto folder=Trim(Widen(text));
        if(!folder.empty()) {
            if(IsFile(folder))return folder;
            folders.push_back(folder);
        }
    }
    if(!game.empty()) {
        auto parent=game.substr(0,game.size()-1);
        const auto slash=parent.find_last_of(L"\\/");
        if(slash!=std::wstring::npos) {
            parent.resize(slash);
            for(const auto* name:{L"Call of Duty Black Ops",L"Call of Duty - Black Ops",L"Call of Duty Black Ops 1",L"Black Ops"})folders.push_back(parent+L"\\"+name);
        }
    }
    for(const auto* key:{L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 42700",
                         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 42700"}) {
        const auto path=ReadRegistryPath(HKEY_LOCAL_MACHINE,key,L"InstallLocation");
        if(!path.empty()) folders.push_back(path);
    }
    for(const auto* path:{L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Call of Duty Black Ops",
                          L"C:\\Program Files\\Steam\\steamapps\\common\\Call of Duty Black Ops",
                          L"D:\\SteamLibrary\\steamapps\\common\\Call of Duty Black Ops",
                          L"D:\\Games\\Call of Duty - Black Ops",L"C:\\Games\\Call of Duty - Black Ops",
                          L"D:\\Games\\Call of Duty Black Ops",L"C:\\Games\\Call of Duty Black Ops"})folders.push_back(path);
    for(const auto& folder:folders) {
        const auto zone=ZoneFor(Trim(folder));
        if(IsFile(zone)) return zone;
    }
    return {};
}
bool ReadFile(const std::wstring& path,std::vector<std::uint8_t>& out,std::string& error) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) {error="open failed";return false;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<64 || size.QuadPart>512ll*1024*1024) {CloseHandle(file);error="bad size";return false;}
    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done=0;
    while(done<out.size()) {
        DWORD chunk=0;const auto want=static_cast<DWORD>(std::min<std::size_t>(out.size()-done,1u<<24));
        if(!::ReadFile(file,out.data()+done,want,&chunk,nullptr) || !chunk) {CloseHandle(file);error="read failed";return false;}
        done+=chunk;
    }
    CloseHandle(file);return true;
}
// T5 PC fastfile: "IWffu100", version 473, then one zlib stream.
bool Inflate(const std::vector<std::uint8_t>& file,std::vector<std::uint8_t>& out,std::string& error) {
    if(file.size()<16 || std::memcmp(file.data(),"IWffu100",8)) {error="not an unsigned IW fastfile";return false;}
    std::uint32_t version=0;std::memcpy(&version,file.data()+8,4);
    if(version!=bo1xanim::kT5Version) {error="fastfile version "+std::to_string(version)+" is not BO1 PC (473)";return false;}
    std::size_t length=0;
    void* data=tinfl_decompress_mem_to_heap(file.data()+12,file.size()-12,&length,TINFL_FLAG_PARSE_ZLIB_HEADER);
    if(!data || length<bo1xanim::kT5HeaderBytes) {if(data)std::free(data);error="inflate failed";return false;}
    std::uint32_t declared=0;std::memcpy(&declared,data,4);
    if(static_cast<std::size_t>(declared)+bo1xanim::kT5HeaderBytes!=length) {std::free(data);error="zone size mismatch";return false;}
    out.assign(static_cast<std::uint8_t*>(data),static_cast<std::uint8_t*>(data)+length);std::free(data);return true;
}
void Load() {
    const auto begin=std::chrono::steady_clock::now();
    Status result;result.state=State::Failed;result.wanted=static_cast<unsigned>(WantedAnims().size());
    std::vector<bo1xanim::Anim> loaded;
    const auto zone=FindZone();
    if(zone.empty()) {
        result.state=State::Missing;result.detail="common_zombie.ff not found (set bo1mw2\\bo1_path.txt to your Black Ops folder)";
    } else {
        result.zone=Utf8(zone);
        std::vector<std::uint8_t> file,raw;std::string error;
        if(ReadFile(zone,file,error) && Inflate(file,raw,error)) {
            file.clear();file.shrink_to_fit();
            if(const auto strings=bo1xanim::ParseZoneStrings(raw.data(),raw.size())) {
                const std::unordered_set<std::string> wanted(WantedAnims().begin(),WantedAnims().end());
                std::size_t rejected=0;
                loaded=bo1xanim::CarveAnims(raw.data(),raw.size(),*strings,wanted,&rejected);
                result.found=static_cast<unsigned>(loaded.size());result.rejected=static_cast<unsigned>(rejected);
                if(loaded.empty()) result.detail="no zombie animations found in zone";
                else {result.state=State::Ready;result.detail="ok";}
            } else error="zone string table unreadable";
        }
        if(!error.empty()) result.detail=error;
    }
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    {
        std::lock_guard<std::mutex> lock(mutex);
        anims=std::move(loaded);status=result;
    }
    changed.notify_all();
    Log("BO1_ASSETS state=%s found=%u/%u rejected=%u seconds=%.2f zone=%s detail=%s",StateText(result.state),result.found,result.wanted,
        result.rejected,result.seconds,result.zone.empty()?"NONE":result.zone.c_str(),result.detail.c_str());
}
} // namespace

const char* StateText(State state) {
    switch(state) {
        case State::Idle: return "IDLE";
        case State::Loading: return "LOADING";
        case State::Ready: return "READY";
        case State::Missing: return "MISSING";
        case State::Failed: return "FAILED";
    }
    return "?";
}
const std::vector<std::string>& WantedAnims() {
    static const std::vector<std::string> names{
        "ai_zombie_walk_v1","ai_zombie_walk_v2","ai_zombie_walk_v4","ai_zombie_walk_fast_v1","ai_zombie_run_v2","ai_zombie_run_v3",
        "ai_zombie_sprint_v1","ai_zombie_sprint_v2","ai_zombie_idle_v1","ai_zombie_death_v1","ai_zombie_death_v2",
        "ai_zombie_attack_v1","ai_zombie_attack_v2","ai_zombie_traverse_ground_v1_walk","ai_zombie_traverse_ground_v1_run",
        "ai_zombie_traverse_ground_climbout_fast"};
    return names;
}
void StartLoad() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(started) return;
        started=true;status.state=State::Loading;status.wanted=static_cast<unsigned>(WantedAnims().size());
    }
    std::thread([]{
        try {Load();}
        catch(...) {
            {std::lock_guard<std::mutex> lock(mutex);status.state=State::Failed;status.detail="exception while loading";}
            changed.notify_all();Log("BO1_ASSETS state=FAILED detail=exception");
        }
    }).detach();
}
bool WaitReady(unsigned milliseconds) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock,std::chrono::milliseconds(milliseconds),[]{return status.state!=State::Loading;});
    return status.state==State::Ready;
}
Status GetStatus() {std::lock_guard<std::mutex> lock(mutex);return status;}
const bo1xanim::Anim* Find(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    if(status.state!=State::Ready) return nullptr;
    for(const auto& a:anims) if(a.name==name) return &a;
    return nullptr;
}
} // namespace mod::bo1assets
