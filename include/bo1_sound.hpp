#pragma once
// Test43: Black Ops zombie sounds for MW2.
//
// Reads Black Ops sound banks (T5 SndBank: alias lists, aliases, loaded and
// streamed sounds) from the owner's own fastfiles, decodes the sounds to
// 16-bit PCM (xWMA through third_party/wmadec, MS-ADPCM, PCM) and writes a
// retail x64 MW2 fastfile of MW2 sound alias lists that carry them as MW2
// loaded sounds. MW2's loader and sound system do the rest. Nothing from
// Black Ops is shipped: the fastfile is generated on the owner's machine.
//
// T5 PC SndBank layout (as stored in the zone stream):
//   SndBank            40 bytes  name, aliasCount, alias*, aliasIndex*, ...
//   snd_alias_list_t   20 bytes  name*, id, head*, count, sequence
//   snd_alias_t        84 bytes  name hash, id, subtitle*, secondaryname*,
//                                soundFile*, flags, duck, contextType,
//                                contextValue, 16 x u16, 16 x s8
//   SoundFile           8 bytes  u*, type (1 loaded, 2 streamed), exists
//   LoadedSound        60 bytes  name*, snd_asset (14 x u32)
// Aliases that share a sound point back at the first SoundFile with a zone
// pointer ((block << 29 | offset) + 1); the parser replays the virtual block
// to resolve them.
//
// MW2 x64 side, following the retail loaders: Load_snd_alias_list_ptr
// 0xe9eb0, Load_snd_alias_list_t 0xe9f70, Load_snd_alias_tArray 0xea050,
// Load_SoundFile 0xe2680, Load_LoadedSoundPtr 0xe0320, Load_LoadedSound
// 0xe0280, Load_snd (data) 0xe6bc0, Load_SndCurvePtr 0xe25c0, Load_SndCurve
// 0xe2530, Load_SpeakerMap 0xe2700.
//
// Header-only and free of engine state so the native tests can exercise it.
#include "bo1_model.hpp"
#include "../third_party/wmadec/wmadec.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mod::bo1sound {
using bo1xanim::Get;
using bo1xanim::Put;

// ---------------------------------------------------------------- T5 input
namespace t5 {
inline constexpr std::uint32_t kInline=0xffffffffu,kInsert=0xfffffffeu;
inline bool IsInline(std::uint32_t p) {return p==kInline || p==kInsert;}
inline constexpr std::size_t kBankBytes=40,kListBytes=20,kAliasBytes=84,kSoundFileBytes=8,kLoadedSoundBytes=60,
    kSndAssetBytes=56,kStreamedBytes=8,kPrimedBytes=12;
inline constexpr std::uint32_t kVirtualBlock=4;
enum Format : std::uint32_t {Pcm16=0,Pcm24=1,Pcm32=2,Ieee=3,Xma4=4,Mp3=5,MsAdpcm=6,Wma=7};
// snd_alias_t flags: bit 0 loops, bit 1 positional (3D).
inline constexpr std::uint32_t kFlagLooping=1u,kFlag3d=2u;
} // namespace t5

struct SndAsset {
    std::uint32_t version=0,frames=0,rate=0,channels=0,headerSize=0,blockSize=0,bufferSize=0,format=0,channelFlags=0,flags=0;
    std::vector<std::uint32_t> seek;
    std::size_t data=0,dataSize=0;       // offset of the data in its source buffer
    bool dataInline=false;
};
struct SoundRef {
    enum class Kind {None,Loaded,Streamed,Unresolved};
    Kind kind=Kind::None;
    std::string file;                    // T5 file name, e.g. sound\vox\zmb\...\x.wav
    SndAsset asset;                      // Loaded only
};
struct Alias {
    std::uint32_t nameHash=0,id=0,flags=0;
    std::array<std::uint16_t,16> u16{};  // fluxTime, startDelay, reverbSend, centerSend, volMin, volMax,
                                         // teamVolMod, pitchMin, pitchMax, teamPitchMod, distMin, distMax, ...
    std::array<std::int8_t,16> c8{};
    std::string subtitle;
    SoundRef sound;
    float volMin() const {return u16[4]/65535.f;}
    float volMax() const {return u16[5]/65535.f;}
    float pitchMin() const {return u16[7]/32767.f;}
    float pitchMax() const {return u16[8]/32767.f;}
    float distMin() const {return static_cast<float>(u16[10]);}
    float distMax() const {return static_cast<float>(u16[11]);}
    int startDelay() const {return u16[1];}
    bool looping() const {return (flags&t5::kFlagLooping)!=0;}
    bool positional() const {return (flags&t5::kFlag3d)!=0;}
};
struct AliasList {
    std::string name;
    std::uint32_t id=0;
    int sequence=0;
    std::vector<Alias> aliases;
};
struct Bank {
    std::string name;
    std::vector<AliasList> lists;
    std::size_t end=0;                   // stream position after the alias lists
    unsigned loaded=0,streamed=0,shared=0,unresolved=0;
};

