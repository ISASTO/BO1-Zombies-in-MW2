#pragma once
// Windows file helpers shared by the generated BO1 fastfiles (models, sounds):
// stat, small reads, crash-safe replace, the Black Ops .iwd list and offset
// reads on an open archive.
#include "bo1_assets.hpp"
#include "bo1_model_build.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace mod::bo1cache {
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    File()=default;
    File(const File&)=delete;
    File& operator=(const File&)=delete;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
inline bool Stat(const std::wstring& path,std::uint64_t& size,std::uint64_t& time) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a) || (a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    size=(static_cast<std::uint64_t>(a.nFileSizeHigh)<<32)|a.nFileSizeLow;
    time=(static_cast<std::uint64_t>(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime;
    return true;
}
inline bool ReadSmall(const std::wstring& path,std::string& out) {
    File f;f.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f.handle==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(f.handle,&size) || size.QuadPart>1<<20) return false;
    out.assign(static_cast<std::size_t>(size.QuadPart),'\0');
    DWORD read=0;
    return !size.QuadPart || (::ReadFile(f.handle,out.data(),static_cast<DWORD>(out.size()),&read,nullptr) && read==out.size());
}
inline bool WriteAll(const std::wstring& path,const void* data,std::size_t size) {
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
// Write to a temporary name, then replace: a crash never leaves half a file.
inline bool Replace(const std::wstring& path,const void* data,std::size_t size) {
    const auto temporary=path+L".tmp";
    if(!WriteAll(temporary,data,size)) {DeleteFileW(temporary.c_str());return false;}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temporary.c_str());return false;}
    return true;
}
inline std::vector<std::wstring> Iwds(const std::wstring& folder) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW d{};
    HANDLE find=FindFirstFileW((folder+L"\\main\\*.iwd").c_str(),&d);
    if(find==INVALID_HANDLE_VALUE) return out;
    do {if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) out.push_back(folder+L"\\main\\"+d.cFileName);} while(FindNextFileW(find,&d));
    FindClose(find);
    std::sort(out.begin(),out.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    return out;
}
// "<version>\n" then "<path> <size> <time>" per source file.
inline std::string Manifest(const char* version,const std::vector<std::wstring>& sources) {
    std::string m=std::string(version)+"\n";
    for(const auto& path:sources) {
        std::uint64_t size=0,time=0;Stat(path,size,time);
        char buffer[96];std::snprintf(buffer,sizeof(buffer)," %llu %llu\n",static_cast<unsigned long long>(size),static_cast<unsigned long long>(time));
        m+=bo1assets::Utf8(path)+buffer;
    }
    return m;
}
inline bool FastFileHeaderOk(const std::wstring& path) {
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
inline bool Open(const std::wstring& path,OpenArchive& out) {
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
} // namespace mod::bo1cache
