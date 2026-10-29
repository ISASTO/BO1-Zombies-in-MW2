#pragma once
// Test45: the BO1 zombies HUD images (round tally chalk marks, the Double
// Points and Insta-Kill icons), read from the owner's Black Ops .iwd archives
// by the model build and kept in bo1mw2\cache\bo1_hud.bin. The overlay
// decodes them to 32-bit BGRA textures (any IWI v13 2D format BO1 uses for
// HUD art: RGBA, RGB, luminance-alpha, luminance, alpha, DXT1/3/5).
//
// Pack: "BO1HUD1\0", u32 count, then per image: u16 name length, name,
// u32 byte count, the .iwi file as stored in the archive.
//
// Header-only and free of engine state so the native tests can exercise it.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mod::bo1hud {
inline constexpr std::array<const char*,7> kImages={"chalkmarks_1","chalkmarks_2","chalkmarks_3","chalkmarks_4","chalkmarks_5",
    "specialty_2x_zombies","specialty_instakill_zombies"};
inline constexpr const char* kPackMagic="BO1HUD1";

struct Bgra {
    std::uint16_t width=0,height=0;
    std::vector<std::uint8_t> pixels;    // B, G, R, A per texel, rows top to bottom
};

namespace detail {
template<class T> T Read(const std::uint8_t* p) {T v{};std::memcpy(&v,p,sizeof(v));return v;}
inline void Rgb565(std::uint16_t c,std::uint8_t* rgb) {
    rgb[0]=static_cast<std::uint8_t>(((c>>11)&31)*255/31);rgb[1]=static_cast<std::uint8_t>(((c>>5)&63)*255/63);rgb[2]=static_cast<std::uint8_t>((c&31)*255/31);
}
// One 4x4 colour block (DXT1 semantics when dxt1, else always four colours).
inline void ColorBlock(const std::uint8_t* b,bool dxt1,std::array<std::array<std::uint8_t,4>,16>& out) {
    const auto c0=Read<std::uint16_t>(b),c1=Read<std::uint16_t>(b+2);
    std::array<std::array<std::uint8_t,4>,4> pal{};
    Rgb565(c0,pal[0].data());Rgb565(c1,pal[1].data());pal[0][3]=pal[1][3]=255;
    if(c0>c1 || !dxt1) {
        for(int k=0;k<3;++k) {pal[2][k]=static_cast<std::uint8_t>((2*pal[0][k]+pal[1][k])/3);pal[3][k]=static_cast<std::uint8_t>((pal[0][k]+2*pal[1][k])/3);}
        pal[2][3]=pal[3][3]=255;
    } else {
        for(int k=0;k<3;++k) pal[2][k]=static_cast<std::uint8_t>((pal[0][k]+pal[1][k])/2);
        pal[2][3]=255;pal[3]={0,0,0,0};
    }
    const auto bits=Read<std::uint32_t>(b+4);
    for(unsigned n=0;n<16;++n) out[n]=pal[(bits>>(2*n))&3];
}
inline void AlphaBlock5(const std::uint8_t* b,std::array<std::uint8_t,16>& out) {
    const int a0=b[0],a1=b[1];
    std::array<int,8> pal{a0,a1,0,0,0,0,0,0};
    if(a0>a1) for(int n=1;n<7;++n) pal[static_cast<std::size_t>(n+1)]=((7-n)*a0+n*a1)/7;
    else {for(int n=1;n<5;++n) pal[static_cast<std::size_t>(n+1)]=((5-n)*a0+n*a1)/5;pal[6]=0;pal[7]=255;}
    std::uint64_t bits=0;for(int n=0;n<6;++n) bits|=static_cast<std::uint64_t>(b[2+n])<<(8*n);
    for(unsigned n=0;n<16;++n) out[n]=static_cast<std::uint8_t>(pal[(bits>>(3*n))&7]);
}
} // namespace detail