namespace detail {
inline bool Fail(std::string* error,const std::string& text) {if(error)*error=text;return false;}

// Replays the T5 virtual block while the bank is read so zone pointers
// (block << 29 | offset) + 1 can be matched with what was read earlier.
class BankReader {
public:
    BankReader(const std::uint8_t* raw,std::size_t size,std::size_t at):raw_(raw),size_(size),pos_(at) {}
    bool ok() const {return ok_;}
    std::size_t pos() const {return pos_;}
    std::uint64_t cursor() const {return cur_;}
    // Bytes of a block allocation; `counted` false for data in another block.
    const std::uint8_t* take(std::size_t n,std::size_t align,bool counted=true) {
        if(!ok_ || n>size_-pos_) {ok_=false;return nullptr;}
        if(counted) {cur_=(cur_+align-1)&~static_cast<std::uint64_t>(align-1);last_=cur_;cur_+=n;}
        const auto* p=raw_+pos_;pos_+=n;return p;
    }
    std::uint64_t lastAddress() const {return last_;}
    std::string cstring() {
        if(!ok_) return {};
        const auto* start=raw_+pos_;
        const auto* end=static_cast<const std::uint8_t*>(std::memchr(start,0,std::min<std::size_t>(size_-pos_,4096)));
        if(!end) {ok_=false;return {};}
        const std::size_t n=static_cast<std::size_t>(end-start)+1;
        std::string s(reinterpret_cast<const char*>(start),n-1);pos_+=n;cur_+=n;return s;
    }
private:
    const std::uint8_t* raw_;std::size_t size_,pos_;
    std::uint64_t cur_=0,last_=0;
    bool ok_=true;
};

inline SndAsset ReadSndAsset(const std::uint8_t* p) {
    SndAsset a;
    a.version=Get<std::uint32_t>(p);a.frames=Get<std::uint32_t>(p+4);a.rate=Get<std::uint32_t>(p+8);a.channels=Get<std::uint32_t>(p+12);
    a.headerSize=Get<std::uint32_t>(p+16);a.blockSize=Get<std::uint32_t>(p+20);a.bufferSize=Get<std::uint32_t>(p+24);
    a.format=Get<std::uint32_t>(p+28);a.channelFlags=Get<std::uint32_t>(p+32);a.flags=Get<std::uint32_t>(p+36);
    a.seek.resize(Get<std::uint32_t>(p+40));
    a.dataSize=Get<std::uint32_t>(p+48);
    return a;
}
inline bool SaneAsset(const SndAsset& a) {
    return a.version==1 && a.format<=t5::Wma && a.channels>=1 && a.channels<=2 && a.rate>=4000 && a.rate<=96000
        && a.frames>0 && a.frames<(1u<<26) && a.seek.size()<(1u<<20);
}
} // namespace detail

// The stream position of the SndBank called `name` (its 40-byte header).
inline std::optional<std::size_t> FindBank(const std::uint8_t* raw,std::size_t size,const std::string& name) {
    const std::string needle=name+'\0';
    std::size_t from=0;
    while(from+needle.size()<=size) {
        const auto* hit=std::search(raw+from,raw+size,needle.begin(),needle.end());
        if(hit==raw+size) break;
        const auto at=static_cast<std::size_t>(hit-raw);
        if(at>=t5::kBankBytes) {
            const auto* h=raw+at-t5::kBankBytes;
            if(t5::IsInline(Get<std::uint32_t>(h)) && Get<std::uint32_t>(h+4)>0 && Get<std::uint32_t>(h+4)<65536
               && t5::IsInline(Get<std::uint32_t>(h+8))) return at-t5::kBankBytes;
        }
        from=at+1;
    }
    return std::nullopt;
}

