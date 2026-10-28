#pragma once
// Black Ops (T5, PC fastfile v473) XAnimParts reader and the x64 MW2
// XAnimParts image builder used by the Test39 animation import.
//
// The flat channel data (dataByte/dataShort/dataInt/random pools/indices) has
// one layout across IW3/IW4/IW5/T4/T5/T6, so BO1 tracks are carried over
// byte-for-byte. Only the asset header, the delta (root motion) records and
// the script-string bone/notetrack names are re-expressed for MW2.
//
// Header-only and free of engine state so the native tests can exercise it.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mod::bo1xanim {

inline constexpr std::uint32_t kT5Version=473;
inline constexpr std::size_t kT5HeaderBytes=36;         // XFile: size, externalSize, blockSize[7]
inline constexpr std::size_t kT5AssetListBytes=16;      // stringList{count,strings}, assetCount, assets
inline constexpr std::size_t kT5XAnimBytes=104;         // sizeof(T5 XAnimParts), 32-bit
inline constexpr std::size_t kMw2XAnimBytes=136;        // sizeof(MW2 x64 XAnimParts)
inline constexpr std::uint32_t kT5Inline=0xffffffffu;
inline constexpr int kPartTypeCount=10;
inline constexpr int kAssetTypeXAnim=4;                 // T5 ASSET_TYPE_XANIMPARTS

// MW2 x64 XAnimParts field offsets (verified against the retail zone stream
// and the native readers: numframes 0x12, notifyCount 0x1f, framerate 0x2c,
// notify 0x78).
namespace mw2 {
inline constexpr std::size_t kName=0x00,kDataByteCount=0x08,kDataShortCount=0x0a,kDataIntCount=0x0c,
    kRandomDataByteCount=0x0e,kRandomDataIntCount=0x10,kNumFrames=0x12,kFlags=0x14,kBoneCount=0x15,
    kNotifyCount=0x1f,kAssetType=0x20,kIsDefault=0x21,kRandomDataShortCount=0x24,kIndexCount=0x28,
    kFramerate=0x2c,kFrequency=0x30,kNames=0x38,kDataByte=0x40,kDataShort=0x48,kDataInt=0x50,
    kRandomDataShort=0x58,kRandomDataByte=0x60,kRandomDataInt=0x68,kIndices=0x70,kNotify=0x78,kDeltaPart=0x80;
inline constexpr std::uint8_t kFlagLoop=1,kFlagDelta=2;
inline constexpr std::size_t kNotifyStride=8;            // {u16 name; float time}
inline constexpr std::size_t kDeltaPartBytes=24;         // {trans*, quat2*, quat*}
inline constexpr std::size_t kTransUnion=8;              // XAnimPartTrans: size u16, smallTrans u8, u @8
inline constexpr std::size_t kQuatUnion=8;               // XAnimDeltaPartQuat2: size u16, u @8
} // namespace mw2

struct DeltaTrans {
    bool present=false;
    std::uint16_t size=0; std::uint8_t smallTrans=0;
    std::array<float,3> frame0{},mins{},step{};
    std::vector<std::uint8_t> indices;                   // (size+1) x 1 or 2 bytes
    std::vector<std::uint8_t> frames;                    // (size+1) x 3 (u8) or 6 (u16)
};
struct DeltaQuat {                                       // yaw-only XQuat2 root rotation
    bool present=false;
    std::uint16_t size=0;
    std::array<std::int16_t,2> frame0{};
    std::vector<std::uint8_t> indices;
    std::vector<std::uint8_t> frames;                    // (size+1) x 4
};
struct Anim {
    std::string name;
    std::uint16_t dataByteCount=0,dataShortCount=0,dataIntCount=0,randomDataByteCount=0,randomDataIntCount=0,numframes=0;
    bool loop=false,delta=false;
    std::array<std::uint8_t,kPartTypeCount> boneCount{};
    std::uint8_t assetType=0; bool isDefault=false;
    std::uint32_t randomDataShortCount=0,indexCount=0;
    float framerate=0.f,frequency=0.f;
    std::vector<std::string> bones;
    std::vector<std::pair<std::string,float>> notify;
    DeltaTrans trans; DeltaQuat quat;
    std::vector<std::uint8_t> dataByte,dataShort,dataInt,randomDataShort,randomDataByte,randomDataInt,indices;
    bool byteIndices() const {return numframes<256;}
    float lengthSeconds() const {return framerate>0.f?static_cast<float>(numframes)/framerate:0.f;}
    // Normalized time of the first notetrack with this name, or -1.
    float notetrack(std::string_view note) const {
        for(const auto& [n,t]:notify) if(n==note) return t;
        return -1.f;
    }
};

