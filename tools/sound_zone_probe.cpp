// Load a Test43-generated MW2 sound fastfile with the exact retail x64 loader.
//
// The private mapped image runs DB_LoadXFile (0xdb7f0) unmodified: header
// and version checks, DB_InitStreams, Load_XAsset and every sound loader
// under it (Load_snd_alias_list_ptr 0xe9eb0, Load_snd_alias_list_t 0xe9f70,
// Load_snd_alias_tArray 0xea050, Load_SoundFile 0xe2680, Load_LoadedSoundPtr
// 0xe0320, Load_LoadedSound 0xe0280, the sound data 0xe6bc0, Load_SndCurvePtr
// 0xe25c0, Load_SndCurve 0xe2530, Load_SpeakerMap 0xe2700/0xe27d0), with the
// real stream allocator. Only the edges are replaced: file reads come from
// memory, zone memory comes from guarded buffers, DB_AddXAsset copies headers
// into a registry (resolving ',' references like the engine), and the copy of
// the sound data to the heap (0x2b06f0) is recorded.
//
// Afterwards every alias, sound file, loaded sound (format and samples),
// curve reference and speaker map is compared with the source spec, block
// guards are checked, and the stream must be consumed exactly.
#include "script_call.hpp"
#include "bo1_sound_build.hpp"
#include <sys/mman.h>
#include <csetjmp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mod::bo1sound;
namespace m=mod::bo1model::mw2;
namespace {
constexpr std::uintptr_t kBase=0x140000000;
constexpr std::size_t kSize=0x5000000;
constexpr std::uintptr_t kLoadXFile=0xdb7f0;
constexpr std::uintptr_t kReaderInit=0xdbb40,kRawRead=0xdbe50,kHeaderError=0xdba70,kInflateInit=0xdbae0,kRead=0xdbbc0;
constexpr std::uintptr_t kVersionError=0x1fb5f0,kComError=0x2906b0,kAllocZone=0xee1b0,kFinishZone=0xee450,kInflateEnd=0xdbb30;
constexpr std::uintptr_t kAfterLoad1=0xdbaa0,kAfterLoad2=0xf0920;
constexpr std::uintptr_t kAddSound=0xf39d0,kAddSoundCurve=0xf32d0,kAddLoadedSound=0xf28b0,kCopySoundData=0x2b06f0,kSlGetString=0x21a670;
constexpr std::uintptr_t kProgressFlag=0x600e04,kStreamPositions=0xb697d0;

void Check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
std::uint8_t* image=nullptr;
void Jump(std::uintptr_t rva,const void* target) {
    auto* p=image+rva;p[0]=0x48;p[1]=0xb8;std::uint64_t t=reinterpret_cast<std::uint64_t>(target);std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;
}

std::vector<std::uint8_t> rawFile;std::size_t rawPos=0;
std::vector<std::uint8_t> stream;std::size_t streamPos=0;
std::string failure;
std::jmp_buf escape;

void MOD_NATIVE_ABI ReaderInit(void*,int) {}
void MOD_NATIVE_ABI InflateInit(int) {}
void MOD_NATIVE_ABI Nothing() {}
void MOD_NATIVE_ABI HeaderError() {failure="fastfile header rejected";std::longjmp(escape,1);}
void MOD_NATIVE_ABI RawRead(void* p,int size) {
    if(rawPos+static_cast<std::size_t>(size)>21){failure="raw read past the header";std::memset(p,0,static_cast<std::size_t>(size));return;}
    std::memcpy(p,rawFile.data()+rawPos,static_cast<std::size_t>(size));rawPos+=static_cast<std::size_t>(size);
}
void MOD_NATIVE_ABI Read(void* p,int size,int) {
    if(size<0 || streamPos+static_cast<std::size_t>(size)>stream.size()){failure="stream read past the end";std::memset(p,0,size>0?static_cast<std::size_t>(size):0);return;}
    std::memcpy(p,stream.data()+streamPos,static_cast<std::size_t>(size));streamPos+=static_cast<std::size_t>(size);
}
void MOD_NATIVE_ABI Error(const char* text) {failure=std::string("engine error: ")+(text?text:"?");std::longjmp(escape,1);}
void MOD_NATIVE_ABI VersionError(int,const char* text) {failure=std::string("engine error: ")+(text?text:"?");std::longjmp(escape,1);}

constexpr std::size_t kPage=4096;
struct Block {std::uint8_t* data=nullptr;std::size_t size=0,mapped=0;};
std::array<Block,8> blocks;
void MOD_NATIVE_ABI AllocZone(const std::uint32_t* sizes,const char*,std::uint8_t* zoneMem) {
    for(std::size_t n=0;n<8;++n) {
        if(!sizes[n]) continue;
        auto& b=blocks[n];b.size=sizes[n];b.mapped=((b.size+kPage-1)&~(kPage-1))+kPage;
        b.data=static_cast<std::uint8_t*>(mmap(nullptr,b.mapped,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        std::memset(b.data,0xcd,b.mapped);
        *reinterpret_cast<std::uint8_t**>(zoneMem+0x10*n)=b.data;*reinterpret_cast<std::uint32_t*>(zoneMem+0x10*n+8)=sizes[n];
    }
}
unsigned MOD_NATIVE_ABI SlGetString(const char*,int) {return 0;}

struct Asset {int type;std::string name;std::vector<std::uint8_t> header;bool placeholder=false;};
std::vector<std::unique_ptr<Asset>> assets;
std::map<std::pair<int,std::string>,Asset*> byName;
std::size_t references=0;
const std::map<int,std::size_t> kHeaderBytes={{mw2::kAssetSound,mw2::kAliasListBytes},{mw2::kAssetSoundCurve,mw2::kCurveBytes},
    {mw2::kAssetLoadedSound,mw2::kLoadedSoundBytes}};
template<int Type> void MOD_NATIVE_ABI Add(std::uint8_t** var) {
    const auto* header=*var;
    const char* name=*reinterpret_cast<const char* const*>(header);
    std::string text=name?name:"";
    if(!text.empty() && text[0]==',') {
        ++references;
        const auto found=byName.find({Type,text.substr(1)});
        if(found!=byName.end()) {*var=found->second->header.data();return;}
        auto a=std::make_unique<Asset>();a->type=Type;a->name=text.substr(1);a->placeholder=true;
        a->header.assign(kHeaderBytes.at(Type),0);
        *var=a->header.data();byName[{Type,a->name}]=a.get();assets.push_back(std::move(a));return;
    }
    if(byName.count({Type,text})) {failure="duplicate asset "+text;return;}
    auto a=std::make_unique<Asset>();a->type=Type;a->name=text;
    a->header.assign(header,header+kHeaderBytes.at(Type));
    *var=a->header.data();byName[{Type,text}]=a.get();assets.push_back(std::move(a));
}
// 0x2b06f0(&snd->data, snd): the engine copies the zone data to its heap.
std::vector<std::unique_ptr<std::vector<std::uint8_t>>> heap;
std::size_t copies=0;
void MOD_NATIVE_ABI CopySoundData(std::uint8_t** data,std::uint8_t* snd) {
    const auto size=Get<std::uint32_t>(snd+0x18);
    heap.push_back(std::make_unique<std::vector<std::uint8_t>>(*data,*data+size));
    *data=heap.back()->data();++copies;
}

template<class T> T At(const std::uint8_t* p,std::size_t o) {return Get<T>(p+o);}
const std::uint8_t* Ptr(const std::uint8_t* p,std::size_t o) {return reinterpret_cast<const std::uint8_t*>(Get<std::uint64_t>(p+o));}
bool Within(const void* p,std::size_t n,int block) {
    const auto& b=blocks[static_cast<std::size_t>(block)];const auto* q=static_cast<const std::uint8_t*>(p);
    return b.data && q>=b.data && q+n<=b.data+b.size;
}
std::string Str(const std::uint8_t* p) {return p?reinterpret_cast<const char*>(p):"";}

struct Report {std::size_t lists=0,aliases=0,sounds=0,pcmBytes=0,speakerMaps=0;};

void Verify(const SoundZoneSpec& spec,Report& report) {
    std::set<std::string> sounds;
    for(const auto& l:spec.lists) {
        const auto found=byName.find({mw2::kAssetSound,l.name});
        Check(found!=byName.end() && !found->second->placeholder,"alias list "+l.name+" not registered");
        const auto* h=found->second->header.data();
        const std::string where="list "+l.name+": ";
        Check(Str(Ptr(h,0))==l.name && At<std::int32_t>(h,0x10)==static_cast<std::int32_t>(l.aliases.size()),where+"header");
        const auto* head=Ptr(h,8);
        Check(Within(head,mw2::kAliasBytes*l.aliases.size(),m::Virtual),where+"aliases outside the virtual block");
        for(std::size_t n=0;n<l.aliases.size();++n) {
            const auto& a=l.aliases[n];const auto* p=head+mw2::kAliasBytes*n;
            const std::string at=where+"alias "+std::to_string(n)+": ";
            Check(Str(Ptr(p,0))==l.name,at+"name");
            for(std::size_t o:{0x08,0x10,0x18,0x20}) Check(!Ptr(p,o),at+"unused string");
            Check(At<float>(p,0x34)==a.volMin && At<float>(p,0x38)==a.volMax && At<float>(p,0x3c)==a.pitchMin && At<float>(p,0x40)==a.pitchMax
                  && At<float>(p,0x44)==a.distMin && At<float>(p,0x48)==a.distMax,at+"volume/pitch/distance");
            Check(At<std::uint32_t>(p,0x50)==a.flags && !(a.flags&1) && ((a.flags>>7)&3)==1,at+"flags");
            Check(At<float>(p,0x54)==1.f && At<float>(p,0x58)==1.f && At<std::int32_t>(p,0x64)==a.startDelay,at+"probability/delay");
            // Sound file -> loaded sound asset.
            const auto* sf=Ptr(p,0x28);
            Check(Within(sf,mw2::kSoundFileBytes,m::Virtual) && sf[0]==1 && sf[1]==1,at+"sound file");
            const auto& want=spec.sounds[a.sound];
            const auto ls=byName.find({mw2::kAssetLoadedSound,want.name});
            Check(ls!=byName.end() && !ls->second->placeholder,at+"loaded sound "+want.name+" not registered");
            Check(Ptr(sf,8)==ls->second->header.data(),at+"sound file does not point at "+want.name);
            if(sounds.insert(want.name).second) {
                const auto* s=ls->second->header.data();
                Check(Str(Ptr(s,0))==want.name,at+"loaded sound name");
                const auto* w=s+8;const auto& pcm=want.pcm;
                Check(At<std::uint16_t>(w,0)==1 && At<std::uint16_t>(w,2)==pcm.channels && At<std::uint32_t>(w,4)==static_cast<std::uint32_t>(pcm.rate)
                      && At<std::uint32_t>(w,8)==static_cast<std::uint32_t>(pcm.rate*pcm.channels*2) && At<std::uint16_t>(w,12)==pcm.channels*2
                      && At<std::uint16_t>(w,14)==16,at+"WAVEFORMATEX");
                Check(At<std::uint32_t>(w,0x18)==pcm.samples.size()*2,at+"data length");
                const auto* data=Ptr(w,0x30);
                Check(data && !std::memcmp(data,pcm.samples.data(),pcm.samples.size()*2),at+"samples");
                bool heapCopy=false;for(const auto& v:heap) heapCopy=heapCopy || v->data()==data;
                Check(heapCopy,at+"samples not copied to the heap");
                ++report.sounds;report.pcmBytes+=pcm.samples.size()*2;
            }
            // Curve: a reference to MW2's "$default".
            const auto curve=byName.find({mw2::kAssetSoundCurve,"$default"});
            Check(curve!=byName.end() && curve->second->placeholder && Ptr(p,0x68)==curve->second->header.data(),at+"curve reference");
            // Speaker map.
            const auto* sm=Ptr(p,0x80);
            Check(Within(sm,mw2::kSpeakerMapBytes,m::Virtual) && sm[0]==1 && Str(Ptr(sm,8))=="default",at+"speaker map");
            const auto& maps=mw2::DefaultSpeakerMap();
            for(std::size_t k=0;k<4;++k) {
                Check(sm[0x10+16*k]==maps[k].size(),at+"speaker count");
                const auto* e=Ptr(sm,0x18+16*k);
                Check(Within(e,maps[k].size()*8,m::Virtual),at+"speaker levels outside the virtual block");
                for(std::size_t i=0;i<maps[k].size();++i)
                    Check(e[8*i]==maps[k][i].input && e[8*i+1]==maps[k][i].speaker && At<float>(e+8*i,4)==maps[k][i].level,at+"speaker level");
            }
            ++report.speakerMaps;++report.aliases;
        }
        ++report.lists;
    }
    Check(copies==report.sounds,"sound data copied "+std::to_string(copies)+" times for "+std::to_string(report.sounds)+" sounds");
}

Report LoadAndVerify(const SoundZoneSpec& spec,const std::vector<std::uint8_t>& file) {
    assets.clear();byName.clear();heap.clear();copies=0;references=0;failure.clear();
    for(auto& b:blocks){if(b.data)munmap(b.data,b.mapped);b=Block{};}
    rawFile.assign(file.begin(),file.begin()+21);rawPos=0;
    stream.clear();streamPos=0;
    std::size_t at=21+2;
    for(;;) {
        const bool last=file.at(at)&1;const std::size_t n=file[at+1]|(file[at+2]<<8);
        Check(((file[at+3]|(file[at+4]<<8))^0xffff)==n,"bad stored block");
        stream.insert(stream.end(),file.begin()+static_cast<std::ptrdiff_t>(at+5),file.begin()+static_cast<std::ptrdiff_t>(at+5+n));at+=5+n;
        if(last) break;
    }
    std::uint8_t zoneStruct[0xf0]{};std::strcpy(reinterpret_cast<char*>(zoneStruct+8),"bo1mw2_sound_probe");
    std::uint8_t zoneMem[0xa0]{};
    *reinterpret_cast<std::uint32_t*>(image+kProgressFlag)=0;
    using LoadFn=void(MOD_NATIVE_ABI*)(std::uint8_t*,std::uint8_t*);
    if(setjmp(escape)) Check(false,failure);
    reinterpret_cast<LoadFn>(image+kLoadXFile)(zoneMem,zoneStruct);
    Check(failure.empty(),failure);
    Check(rawPos==21,"header not read exactly");
    Check(streamPos==stream.size(),"stream not consumed exactly ("+std::to_string(streamPos)+" of "+std::to_string(stream.size())+")");
    for(std::size_t n=0;n<8;++n) {
        const auto& b=blocks[n];
        const auto pos=*reinterpret_cast<std::uint8_t**>(image+kStreamPositions+8*n);
        if(!b.data) continue;
        Check(pos>=b.data && pos<=b.data+b.size,"block "+std::to_string(n)+" position outside its size");
        for(std::size_t k=b.size;k<b.mapped;++k) Check(b.data[k]==0xcd,"block "+std::to_string(n)+" overran its size");
    }
    Report report;
    Verify(spec,report);
    return report;
}

// Two lists, one sound shared between them, mono and stereo, 44.1/48 kHz.
SoundZoneSpec Synthetic() {
    SoundZoneSpec spec;
    auto tone=[](int channels,int rate,std::size_t frames,int seed) {
        Pcm p;p.channels=channels;p.rate=rate;p.samples.resize(frames*static_cast<std::size_t>(channels));
        for(std::size_t i=0;i<p.samples.size();++i) p.samples[i]=static_cast<std::int16_t>((static_cast<int>(i)*37*seed)%20000-10000);
        return p;
    };
    spec.sounds.push_back({"bo1/synthetic/a.wav",tone(1,44100,4410,1)});
    spec.sounds.push_back({"bo1/synthetic/b.wav",tone(2,48000,9601,2)});
    spec.sounds.push_back({"bo1/synthetic/c.wav",tone(1,48000,3,3)});
    ZoneAliasList l1;l1.name="bo1_synthetic_one";
    ZoneAlias a;a.sound=0;a.volMin=.5f;a.volMax=.7f;a.pitchMin=.9f;a.pitchMax=1.1f;a.distMin=100;a.distMax=1500;a.flags=mw2::Flags(mw2::kChannelAuto);
    l1.aliases.push_back(a);
    a.sound=1;a.startDelay=250;a.flags=mw2::Flags(mw2::kChannelAuto2d);l1.aliases.push_back(a);
    ZoneAliasList l2;l2.name="bo1_synthetic_two";
    a.sound=1;a.startDelay=0;a.flags=mw2::Flags(mw2::kChannelBody);l2.aliases.push_back(a);
    a.sound=2;a.flags=mw2::Flags(mw2::kChannelEffects1);l2.aliases.push_back(a);
    a.sound=0;l2.aliases.push_back(a);
    spec.lists={l1,l2};
    return spec;
}
std::vector<std::uint8_t> ReadAll(const char* path) {
    std::ifstream f(path,std::ios::binary);Check(f.is_open(),std::string("cannot read ")+path);
    return {std::istreambuf_iterator<char>(f),{}};
}
} // namespace

int main(int argc,char** argv) {
    if(argc<2) return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4071280 && bytes.size()<kSize,"Invalid private image");
        image=static_cast<std::uint8_t*>(mmap(reinterpret_cast<void*>(kBase),kSize,PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
        Check(image==reinterpret_cast<std::uint8_t*>(kBase),"Preferred image base unavailable");
        std::memcpy(image,bytes.data(),bytes.size());
        Jump(kReaderInit,reinterpret_cast<void*>(&ReaderInit));Jump(kRawRead,reinterpret_cast<void*>(&RawRead));
        Jump(kHeaderError,reinterpret_cast<void*>(&HeaderError));Jump(kInflateInit,reinterpret_cast<void*>(&InflateInit));
        Jump(kRead,reinterpret_cast<void*>(&Read));Jump(kVersionError,reinterpret_cast<void*>(&VersionError));
        Jump(kComError,reinterpret_cast<void*>(&Error));Jump(kAllocZone,reinterpret_cast<void*>(&AllocZone));
        Jump(kFinishZone,reinterpret_cast<void*>(&Nothing));Jump(kInflateEnd,reinterpret_cast<void*>(&Nothing));
        Jump(kAfterLoad1,reinterpret_cast<void*>(&Nothing));Jump(kAfterLoad2,reinterpret_cast<void*>(&Nothing));
        Jump(kAddSound,reinterpret_cast<void*>(&Add<mw2::kAssetSound>));
        Jump(kAddSoundCurve,reinterpret_cast<void*>(&Add<mw2::kAssetSoundCurve>));
        Jump(kAddLoadedSound,reinterpret_cast<void*>(&Add<mw2::kAssetLoadedSound>));
        Jump(kCopySoundData,reinterpret_cast<void*>(&CopySoundData));
        Jump(kSlGetString,reinterpret_cast<void*>(&SlGetString));

        std::string json="{";
        auto emit=[&](const char* tag,const Report& r,std::size_t fileBytes,const mod::bo1model::ZoneResult& z,const std::string& extra) {
            char buffer[768];
            std::snprintf(buffer,sizeof(buffer),"\"%s\":{\"alias_lists\":%zu,\"aliases\":%zu,\"loaded_sounds\":%zu,\"pcm_bytes\":%zu,"
                "\"speaker_maps\":%zu,\"references_resolved\":%zu,\"fastfile_bytes\":%zu,\"block_sizes\":[%u,%u,%u,%u,%u,%u,%u,%u]%s}",
                tag,r.lists,r.aliases,r.sounds,r.pcmBytes,r.speakerMaps,references,fileBytes,z.blockSize[0],z.blockSize[1],z.blockSize[2],
                z.blockSize[3],z.blockSize[4],z.blockSize[5],z.blockSize[6],z.blockSize[7],extra.c_str());
            json+=(json.size()>1?",":"");json+=buffer;
        };
        {
            const auto spec=Synthetic();
            SoundZoneResult z;std::string error;
            Check(BuildSoundZone(spec,z,&error),"BuildSoundZone: "+error);
            const auto file=mod::bo1model::FastFile(z.zone);
            const auto r=LoadAndVerify(spec,file);
            // Two shared sounds become ',' references, plus one "$default" curve per alias.
            Check(r.lists==2 && r.aliases==5 && r.sounds==3 && z.references==2 && references==2+5,"synthetic coverage");
            emit("synthetic",r,file.size(),z.zone,"");
        }
        if(argc>2) {
            const auto common=ReadAll(argv[2]);
            std::vector<std::uint8_t> theater;
            if(argc>3) theater=ReadAll(argv[3]);
            SoundZoneSpec spec;SoundBuildReport report;std::string error;
            Check(BuildSoundSpec(common.data(),common.size(),theater.data(),theater.size(),{},spec,report,&error),"BuildSoundSpec: "+error);
            SoundZoneResult z;
            Check(BuildSoundZone(spec,z,&error),"BuildSoundZone: "+error);
            const auto file=mod::bo1model::FastFile(z.zone);
            const auto r=LoadAndVerify(spec,file);
            Check(r.lists==spec.lists.size() && r.lists>=40 && r.sounds==spec.sounds.size(),"zombie sound coverage");
            char extra[256];
            std::snprintf(extra,sizeof(extra),",\"decode_failures\":%u,\"skipped_aliases\":%u,\"missing_lists\":%u,\"streamed_missing\":%u,\"audio_seconds\":%.1f",
                report.decodeFailures,report.skippedAliases,report.missingLists,report.streamedMissing,report.seconds10/10.0);
            emit("zombie_sounds",r,file.size(),z.zone,extra);
        }
        json+="}";
        std::puts(json.c_str());
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"%s\n",e.what());return 1;
    }
}