// Reads the bank at `at` (from FindBank) and resolves shared sounds.
inline bool ParseBank(const std::uint8_t* raw,std::size_t size,std::size_t at,Bank& out,std::string* error=nullptr) {
    using detail::Fail;
    out=Bank{};
    detail::BankReader r(raw,size,at);
    const auto* h=r.take(t5::kBankBytes,4);
    if(!h) return Fail(error,"truncated sound bank");
    const auto count=Get<std::uint32_t>(h+4);
    if(!t5::IsInline(Get<std::uint32_t>(h)) || !t5::IsInline(Get<std::uint32_t>(h+8)) || !count || count>65535)
        return Fail(error,"not a sound bank header");
    out.name=r.cstring();
    const auto* listBytes=r.take(t5::kListBytes*count,4);
    if(!listBytes) return Fail(error,"truncated alias lists");
    std::vector<std::uint8_t> lists(listBytes,listBytes+t5::kListBytes*count);
    struct Pending {std::size_t list,alias;std::uint32_t ref;bool loadedSound;std::uint64_t address;};
    std::vector<Pending> pending;
    std::unordered_map<std::uint64_t,SoundRef> soundFiles,loadedSounds;   // by virtual address
    out.lists.resize(count);
    for(std::uint32_t n=0;n<count;++n) {
        const auto* l=lists.data()+t5::kListBytes*n;
        auto& list=out.lists[n];
        list.id=Get<std::uint32_t>(l+4);list.sequence=Get<std::int32_t>(l+16);
        if(t5::IsInline(Get<std::uint32_t>(l))) list.name=r.cstring();
        const auto head=Get<std::uint32_t>(l+8);
        const auto aliasCount=Get<std::uint32_t>(l+12);
        if(!t5::IsInline(head)) {if(head) ++out.unresolved;continue;}
        if(aliasCount>4096) return Fail(error,"alias list too long");
        const auto* arr=r.take(t5::kAliasBytes*aliasCount,4);
        if(!arr) return Fail(error,"truncated aliases");
        std::vector<std::uint8_t> aliases(arr,arr+t5::kAliasBytes*aliasCount);
        list.aliases.resize(aliasCount);
        for(std::uint32_t k=0;k<aliasCount;++k) {
            const auto* a=aliases.data()+t5::kAliasBytes*k;
            auto& alias=list.aliases[k];
            alias.nameHash=Get<std::uint32_t>(a);alias.id=Get<std::uint32_t>(a+4);alias.flags=Get<std::uint32_t>(a+20);
            for(std::size_t i=0;i<16;++i) alias.u16[i]=Get<std::uint16_t>(a+36+2*i);
            for(std::size_t i=0;i<16;++i) alias.c8[i]=static_cast<std::int8_t>(a[68+i]);
            if(t5::IsInline(Get<std::uint32_t>(a+8))) alias.subtitle=r.cstring();
            if(t5::IsInline(Get<std::uint32_t>(a+12))) r.cstring();
            const auto sf=Get<std::uint32_t>(a+16);
            if(!sf) continue;
            if(!t5::IsInline(sf)) {pending.push_back({n,k,sf,false,0});continue;}
            const auto* file=r.take(t5::kSoundFileBytes,4);
            if(!file) return Fail(error,"truncated sound file");
            const auto address=r.lastAddress();
            const auto u=Get<std::uint32_t>(file);const auto type=file[4];
            SoundRef ref;
            if(type==1 && t5::IsInline(u)) {
                const auto* ls=r.take(t5::kLoadedSoundBytes,4);
                if(!ls) return Fail(error,"truncated loaded sound");
                const auto lsAddress=r.lastAddress();
                ref.kind=SoundRef::Kind::Loaded;
                ref.asset=detail::ReadSndAsset(ls+4);
                if(t5::IsInline(Get<std::uint32_t>(ls))) ref.file=r.cstring();
                const auto seekPtr=Get<std::uint32_t>(ls+4+44);
                if(t5::IsInline(seekPtr) && !ref.asset.seek.empty()) {
                    const auto* s=r.take(4*ref.asset.seek.size(),4);
                    if(!s) return Fail(error,"truncated seek table");
                    for(std::size_t i=0;i<ref.asset.seek.size();++i) ref.asset.seek[i]=Get<std::uint32_t>(s+4*i);
                } else if(!t5::IsInline(seekPtr)) ref.asset.seek.clear();
                const auto dataPtr=Get<std::uint32_t>(ls+4+52);
                if(t5::IsInline(dataPtr)) {
                    ref.asset.data=r.pos();ref.asset.dataInline=true;
                    if(!r.take(ref.asset.dataSize,2048,false)) return Fail(error,"truncated sound data");
                }
                if(!detail::SaneAsset(ref.asset)) return Fail(error,"unexpected loaded sound header ("+ref.file+")");
                loadedSounds[lsAddress]=ref;
                ++out.loaded;
            } else if(type==1 && u) {
                pending.push_back({n,k,u,true,address});
                ref.kind=SoundRef::Kind::Unresolved;   // filled in after resolution
                soundFiles[address]=ref;
                alias.sound=ref;
                continue;
            } else if(type==2 && t5::IsInline(u)) {
                const auto* ss=r.take(t5::kStreamedBytes,4);
                if(!ss) return Fail(error,"truncated streamed sound");
                ref.kind=SoundRef::Kind::Streamed;
                if(t5::IsInline(Get<std::uint32_t>(ss))) ref.file=r.cstring();
                const auto prime=Get<std::uint32_t>(ss+4);
                if(t5::IsInline(prime)) {
                    const auto* ps=r.take(t5::kPrimedBytes,4);
                    if(!ps) return Fail(error,"truncated primed sound");
                    if(t5::IsInline(Get<std::uint32_t>(ps))) r.cstring();
                    if(t5::IsInline(Get<std::uint32_t>(ps+4)) && !r.take(Get<std::uint32_t>(ps+8),2048,false))
                        return Fail(error,"truncated primed data");
                }
                ++out.streamed;
            }
            soundFiles[address]=ref;
            alias.sound=ref;
        }
        if(!r.ok()) return Fail(error,"sound bank read failed");
    }
    out.end=r.pos();
    // Shared sounds: (block << 29 | offset) + 1 in the virtual block, which
    // began at an unknown base. The base is the one offset that maps every
    // reference onto a sound file (or loaded sound) read above.
    if(!pending.empty()) {
        auto offsetOf=[](std::uint32_t p){return static_cast<std::int64_t>((p-1)&0x1fffffffu);};
        auto blockOf=[](std::uint32_t p){return (p-1)>>29;};
        // Candidate bases from the first reference; each is scored on up to
        // 64 references of both kinds and the best one is kept.
        std::optional<std::int64_t> base;
        {
            const auto& first=pending.front();
            const auto& firstTable=first.loadedSound?loadedSounds:soundFiles;
            std::vector<const Pending*> sample;
            const std::size_t stride=std::max<std::size_t>(1,pending.size()/64);
            for(std::size_t i=0;i<pending.size() && sample.size()<64;i+=stride) sample.push_back(&pending[i]);
            std::size_t bestHits=0;
            for(const auto& [a,ref]:firstTable) {
                (void)ref;
                const auto candidate=offsetOf(first.ref)-static_cast<std::int64_t>(a);
                std::size_t hits=0;
                for(const auto* p:sample) {
                    const auto at=offsetOf(p->ref)-candidate;
                    const auto& table=p->loadedSound?loadedSounds:soundFiles;
                    if(at>=0 && blockOf(p->ref)==t5::kVirtualBlock && table.count(static_cast<std::uint64_t>(at))) ++hits;
                }
                if(hits>bestHits) {bestHits=hits;base=candidate;}
            }
        }
        auto resolve=[&](const Pending& p)->SoundRef {
            SoundRef ref;ref.kind=SoundRef::Kind::Unresolved;
            if(!base || blockOf(p.ref)!=t5::kVirtualBlock) return ref;
            const auto at=offsetOf(p.ref)-*base;
            const auto& table=p.loadedSound?loadedSounds:soundFiles;
            const auto found=at>=0?table.find(static_cast<std::uint64_t>(at)):table.end();
            if(found!=table.end() && found->second.kind!=SoundRef::Kind::None) ref=found->second;
            return ref;
        };
        // Shared loaded sounds first: the sound files holding them may be shared too.
        for(const bool loaded:{true,false}) for(const auto& p:pending) {
            if(p.loadedSound!=loaded) continue;
            const auto ref=resolve(p);
            out.lists[p.list].aliases[p.alias].sound=ref;
            if(loaded) soundFiles[p.address]=ref;
            if(ref.kind==SoundRef::Kind::Unresolved) ++out.unresolved;
            else ++out.shared;
        }
    }
    return true;
}

