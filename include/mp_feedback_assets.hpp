#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <optional>
namespace mod {
struct MpImage {std::uint16_t width=0,height=0;std::size_t offset=0,bytes=0,rowBytes=0,rows=0;};
// IW4 IWI version 8: image info starts at 8, four picmip sizes at 16.
// Mips are stored smallest first; the complete base level ends at file EOF.
inline std::optional<MpImage> ReadMpHitmarkerImage(const std::uint8_t* data,std::size_t size) {
    if(!data || size<32 || std::memcmp(data,"IWi\x08",4) || data[8]!=0x0d)return std::nullopt;
    auto word=[&](std::size_t at){std::uint16_t v;std::memcpy(&v,data+at,2);return v;};
    std::uint32_t fileBytes=0;std::memcpy(&fileBytes,data+16,4);
    const auto width=word(10),height=word(12),depth=word(14);
    if(width!=36 || height!=72 || depth!=1 || fileBytes!=size)return std::nullopt;
    const std::size_t row=((width+3)/4)*16,rows=(height+3)/4,bytes=row*rows;
    if(bytes>size-32)return std::nullopt;
    for(std::size_t at=16;at<32;at+=4) {
        std::uint32_t v;std::memcpy(&v,data+at,4);
        if(v<32 || v>size)return std::nullopt;
    }
    return MpImage{width,height,size-bytes,bytes,row,rows};
}
} // namespace mod