class Reader {
public:
    Reader(const std::uint8_t* data,std::size_t size,std::size_t at):d_(data),n_(size),p_(at) {}
    bool ok() const {return ok_;}
    std::size_t pos() const {return p_;}
    template<class T> T get() {
        T v{};
        if(!ok_ || p_+sizeof(T)>n_) {ok_=false;return v;}
        std::memcpy(&v,d_+p_,sizeof(T));p_+=sizeof(T);return v;
    }
    std::vector<std::uint8_t> take(std::size_t count) {
        if(!ok_ || count>n_-p_) {ok_=false;return {};}
        std::vector<std::uint8_t> out(d_+p_,d_+p_+count);p_+=count;return out;
    }
    void skip(std::size_t count) {if(!ok_ || count>n_-p_)ok_=false;else p_+=count;}
    std::string cstring(std::size_t maximum=256) {
        const auto* start=d_+p_;
        const auto limit=std::min(maximum,n_-p_);
        const auto* end=static_cast<const std::uint8_t*>(std::memchr(start,0,limit));
        if(!ok_ || !end) {ok_=false;return {};}
        std::string s(reinterpret_cast<const char*>(start),static_cast<std::size_t>(end-start));p_+=s.size()+1;return s;
    }
private:
    const std::uint8_t* d_; std::size_t n_; std::size_t p_; bool ok_=true;
};

struct ZoneStrings {
    std::vector<std::string> strings;
    std::uint32_t assetCount=0;
    std::size_t assetsOffset=0;
};

// XFile header + XAssetList script strings (T5 stores each inline, NUL-terminated).
inline std::optional<ZoneStrings> ParseZoneStrings(const std::uint8_t* raw,std::size_t size) {
    Reader r(raw,size,kT5HeaderBytes);
    const auto count=r.get<std::uint32_t>();const auto strings=r.get<std::uint32_t>();
    ZoneStrings z;z.assetCount=r.get<std::uint32_t>();const auto assets=r.get<std::uint32_t>();
    if(!r.ok() || strings!=kT5Inline || assets!=kT5Inline || count==0 || count>65535 || z.assetCount>200000) return std::nullopt;
    std::vector<std::uint32_t> pointers(count);
    for(auto& p:pointers) p=r.get<std::uint32_t>();
    z.strings.reserve(count);
    for(auto p:pointers) {
        if(p==0) {z.strings.emplace_back();continue;}
        if(p!=kT5Inline) return std::nullopt;
        z.strings.push_back(r.cstring());
    }
    if(!r.ok()) return std::nullopt;
    z.assetsOffset=r.pos();
    if(z.assetsOffset+static_cast<std::size_t>(z.assetCount)*8>size) return std::nullopt;
    return z;
}

namespace detail {
inline bool ParseTrans(Reader& r,std::uint16_t numframes,DeltaTrans& t) {
    t.present=true;t.size=r.get<std::uint16_t>();t.smallTrans=r.get<std::uint8_t>();r.skip(1);
    if(t.size==0) {for(auto& v:t.frame0)v=r.get<float>();return r.ok();}
    for(auto& v:t.mins)v=r.get<float>();
    for(auto& v:t.step)v=r.get<float>();
    const auto frames=r.get<std::uint32_t>();
    const std::size_t count=static_cast<std::size_t>(t.size)+1;
    t.indices=r.take(count*(numframes<256?1:2));
    if(frames==kT5Inline) t.frames=r.take(count*(t.smallTrans?3:6));
    else if(frames!=0) return false;
    return r.ok() && !t.frames.empty();
}
inline bool ParseQuat(Reader& r,std::uint16_t numframes,DeltaQuat& q) {
    q.present=true;q.size=r.get<std::uint16_t>();r.skip(2);
    if(q.size==0) {for(auto& v:q.frame0)v=r.get<std::int16_t>();return r.ok();}
    const auto frames=r.get<std::uint32_t>();
    const std::size_t count=static_cast<std::size_t>(q.size)+1;
    q.indices=r.take(count*(numframes<256?1:2));
    if(frames==kT5Inline) q.frames=r.take(count*4);
    else if(frames!=0) return false;
    return r.ok() && !q.frames.empty();
}
} // namespace detail