// -------------------------------------------------------------- decoding
struct Pcm {
    int channels=0,rate=0;
    std::vector<std::int16_t> samples;   // interleaved
    std::size_t frames() const {return channels?samples.size()/static_cast<std::size_t>(channels):0;}
};

namespace detail {
// MS-ADPCM as Black Ops stores it: 512 samples per block, 7 header bytes and
// 255 data bytes per channel.
inline bool DecodeMsAdpcm(const std::uint8_t* data,std::size_t size,int channels,std::size_t frames,std::vector<std::int16_t>& out) {
    static constexpr int kCoef1[7]={256,512,0,192,240,460,392},kCoef2[7]={0,-256,0,64,0,-208,-232};
    static constexpr int kAdapt[16]={230,230,230,230,307,409,512,614,768,614,512,409,307,230,230,230};
    const std::size_t blockAlign=static_cast<std::size_t>(262*channels),perBlock=512;
    out.clear();out.reserve(frames*static_cast<std::size_t>(channels));
    for(std::size_t at=0;at+blockAlign<=size && out.size()<frames*static_cast<std::size_t>(channels);at+=blockAlign) {
        const auto* b=data+at;
        int c1[2]{},c2[2]{},delta[2]{},s1[2]{},s2[2]{};
        for(int c=0;c<channels;++c) {
            const int pred=b[c];
            if(pred>6) return false;
            c1[c]=kCoef1[pred];c2[c]=kCoef2[pred];
        }
        const auto* p=b+channels;
        for(int c=0;c<channels;++c) delta[c]=Get<std::int16_t>(p+2*c);
        p+=2*channels;
        for(int c=0;c<channels;++c) s1[c]=Get<std::int16_t>(p+2*c);
        p+=2*channels;
        for(int c=0;c<channels;++c) s2[c]=Get<std::int16_t>(p+2*c);
        p+=2*channels;
        for(int c=0;c<channels;++c) out.push_back(static_cast<std::int16_t>(s2[c]));
        for(int c=0;c<channels;++c) out.push_back(static_cast<std::int16_t>(s1[c]));
        auto expand=[&](int c,int nibble) {
            int predictor=(s1[c]*c1[c]+s2[c]*c2[c])/256;
            predictor+=(nibble&8?nibble-16:nibble)*delta[c];
            s2[c]=s1[c];s1[c]=std::clamp(predictor,-32768,32767);
            delta[c]=(kAdapt[nibble]*delta[c])>>8;
            if(delta[c]<16) delta[c]=16;
            out.push_back(static_cast<std::int16_t>(s1[c]));
        };
        const std::size_t bytes=(perBlock-2)*static_cast<std::size_t>(channels)/2;
        for(std::size_t i=0;i<bytes;++i) {
            const int byte=p[i];
            expand(0,byte>>4);
            expand(channels==2?1:0,byte&15);
        }
    }
    if(out.size()<frames*static_cast<std::size_t>(channels)) return false;
    out.resize(frames*static_cast<std::size_t>(channels));
    return true;
}
} // namespace detail

