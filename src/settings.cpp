#include "runtime.hpp"
#include <cwchar>

namespace mod {
namespace {
std::wstring SettingsPath() {
    wchar_t module[32768]{};
    const DWORD size=GetModuleFileNameW(nullptr,module,32768);
    if (!size || size>=32768) return {};
    wchar_t* end=std::wcsrchr(module,L'\\');
    if (!end) return {};
    *end=0;
    const std::wstring directory=std::wstring(module)+L"\\bo1mw2";
    if (!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return {};
    return directory+L"\\settings.ini";
}
}
void LoadFovPreference(FovPreference& preference) {
    const auto path=SettingsPath();
    preference.load({});
    if (path.empty()) { Log("FOV_SETTINGS directory unavailable"); return; }
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) {
        Log("FOV_SETTINGS no saved preference or unreadable file error=%lu path=%ls",GetLastError(),path.c_str()); return;
    }
    LARGE_INTEGER length{}; char bytes[128]{}; DWORD count=0;
    const bool ok=GetFileSizeEx(file,&length) && length.QuadPart>0 && length.QuadPart<=128
        && ReadFile(file,bytes,static_cast<DWORD>(length.QuadPart),&count,nullptr)
        && count==static_cast<DWORD>(length.QuadPart);
    CloseHandle(file);
    const auto value=ok?ParseFovPreference(std::string(bytes,count)):std::nullopt;
    preference.load(value);
    Log("FOV_SETTINGS loaded=%d saved=%d path=%ls",value.has_value(),value.value_or(0),path.c_str());
    if (!value) Log("FOV_SETTINGS invalid/unrecognized file ignored; no automatic overwrite");
}
bool SaveFovPreference(int value) {
    const auto text=SerializeFovPreference(value);
    const auto path=SettingsPath();
    if (text.empty() || path.empty()) return false;
    const auto temp=path+L".tmp";
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) { Log("FOV_SETTINGS save open failed error=%lu",GetLastError()); return false; }
    DWORD written=0;
    bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)
        && written==text.size() && FlushFileBuffers(file);
    DWORD error=ok?ERROR_SUCCESS:GetLastError();
    CloseHandle(file);
    if (ok) {
        ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if (!ok) error=GetLastError();
    }
    if (!ok) { DeleteFileW(temp.c_str()); Log("FOV_SETTINGS save failed error=%lu",error); }
    else Log("FOV_SETTINGS saved=%d path=%ls",value,path.c_str());
    return ok;
}
bool ClearFovPreference() {
    const auto path=SettingsPath();
    if (path.empty()) return false;
    const bool deleted=DeleteFileW(path.c_str())!=0;
    const auto error=deleted?ERROR_SUCCESS:GetLastError();
    const bool ok=deleted || error==ERROR_FILE_NOT_FOUND;
    Log("FOV_SETTINGS clear saved preference success=%d error=%lu",ok,error);
    return ok;
}
} // namespace mod