// Parse one T5 XAnimParts whose 104-byte header starts at `at`. Every pointer
// must be inline (-1) or null, as in a fastfile's XFILE_BLOCK_TEMP stream.
inline std::optional<Anim> ParseAnim(const std::uint8_t* raw,std::size_t size,std::size_t at,const std::vector<std::string>& strings,std::size_t* end=nullptr) {
    Reader r(raw,size,at);
    Anim a;
    if(r.get<std::uint32_t>()!=kT5Inline) return std::nullopt;
    a.dataByteCount=r.get<std::uint16_t>();a.dataShortCount=r.get<std::uint16_t>();a.dataIntCount=r.get<std::uint16_t>();
    a.randomDataByteCount=r.get<std::uint16_t>();a.randomDataIntCount=r.get<std::uint16_t>();a.numframes=r.get<std::uint16_t>();
    a.loop=r.get<std::uint8_t>()!=0;a.delta=r.get<std::uint8_t>()!=0;
    const auto leftHandIk=r.get<std::uint8_t>();const auto streamable=r.get<std::uint8_t>();(void)leftHandIk;
    const auto streamed=r.get<std::uint32_t>();
    for(auto& b:a.boneCount)b=r.get<std::uint8_t>();
    const auto notifyCount=r.get<std::uint8_t>();a.assetType=r.get<std::uint8_t>();a.isDefault=r.get<std::uint8_t>()!=0;r.skip(3);
    a.randomDataShortCount=r.get<std::uint32_t>();a.indexCount=r.get<std::uint32_t>();
    a.framerate=r.get<float>();a.frequency=r.get<float>();r.get<float>();r.get<float>();   // primedLength, loopEntryTime
    std::array<std::uint32_t,10> p{};for(auto& v:p)v=r.get<std::uint32_t>();
    enum {Names,DataByte,DataShort,DataInt,RandomDataShort,RandomDataByte,RandomDataInt,Indices,Notify,DeltaPart};
    if(!r.ok() || streamable || streamed) return std::nullopt;
    for(auto v:p) if(v!=0 && v!=kT5Inline) return std::nullopt;
    int quats=0,trans=0;for(int n=0;n<5;++n)quats+=a.boneCount[static_cast<std::size_t>(n)];
    for(int n=5;n<9;++n)trans+=a.boneCount[static_cast<std::size_t>(n)];
    const int bones=a.boneCount[9];
    if(quats!=bones || trans!=bones || !(a.framerate>0.f && a.framerate<=1000.f) || a.numframes==0) return std::nullopt;
    a.name=r.cstring(128);
    if(!r.ok() || a.name.empty()) return std::nullopt;
    if(p[Names]==kT5Inline) {
        a.bones.reserve(static_cast<std::size_t>(bones));
        for(int n=0;n<bones;++n) {
            const auto id=r.get<std::uint16_t>();
            if(id>=strings.size()) return std::nullopt;
            a.bones.push_back(strings[id]);
        }
    } else if(bones) return std::nullopt;
    if(p[Notify]==kT5Inline) {
        for(int n=0;n<notifyCount;++n) {
            const auto id=r.get<std::uint16_t>();r.skip(2);const auto time=r.get<float>();
            if(id>=strings.size()) return std::nullopt;
            a.notify.emplace_back(strings[id],time);
        }
    } else if(notifyCount) return std::nullopt;
    if(p[DeltaPart]==kT5Inline) {
        const auto trans=r.get<std::uint32_t>(),quat=r.get<std::uint32_t>();
        if((trans!=0 && trans!=kT5Inline) || (quat!=0 && quat!=kT5Inline)) return std::nullopt;
        if(trans==kT5Inline && !detail::ParseTrans(r,a.numframes,a.trans)) return std::nullopt;
        if(quat==kT5Inline && !detail::ParseQuat(r,a.numframes,a.quat)) return std::nullopt;
    }
    auto pool=[&](int which,std::size_t count,std::size_t width,std::vector<std::uint8_t>& out) {
        if(p[static_cast<std::size_t>(which)]==kT5Inline) out=r.take(count*width);
        else if(count) return false;
        return r.ok();
    };
    if(!pool(DataByte,a.dataByteCount,1,a.dataByte) || !pool(DataShort,a.dataShortCount,2,a.dataShort)
        || !pool(DataInt,a.dataIntCount,4,a.dataInt) || !pool(RandomDataShort,a.randomDataShortCount,2,a.randomDataShort)
        || !pool(RandomDataByte,a.randomDataByteCount,1,a.randomDataByte) || !pool(RandomDataInt,a.randomDataIntCount,4,a.randomDataInt)
        || !pool(Indices,a.indexCount,a.byteIndices()?1:2,a.indices)) return std::nullopt;
    if(end)*end=r.pos();
    return a;
}