// xWMA codec rates by (channels, sample rate): the encoder's nominal rate,
// which picks the decoder's tables. Black Ops uses 44.1 kHz mono at 48 kbps
// and 48 kHz stereo at 96 kbps; the rest are tried in turn.
inline std::vector<int> WmaRates(int channels,int rate) {
    std::vector<int> r;
    if(channels==1 && rate==44100) r.push_back(48000);
    if(channels==2 && rate==48000) r.push_back(96000);
    for(int b:{48000,96000,64000,32000,160000,20000,192000,128000}) if(std::find(r.begin(),r.end(),b)==r.end()) r.push_back(b);
    return r;
}
inline std::vector<int> WmaBlockAligns(const SndAsset& a) {
    std::vector<int> r;
    if(!a.seek.empty() && a.dataSize%a.seek.size()==0) r.push_back(static_cast<int>(a.dataSize/a.seek.size()));
    for(int b:{2230,4096,8192,4459,5945,8917,2731,1487,929,1280,2304,1536,1485,1008,6827,5462})
        if(std::find(r.begin(),r.end(),b)==r.end() && b>0 && a.dataSize%static_cast<std::size_t>(b)==0) r.push_back(b);
    return r;
}

// Decodes `a` (its data at `data`) to PCM with exactly a.frames frames.
inline bool Decode(const SndAsset& a,const std::uint8_t* data,Pcm& out,std::string* error=nullptr) {
    using detail::Fail;
    out=Pcm{};out.channels=static_cast<int>(a.channels);out.rate=static_cast<int>(a.rate);
    const std::size_t want=static_cast<std::size_t>(a.frames)*a.channels;
    switch(a.format) {
        case t5::Pcm16: {
            if(a.dataSize<want*2) return Fail(error,"short PCM data");
            out.samples.resize(want);std::memcpy(out.samples.data(),data,want*2);return true;
        }
        case t5::MsAdpcm:
            if(!detail::DecodeMsAdpcm(data,a.dataSize,static_cast<int>(a.channels),a.frames,out.samples)) return Fail(error,"bad MS-ADPCM data");
            return true;
        case t5::Wma: {
            for(const int align:WmaBlockAligns(a)) for(const int rate:WmaRates(static_cast<int>(a.channels),static_cast<int>(a.rate))) {
                wmadec::Config c;c.channels=static_cast<int>(a.channels);c.sampleRate=static_cast<int>(a.rate);c.bitRate=rate;c.blockAlign=align;
                std::vector<std::int16_t> pcm;int damaged=0;std::string text;
                // The frame count can end a quarter frame past the last whole
                // decoded frame; that tail (at most one frame) is silence.
                const std::size_t slack=static_cast<std::size_t>(2048)*a.channels;
                if(!wmadec::DecodeStream(c,data,a.dataSize,pcm,&damaged,&text) || damaged || pcm.size()+slack<want) continue;
                pcm.resize(want,0);out.samples=std::move(pcm);return true;
            }
            return Fail(error,"WMA data did not decode");
        }
        default: return Fail(error,"unsupported sound format "+std::to_string(a.format));
    }
}