// Decodes the largest mip of a Black Ops IWI v13 file (mips are stored
// smallest first, so the largest is the last one).
inline bool DecodeIwi(const std::uint8_t* p,std::size_t n,Bgra& out,std::string* error=nullptr) {
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    if(n<48 || std::memcmp(p,"IWi",3) || p[3]!=13) return fail("not an IWI v13 image");
    const auto format=p[4],flags=p[5];
    const auto w=detail::Read<std::uint16_t>(p+6),h=detail::Read<std::uint16_t>(p+8),d=detail::Read<std::uint16_t>(p+10);
    if((flags&(4|8)) || d>1) return fail("cube or volume image");
    if(!w || !h || w>2048 || h>2048) return fail("bad image size");
    std::size_t bpp=0;bool block=false;std::size_t blockBytes=0;
    switch(format) {
        case 1: bpp=4;break;            // RGBA (stored B, G, R, A)
        case 2: bpp=3;break;            // RGB (stored B, G, R)
        case 3: bpp=2;break;            // luminance, alpha
        case 4: case 5: bpp=1;break;    // luminance / alpha
        case 0xb: block=true;blockBytes=8;break;
        case 0xc: case 0xd: block=true;blockBytes=16;break;
        default: return fail("unsupported image format");
    }
    auto size=[&](unsigned mw,unsigned mh)->std::size_t {
        mw=std::max(1u,mw);mh=std::max(1u,mh);
        return block?static_cast<std::size_t>((mw+3)/4)*((mh+3)/4)*blockBytes:static_cast<std::size_t>(mw)*mh*bpp;
    };
    // Mips follow the 48-byte header smallest first (one level when flag 2
    // says "no mips"), so the largest starts after all the smaller ones.
    unsigned levels=1;
    if(!(flags&2)) for(unsigned mw=w,mh=h;mw>1 || mh>1;mw=std::max(1u,mw/2),mh=std::max(1u,mh/2)) ++levels;
    std::size_t before=0;
    for(unsigned level=1;level<levels;++level) before+=size(w>>level,h>>level);
    const std::size_t top=size(w,h);
    if(n<48+before+top) return fail("image is shorter than its mips");
    const std::uint8_t* src=p+48+before;
    out.width=w;out.height=h;out.pixels.assign(static_cast<std::size_t>(w)*h*4,0);
    auto put=[&](unsigned x,unsigned y,std::uint8_t r,std::uint8_t g,std::uint8_t b,std::uint8_t a) {
        if(x>=w || y>=h) return;
        auto* q=out.pixels.data()+(static_cast<std::size_t>(y)*w+x)*4;q[0]=b;q[1]=g;q[2]=r;q[3]=a;
    };
    if(!block) {
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            const auto* s=src+(static_cast<std::size_t>(y)*w+x)*bpp;
            switch(format) {
                case 1: put(x,y,s[2],s[1],s[0],s[3]);break;
                case 2: put(x,y,s[2],s[1],s[0],255);break;
                case 3: put(x,y,s[0],s[0],s[0],s[1]);break;
                case 4: put(x,y,s[0],s[0],s[0],255);break;
                case 5: put(x,y,255,255,255,s[0]);break;
            }
        }
        return true;
    }
    const unsigned bw=(w+3u)/4u,bh=(h+3u)/4u;
    for(unsigned by=0;by<bh;++by) for(unsigned bx=0;bx<bw;++bx) {
        const auto* b=src+(static_cast<std::size_t>(by)*bw+bx)*blockBytes;
        std::array<std::array<std::uint8_t,4>,16> colors{};std::array<std::uint8_t,16> alpha{};
        if(format==0xb) {detail::ColorBlock(b,true,colors);for(unsigned k=0;k<16;++k) alpha[k]=colors[k][3];}
        else {
            detail::ColorBlock(b+8,false,colors);
            if(format==0xc) for(unsigned k=0;k<16;++k) {const auto v=(b[k/2]>>(4*(k&1)))&15;alpha[k]=static_cast<std::uint8_t>(v*17);}
            else detail::AlphaBlock5(b,alpha);
        }
        for(unsigned k=0;k<16;++k) put(bx*4+k%4,by*4+k/4,colors[k][0],colors[k][1],colors[k][2],alpha[k]);
    }
    return true;
}

// ------------------------------------------------------------- pack
inline std::vector<std::uint8_t> WritePack(const std::map<std::string,std::vector<std::uint8_t>>& files) {
    std::vector<std::uint8_t> out(kPackMagic,kPackMagic+8);
    auto u32=[&](std::uint32_t v){for(int k=0;k<4;++k) out.push_back(static_cast<std::uint8_t>(v>>(8*k)));};
    u32(static_cast<std::uint32_t>(files.size()));
    for(const auto& [name,bytes]:files) {
        out.push_back(static_cast<std::uint8_t>(name.size()));out.push_back(static_cast<std::uint8_t>(name.size()>>8));
        out.insert(out.end(),name.begin(),name.end());
        u32(static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(),bytes.begin(),bytes.end());
    }
    return out;
}
inline bool ReadPack(const std::uint8_t* p,std::size_t n,std::map<std::string,std::vector<std::uint8_t>>& files) {
    files.clear();
    if(n<12 || std::memcmp(p,kPackMagic,8)) return false;
    const auto count=detail::Read<std::uint32_t>(p+8);
    std::size_t at=12;
    for(std::uint32_t k=0;k<count;++k) {
        if(at+2>n) return false;
        const std::size_t length=detail::Read<std::uint16_t>(p+at);at+=2;
        if(!length || length>255 || at+length+4>n) return false;
        std::string name(reinterpret_cast<const char*>(p+at),length);at+=length;
        const std::size_t size=detail::Read<std::uint32_t>(p+at);at+=4;
        if(size>(16u<<20) || at+size>n) return false;
        files.emplace(std::move(name),std::vector<std::uint8_t>(p+at,p+at+size));at+=size;
    }
    return at==n;
}
} // namespace mod::bo1hud