// Consume the flat pools in the engine's reader order; true when every pool
// is exactly exhausted (the strongest structural check without decoding).
inline bool ValidateFlat(const Anim& a) {
    std::size_t b=0,s=0,i=0,rs=0,rb=0,ix=0;
    const std::size_t nb=a.dataByte.size(),ns=a.dataShort.size()/2,ni=a.dataInt.size()/4;
    const std::size_t nrs=a.randomDataShort.size()/2,nrb=a.randomDataByte.size(),nix=a.indices.size()/(a.byteIndices()?1:2);
    bool ok=true;
    auto popByte=[&]{if(b>=nb){ok=false;return std::uint8_t{0};}return a.dataByte[b++];};
    auto popShort=[&]{
        if(s>=ns){ok=false;return std::uint16_t{0};}
        std::uint16_t v=0;std::memcpy(&v,a.dataShort.data()+2*s,2);++s;return v;
    };
    auto packed=[&](std::uint16_t stored) {
        const std::size_t count=static_cast<std::size_t>(stored)+1;
        if(a.byteIndices()) b+=count;
        else if(stored>=64) {ix+=count;s+=((count-2)/256)+2;}
        else s+=count;
    };
    const auto& bc=a.boneCount;
    for(int n=0;n<bc[1] && ok;++n){const auto st=popShort();packed(st);rs+=(static_cast<std::size_t>(st)+1)*2;}
    for(int n=0;n<bc[2] && ok;++n){const auto st=popShort();packed(st);rs+=(static_cast<std::size_t>(st)+1)*4;}
    s+=2u*bc[3];s+=4u*bc[4];
    for(int n=0;n<bc[5] && ok;++n){popByte();const auto st=popShort();i+=6;packed(st);rb+=(static_cast<std::size_t>(st)+1)*3;}
    for(int n=0;n<bc[6] && ok;++n){popByte();const auto st=popShort();i+=6;packed(st);rs+=(static_cast<std::size_t>(st)+1)*3;}
    for(int n=0;n<bc[7] && ok;++n){popByte();i+=3;}
    for(int n=0;n<bc[8] && ok;++n)popByte();
    return ok && b==nb && s==ns && i==ni && rs==nrs && rb==nrb && ix==nix && a.randomDataInt.empty();
}

// One pass over the decompressed zone: every inline XAnimParts header whose
// name is wanted is parsed and validated. Returns the anims found.
inline std::vector<Anim> CarveAnims(const std::uint8_t* raw,std::size_t size,const ZoneStrings& zone,
                                    const std::unordered_set<std::string>& wanted,std::size_t* rejected=nullptr) {
    std::vector<Anim> found;std::unordered_set<std::string> seen;
    if(size<kT5XAnimBytes+2) return found;
    for(std::size_t at=zone.assetsOffset;at+kT5XAnimBytes<size;++at) {
        if(raw[at]!=0xff || raw[at+1]!=0xff || raw[at+2]!=0xff || raw[at+3]!=0xff) continue;
        const auto* nameAt=raw+at+kT5XAnimBytes;
        const auto limit=std::min<std::size_t>(128,size-at-kT5XAnimBytes);
        const auto* nul=static_cast<const std::uint8_t*>(std::memchr(nameAt,0,limit));
        if(!nul || nul==nameAt) continue;
        const std::string name(reinterpret_cast<const char*>(nameAt),static_cast<std::size_t>(nul-nameAt));
        if(!wanted.count(name) || seen.count(name)) continue;
        auto anim=ParseAnim(raw,size,at,zone.strings);
        if(!anim || anim->name!=name || !ValidateFlat(*anim)) {if(rejected)++*rejected;continue;}
        seen.insert(name);found.push_back(std::move(*anim));
        if(seen.size()==wanted.size()) break;
    }
    return found;
}