// A streamed sound file (from an .iwd): the snd_asset header, its seek table,
// then the data at headerSize.
inline bool ParseStreamedFile(const std::uint8_t* p,std::size_t size,SndAsset& out,std::string* error=nullptr) {
    using detail::Fail;
    if(size<t5::kSndAssetBytes) return Fail(error,"streamed sound too small");
    out=detail::ReadSndAsset(p);
    if(!detail::SaneAsset(out)) return Fail(error,"not a Black Ops sound file");
    if(out.headerSize<t5::kSndAssetBytes || out.headerSize>size || out.dataSize>size-out.headerSize) return Fail(error,"bad streamed sound sizes");
    const std::size_t seekBytes=4*out.seek.size();
    if(t5::kSndAssetBytes+seekBytes<=out.headerSize) {
        for(std::size_t i=0;i<out.seek.size();++i) out.seek[i]=Get<std::uint32_t>(p+t5::kSndAssetBytes+4*i);
    } else out.seek.clear();
    out.data=out.headerSize;out.dataInline=true;
    return true;
}

// Mono mix of interleaved stereo (positional sounds are spatialised by MW2).
inline Pcm Downmix(const Pcm& in) {
    if(in.channels!=2) return in;
    Pcm out;out.channels=1;out.rate=in.rate;out.samples.resize(in.frames());
    for(std::size_t i=0;i<out.samples.size();++i)
        out.samples[i]=static_cast<std::int16_t>((static_cast<int>(in.samples[2*i])+in.samples[2*i+1])/2);
    return out;
}

// ------------------------------------------------------------ MW2 output
namespace mw2 {
inline constexpr int kAssetSound=11,kAssetSoundCurve=12,kAssetLoadedSound=13;
inline constexpr std::size_t kAliasListBytes=0x18,kAliasBytes=0x88,kSoundFileBytes=0x18,kLoadedSoundBytes=0x40,
    kCurveBytes=0x90,kSpeakerMapBytes=0x50,kSpeakerLevelBytes=8;
// snd_alias_t flags: bit 0 looping, bits 7-8 type (1 loaded), bits 9-14 channel.
inline constexpr std::uint32_t kTypeLoaded=1u<<7;
inline constexpr std::uint32_t kChannelAuto=3,kChannelAuto2d=10,kChannelBody=14,kChannelEffects1=19;
inline constexpr std::uint32_t Flags(std::uint32_t channel) {return kTypeLoaded|(channel<<9);}
inline constexpr const char* kDefaultCurve=",$default";
// The default speaker map: {input channel, output speaker, level} per entry,
// for mono -> stereo, mono -> 5.1, stereo -> stereo, stereo -> 5.1.
struct SpeakerLevel {std::uint8_t input,speaker;float level;};
inline const std::array<std::vector<SpeakerLevel>,4>& DefaultSpeakerMap() {
    static const std::array<std::vector<SpeakerLevel>,4> map={{
        {{0,0,.5f},{0,1,.5f}},
        {{0,0,.5f},{0,1,.5f},{0,2,0.f},{0,3,0.f},{0,4,0.f},{0,5,0.f}},
        {{0,0,1.f},{1,0,0.f},{0,1,0.f},{1,1,1.f}},
        {{0,0,1.f},{1,0,0.f},{0,1,0.f},{1,1,1.f},{0,2,0.f},{1,2,0.f},{0,3,0.f},{1,3,0.f},{0,4,0.f},{1,4,0.f},{0,5,0.f},{1,5,0.f}},
    }};
    return map;
}
} // namespace mw2

struct ZoneSound {
    std::string name;                    // MW2 loaded sound asset name
    Pcm pcm;
};
struct ZoneAlias {
    std::size_t sound=0;                 // index into SoundZoneSpec::sounds
    float volMin=1.f,volMax=1.f,pitchMin=1.f,pitchMax=1.f,distMin=100.f,distMax=1000.f;
    int startDelay=0;
    std::uint32_t flags=mw2::Flags(mw2::kChannelAuto);
};
struct ZoneAliasList {
    std::string name;
    std::vector<ZoneAlias> aliases;
};
struct SoundZoneSpec {
    std::vector<ZoneSound> sounds;
    std::vector<ZoneAliasList> lists;
};

