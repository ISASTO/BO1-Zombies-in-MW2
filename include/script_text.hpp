#pragma once
#include <cstddef>
#include <string>
namespace mod {
inline bool DecodeScriptText(const char* data,std::size_t capacity,std::string& out) {
    if(!data)return false;
    for(std::size_t n=0;n<capacity;++n) {
        if(!data[n]) {out.assign(data,n);return true;}
        const auto c=static_cast<unsigned char>(data[n]);
        if(c<32 || c>126)return false;
    }
    return false;
}
} // namespace mod