// ---- MW2 x64 image ---------------------------------------------------------
// Owned storage for one converted anim; pointers inside `header` refer into
// the vectors, so an Image must not be copied or moved after Build().
struct Image {
    alignas(16) std::array<std::uint8_t,kMw2XAnimBytes> header{};
    std::vector<std::uint16_t> names;
    std::vector<std::uint8_t> notify;
    alignas(16) std::array<std::uint8_t,mw2::kDeltaPartBytes> deltaPart{};
    std::vector<std::uint8_t> trans,quat2;
    std::vector<std::uint8_t> transFrames,quatFrames;
    std::vector<std::uint8_t> dataByte,dataShort,dataInt,randomDataShort,randomDataByte,indices;
    Image()=default;Image(const Image&)=delete;Image& operator=(const Image&)=delete;
};

template<class T> inline void Put(std::uint8_t* at,T value) {std::memcpy(at,&value,sizeof(T));}
template<class T> inline T Get(const std::uint8_t* at) {T v{};std::memcpy(&v,at,sizeof(T));return v;}
inline std::uint64_t Address(const void* p) {return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p));}
inline std::uint64_t VectorAddress(const std::vector<std::uint8_t>& v) {return v.empty()?0:Address(v.data());}

// The MW2 delta flag: set only when there is root motion to sample.
inline bool HasDelta(const Anim& a) {return a.delta && (a.trans.present || a.quat.present);}