namespace detail {
class SoundZoneBuilder {
public:
    SoundZoneBuilder(const SoundZoneSpec& spec,bo1model::ZoneStream& s):spec_(spec),s_(s) {}
    std::size_t pcmBytes=0,references=0;
    // Load_snd_alias_list_ptr: header in TEMP, the rest in the virtual block.
    void list(const ZoneAliasList& l) {
        namespace m=bo1model::mw2;
        s_.push(m::Temp);s_.align(3);
        std::uint8_t h[mw2::kAliasListBytes]{};
        Put<std::uint64_t>(h,m::kInline);Put<std::uint64_t>(h+8,m::kInline);Put<std::int32_t>(h+0x10,static_cast<std::int32_t>(l.aliases.size()));
        s_.read(h,sizeof(h));
        s_.push(m::Virtual);
        s_.string(l.name);
        s_.align(3);
        std::vector<std::uint8_t> head(mw2::kAliasBytes*l.aliases.size(),0);
        for(std::size_t n=0;n<l.aliases.size();++n) {
            const auto& a=l.aliases[n];auto* p=head.data()+mw2::kAliasBytes*n;
            Put<std::uint64_t>(p,m::kInline);                  // aliasName
            Put<std::uint64_t>(p+0x28,m::kInline);             // soundFile
            Put<float>(p+0x34,a.volMin);Put<float>(p+0x38,a.volMax);Put<float>(p+0x3c,a.pitchMin);Put<float>(p+0x40,a.pitchMax);
            Put<float>(p+0x44,a.distMin);Put<float>(p+0x48,a.distMax);
            Put<std::uint32_t>(p+0x50,a.flags);
            Put<float>(p+0x54,1.f);                           // slave/master percentage
            Put<float>(p+0x58,1.f);                           // probability
            Put<std::int32_t>(p+0x64,a.startDelay);
            Put<std::uint64_t>(p+0x68,m::kInline);             // volumeFalloffCurve
            Put<std::uint64_t>(p+0x80,m::kInline);             // speakerMap
        }
        s_.read(head);
        for(const auto& a:l.aliases) {
            s_.string(l.name);
            soundFile(a);
            curve();
            speakerMap();
        }
        s_.pop();
        s_.pop();
    }
private:
    void soundFile(const ZoneAlias& a) {
        namespace m=bo1model::mw2;
        s_.align(3);
        std::uint8_t f[mw2::kSoundFileBytes]{};
        f[0]=1;f[1]=1;Put<std::uint64_t>(f+8,m::kInline);
        s_.read(f,sizeof(f));
        loadedSound(spec_.sounds[a.sound]);
    }
    // Load_LoadedSoundPtr: header in TEMP; the PCM in TEMP too (MW2 copies it
    // to its own heap). A sound already written is a ',' reference.
    void loadedSound(const ZoneSound& sound) {
        namespace m=bo1model::mw2;
        const bool reference=!written_.insert(sound.name).second;
        s_.push(m::Temp);s_.align(3);
        std::uint8_t h[mw2::kLoadedSoundBytes]{};
        Put<std::uint64_t>(h,m::kInline);
        const auto& pcm=sound.pcm;
        const std::uint32_t bytes=static_cast<std::uint32_t>(pcm.samples.size()*2);
        if(!reference) {
            auto* w=h+8;                                       // WAVEFORMATEX
            Put<std::uint16_t>(w,1);Put<std::uint16_t>(w+2,static_cast<std::uint16_t>(pcm.channels));
            Put<std::uint32_t>(w+4,static_cast<std::uint32_t>(pcm.rate));
            Put<std::uint32_t>(w+8,static_cast<std::uint32_t>(pcm.rate*pcm.channels*2));
            Put<std::uint16_t>(w+12,static_cast<std::uint16_t>(pcm.channels*2));Put<std::uint16_t>(w+14,16);
            Put<std::uint32_t>(w+0x14,44);                     // data offset of the source .wav
            Put<std::uint32_t>(w+0x18,bytes);
            Put<std::uint64_t>(w+0x30,m::kInline);
        }
        s_.read(h,sizeof(h));
        s_.push(m::Virtual);
        s_.string(reference?','+sound.name:sound.name);
        s_.push(m::Temp);
        if(!reference && bytes) {s_.align(0);s_.read(pcm.samples.data(),bytes);pcmBytes+=bytes;}
        s_.pop();
        s_.pop();
        s_.pop();
        if(reference) ++references;
    }
    // Load_SndCurvePtr: a ',' reference to MW2's own "$default" curve.
    void curve() {
        namespace m=bo1model::mw2;
        s_.push(m::Temp);s_.align(3);
        std::uint8_t h[mw2::kCurveBytes]{};Put<std::uint64_t>(h,m::kInline);
        s_.read(h,sizeof(h));
        s_.push(m::Virtual);s_.string(mw2::kDefaultCurve);s_.pop();
        s_.pop();
    }
    void speakerMap() {
        namespace m=bo1model::mw2;
        s_.align(3);
        const auto& maps=mw2::DefaultSpeakerMap();
        std::uint8_t h[mw2::kSpeakerMapBytes]{};
        h[0]=1;Put<std::uint64_t>(h+8,m::kInline);
        for(std::size_t n=0;n<4;++n) {h[0x10+16*n]=static_cast<std::uint8_t>(maps[n].size());Put<std::uint64_t>(h+0x18+16*n,m::kInline);}
        s_.read(h,sizeof(h));
        s_.string("default");
        for(const auto& map:maps) {
            s_.align(3);
            for(const auto& e:map) {
                std::uint8_t b[mw2::kSpeakerLevelBytes]{};
                b[0]=e.input;b[1]=e.speaker;Put<float>(b+4,e.level);
                s_.read(b,sizeof(b));
            }
        }
    }
    const SoundZoneSpec& spec_;
    bo1model::ZoneStream& s_;
    std::set<std::string> written_;
};
} // namespace detail

