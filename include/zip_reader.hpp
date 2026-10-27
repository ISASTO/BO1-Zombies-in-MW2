#pragma once
// Minimal read-only zip (Black Ops .iwd) reader: the central directory, then
// single entries on demand (stored or deflated). The caller supplies the
// file reads, so huge archives are never loaded whole. Header-only; the
// inflate is miniz's tinfl (third_party/miniz).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
extern "C" {
#include "../third_party/miniz/miniz_tinfl.h"
}

namespace mod::zip {
struct Entry {
    std::string name;                    // as stored, '/' separated
    std::uint16_t method=0;              // 0 stored, 8 deflated
    std::uint32_t compressed=0,size=0,crc=0;
    std::uint64_t localHeader=0;
};
// read(offset, buffer, bytes) -> true when all bytes were read.
using ReadAt=std::function<bool(std::uint64_t,void*,std::size_t)>;

inline std::uint16_t U16(const std::uint8_t* p) {return static_cast<std::uint16_t>(p[0]|(p[1]<<8));}
inline std::uint32_t U32(const std::uint8_t* p) {return static_cast<std::uint32_t>(p[0])|(static_cast<std::uint32_t>(p[1])<<8)|(static_cast<std::uint32_t>(p[2])<<16)|(static_cast<std::uint32_t>(p[3])<<24);}
inline bool SameName(const std::string& a,const std::string& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){
        if(x=='\\') x='/';
        if(y=='\\') y='/';
        return std::tolower(static_cast<unsigned char>(x))==std::tolower(static_cast<unsigned char>(y));});
}

// The central directory of an archive of `fileSize` bytes.
inline bool ReadDirectory(const ReadAt& read,std::uint64_t fileSize,std::vector<Entry>& out,std::string* error=nullptr) {
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    out.clear();
    if(fileSize<22) return fail("too small for a zip");
    const std::uint64_t tail=std::min<std::uint64_t>(fileSize,65557);
    std::vector<std::uint8_t> end(static_cast<std::size_t>(tail));
    if(!read(fileSize-tail,end.data(),end.size())) return fail("cannot read the end of the zip");
    std::size_t at=end.size()-22;
    for(;;) {
        if(U32(end.data()+at)==0x06054b50u) break;
        if(!at) return fail("no end-of-central-directory record");
        --at;
    }
    const auto* e=end.data()+at;
    const std::uint32_t count=U16(e+10),dirSize=U32(e+12),dirOffset=U32(e+16);
    if(count==0xffff || dirSize==0xffffffffu || dirOffset==0xffffffffu) return fail("zip64 archives are not supported");
    if(static_cast<std::uint64_t>(dirOffset)+dirSize>fileSize || dirSize>64u*1024*1024) return fail("bad central directory");
    std::vector<std::uint8_t> dir(dirSize);
    if(dirSize && !read(dirOffset,dir.data(),dir.size())) return fail("cannot read the central directory");
    std::size_t p=0;
    for(std::uint32_t n=0;n<count;++n) {
        if(p+46>dir.size() || U32(dir.data()+p)!=0x02014b50u) return fail("bad central directory entry");
        const auto* h=dir.data()+p;
        Entry entry;entry.method=U16(h+10);entry.crc=U32(h+16);entry.compressed=U32(h+20);entry.size=U32(h+24);
        const std::size_t nameLength=U16(h+28),extra=U16(h+30),comment=U16(h+32);
        entry.localHeader=U32(h+42);
        if(p+46+nameLength+extra+comment>dir.size()) return fail("bad central directory entry");
        entry.name.assign(reinterpret_cast<const char*>(h+46),nameLength);
        out.push_back(std::move(entry));
        p+=46+nameLength+extra+comment;
    }
    return true;
}

// One entry's bytes (CRC-checked).
inline bool ReadEntry(const ReadAt& read,std::uint64_t fileSize,const Entry& entry,std::vector<std::uint8_t>& out,std::string* error=nullptr) {
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    if(entry.size>256u*1024*1024 || entry.compressed>256u*1024*1024) return fail("entry too large");
    std::uint8_t local[30];
    if(entry.localHeader+30>fileSize || !read(entry.localHeader,local,sizeof(local)) || U32(local)!=0x04034b50u) return fail("bad local header");
    const std::uint64_t data=entry.localHeader+30+U16(local+26)+U16(local+28);
    if(data+entry.compressed>fileSize) return fail("entry past the end of the zip");
    std::vector<std::uint8_t> packed(entry.compressed);
    if(entry.compressed && !read(data,packed.data(),packed.size())) return fail("cannot read the entry");
    out.assign(entry.size,0);
    if(entry.method==0) {
        if(entry.compressed!=entry.size) return fail("stored entry size mismatch");
        out=std::move(packed);
    } else if(entry.method==8) {
        const auto n=tinfl_decompress_mem_to_mem(out.data(),out.size(),packed.data(),packed.size(),TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if(n==TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || n!=entry.size) return fail("inflate failed");
    } else return fail("unsupported compression method");
    // CRC-32 (IEEE), table-free.
    std::uint32_t crc=0xffffffffu;
    for(auto b:out) {crc^=b;for(int k=0;k<8;++k) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    if((crc^0xffffffffu)!=entry.crc) return fail("CRC mismatch");
    return true;
}
} // namespace mod::zip