// Build the MW2 XAnimParts image for `a`. `intern` returns the MW2 script
// string for a name (0 refuses). `nameField` is the asset name pointer to
// keep (the MW2 asset's own name so database lookups stay unchanged).
inline bool Build(const Anim& a,const std::function<std::uint16_t(const std::string&)>& intern,std::uint64_t nameField,Image& out) {
    if(a.bones.size()!=a.boneCount[9] || a.notify.size()>255 || !ValidateFlat(a)) return false;
    out.names.clear();out.names.reserve(a.bones.size());
    for(const auto& bone:a.bones) {const auto id=intern(bone);if(!id)return false;out.names.push_back(id);}
    out.notify.assign(a.notify.size()*mw2::kNotifyStride,0);
    for(std::size_t n=0;n<a.notify.size();++n) {
        const auto id=intern(a.notify[n].first);if(!id)return false;
        Put<std::uint16_t>(out.notify.data()+n*mw2::kNotifyStride,id);
        Put<float>(out.notify.data()+n*mw2::kNotifyStride+4,a.notify[n].second);
    }
    out.dataByte=a.dataByte;out.dataShort=a.dataShort;out.dataInt=a.dataInt;
    out.randomDataShort=a.randomDataShort;out.randomDataByte=a.randomDataByte;out.indices=a.indices;
    out.deltaPart.fill(0);out.trans.clear();out.quat2.clear();out.transFrames.clear();out.quatFrames.clear();
    if(a.trans.present) {
        const auto& t=a.trans;
        if(t.size==0) {
            out.trans.assign(mw2::kTransUnion+12,0);
            Put<std::uint16_t>(out.trans.data(),0);out.trans[2]=t.smallTrans;
            std::memcpy(out.trans.data()+mw2::kTransUnion,t.frame0.data(),12);
        } else {
            out.transFrames=t.frames;
            out.trans.assign(mw2::kTransUnion+32+t.indices.size(),0);
            Put<std::uint16_t>(out.trans.data(),t.size);out.trans[2]=t.smallTrans;
            std::memcpy(out.trans.data()+mw2::kTransUnion,t.mins.data(),12);
            std::memcpy(out.trans.data()+mw2::kTransUnion+12,t.step.data(),12);
            Put<std::uint64_t>(out.trans.data()+mw2::kTransUnion+24,VectorAddress(out.transFrames));
            std::memcpy(out.trans.data()+mw2::kTransUnion+32,t.indices.data(),t.indices.size());
        }
        out.trans.resize((out.trans.size()+15)&~std::size_t(15),0);
        Put<std::uint64_t>(out.deltaPart.data(),VectorAddress(out.trans));
    }
    if(a.quat.present) {
        const auto& q=a.quat;
        if(q.size==0) {
            out.quat2.assign(mw2::kQuatUnion+4,0);
            std::memcpy(out.quat2.data()+mw2::kQuatUnion,q.frame0.data(),4);
        } else {
            out.quatFrames=q.frames;
            out.quat2.assign(mw2::kQuatUnion+8+q.indices.size(),0);
            Put<std::uint16_t>(out.quat2.data(),q.size);
            Put<std::uint64_t>(out.quat2.data()+mw2::kQuatUnion,VectorAddress(out.quatFrames));
            std::memcpy(out.quat2.data()+mw2::kQuatUnion+8,q.indices.data(),q.indices.size());
        }
        out.quat2.resize((out.quat2.size()+15)&~std::size_t(15),0);
        Put<std::uint64_t>(out.deltaPart.data()+8,VectorAddress(out.quat2));
    }
    auto& h=out.header;h.fill(0);
    Put<std::uint64_t>(h.data()+mw2::kName,nameField);
    Put<std::uint16_t>(h.data()+mw2::kDataByteCount,a.dataByteCount);
    Put<std::uint16_t>(h.data()+mw2::kDataShortCount,a.dataShortCount);
    Put<std::uint16_t>(h.data()+mw2::kDataIntCount,a.dataIntCount);
    Put<std::uint16_t>(h.data()+mw2::kRandomDataByteCount,a.randomDataByteCount);
    Put<std::uint16_t>(h.data()+mw2::kRandomDataIntCount,a.randomDataIntCount);
    Put<std::uint16_t>(h.data()+mw2::kNumFrames,a.numframes);
    // Some BO1 anims (e.g. ai_zombie_attack_v1/v4/v6) carry the delta flag
    // with no delta part at all. BO1 tests the pointer; MW2's root-motion
    // sampler (XAnimCalcRelDeltaParts 0x29b200) tests only the flag and then
    // reads deltaPart, so such an anim is written as a plain (non-delta) one:
    // the same "no root motion" BO1 plays (Test41 freeze/crash on attacks).
    h[mw2::kFlags]=static_cast<std::uint8_t>((a.loop?mw2::kFlagLoop:0)|(HasDelta(a)?mw2::kFlagDelta:0));
    for(std::size_t n=0;n<kPartTypeCount;++n)h[mw2::kBoneCount+n]=a.boneCount[n];
    h[mw2::kNotifyCount]=static_cast<std::uint8_t>(a.notify.size());
    h[mw2::kAssetType]=a.assetType;h[mw2::kIsDefault]=0;
    Put<std::uint32_t>(h.data()+mw2::kRandomDataShortCount,a.randomDataShortCount);
    Put<std::uint32_t>(h.data()+mw2::kIndexCount,a.indexCount);
    Put<float>(h.data()+mw2::kFramerate,a.framerate);
    Put<float>(h.data()+mw2::kFrequency,a.frequency);
    Put<std::uint64_t>(h.data()+mw2::kNames,out.names.empty()?0:Address(out.names.data()));
    Put<std::uint64_t>(h.data()+mw2::kDataByte,VectorAddress(out.dataByte));
    Put<std::uint64_t>(h.data()+mw2::kDataShort,VectorAddress(out.dataShort));
    Put<std::uint64_t>(h.data()+mw2::kDataInt,VectorAddress(out.dataInt));
    Put<std::uint64_t>(h.data()+mw2::kRandomDataShort,VectorAddress(out.randomDataShort));
    Put<std::uint64_t>(h.data()+mw2::kRandomDataByte,VectorAddress(out.randomDataByte));
    Put<std::uint64_t>(h.data()+mw2::kRandomDataInt,0);
    Put<std::uint64_t>(h.data()+mw2::kIndices,VectorAddress(out.indices));
    Put<std::uint64_t>(h.data()+mw2::kNotify,VectorAddress(out.notify));
    Put<std::uint64_t>(h.data()+mw2::kDeltaPart,(a.trans.present || a.quat.present)?Address(out.deltaPart.data()):0);
    return true;
}

// Reads a built header back the way the engine does and checks it against
// the source (used by the native tests and as a runtime self-check).
inline bool ImageMatches(const Image& image,const Anim& a) {
    const auto* h=image.header.data();
    if(Get<std::uint16_t>(h+mw2::kNumFrames)!=a.numframes || Get<float>(h+mw2::kFramerate)!=a.framerate) return false;
    if(h[mw2::kNotifyCount]!=a.notify.size() || h[mw2::kBoneCount+9]!=a.boneCount[9]) return false;
    if(Get<std::uint64_t>(h+mw2::kNames)!=(image.names.empty()?0:Address(image.names.data()))) return false;
    if(Get<std::uint64_t>(h+mw2::kNotify)!=VectorAddress(image.notify)) return false;
    // The engine reads deltaPart whenever the delta flag is set.
    if((h[mw2::kFlags]&mw2::kFlagDelta) && !Get<std::uint64_t>(h+mw2::kDeltaPart)) return false;
    return Get<std::uint32_t>(h+mw2::kRandomDataShortCount)==a.randomDataShort.size()/2;
}

} // namespace mod::bo1xanim