struct SoundZoneResult {
    bo1model::ZoneResult zone;
    std::size_t pcmBytes=0,references=0;
};
inline bool BuildSoundZone(const SoundZoneSpec& spec,SoundZoneResult& out,std::string* error=nullptr) {
    using detail::Fail;
    namespace m=bo1model::mw2;
    if(spec.lists.empty()) return Fail(error,"no sounds to write");
    for(const auto& l:spec.lists) {
        if(l.aliases.empty() || l.name.empty() || l.name[0]==',') return Fail(error,"bad alias list "+l.name);
        for(const auto& a:l.aliases) {
            if(a.sound>=spec.sounds.size()) return Fail(error,"alias list "+l.name+" names a missing sound");
            const auto& p=spec.sounds[a.sound].pcm;
            if(p.channels<1 || p.channels>2 || p.rate<1000 || p.rate>200000 || p.samples.empty() || p.samples.size()%p.channels
               || p.samples.size()*2>0x7fffffffull) return Fail(error,"sound "+spec.sounds[a.sound].name+" is not playable PCM");
        }
    }
    bo1model::ZoneStream s;
    detail::SoundZoneBuilder b(spec,s);
    std::uint8_t list[m::kAssetListBytes]{};
    Put<std::uint32_t>(list,1);Put<std::uint64_t>(list+8,m::kInline);   // the null script string
    Put<std::uint32_t>(list+16,static_cast<std::uint32_t>(spec.lists.size()));Put<std::uint64_t>(list+24,m::kInline);
    s.bytes.insert(s.bytes.end(),list,list+sizeof(list));
    s.push(m::Virtual);                            // Load_XAssetList
    s.push(m::Virtual);                            // Load_ScriptStringList
    s.align(3);
    {const std::uint64_t p=0;s.read(&p,8);}
    s.pop();
    s.pop();
    s.push(m::Virtual);
    s.align(3);
    for(std::size_t n=0;n<spec.lists.size();++n) {
        std::uint8_t a[m::kAssetBytes]{};Put<std::int32_t>(a,mw2::kAssetSound);Put<std::uint64_t>(a+8,m::kInline);
        s.read(a,sizeof(a));
    }
    for(const auto& l:spec.lists) b.list(l);
    s.pop();
    if(s.depth()!=0) return Fail(error,"zone stream unbalanced");
    auto& z=out.zone;
    z.blockSize.fill(0);
    for(std::size_t n=0;n<m::BlockCount;++n) if(s.peak[n]) z.blockSize[n]=static_cast<std::uint32_t>(((s.peak[n]+15)&~15ull)+4096);
    z.stream.assign(m::kXFileBytes,0);
    Put<std::uint32_t>(z.stream.data(),static_cast<std::uint32_t>(s.bytes.size()));
    for(std::size_t n=0;n<m::BlockCount;++n) Put<std::uint32_t>(z.stream.data()+8+4*n,z.blockSize[n]);
    z.stream.insert(z.stream.end(),s.bytes.begin(),s.bytes.end());
    z.scriptStrings=1;z.assets=spec.lists.size();
    out.pcmBytes=b.pcmBytes;out.references=b.references;
    return true;
}
} // namespace mod::bo1sound
