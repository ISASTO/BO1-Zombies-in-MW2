// Test43: BO1 sound import (include/bo1_sound.hpp, include/bo1_sound_build.hpp,
// third_party/wmadec). Synthetic data always; with BO1_COMMON_DEC (and
// optionally BO1_THEATER_DEC) set to local decompressed zones, the real
// zombie sound banks too (never committed).
#include "bo1_sound_build.hpp"
#include "wma_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

using namespace mod::bo1sound;
using mod::bo1xanim::Put;
using mod::bo1xanim::Get;

namespace {
int failures=0;
void Expect(bool ok,const char* what) {if(!ok){std::cerr<<"FAIL: "<<what<<"\n";++failures;}}

// ------------------------------------------------------------ WMA decoder
void TestBitsAndVlc() {
    const std::uint8_t data[]={0b10110010,0b01111111};
    wmadec::BitReader r(data,sizeof(data),16);
    Expect(r.get(3)==0b101 && r.get1()==1 && r.get(4)==0b0010 && r.count()==8,"bit reader MSB first");
    Expect(r.get(8)==0x7f && r.left()==0 && r.get(4)==0 && r.left()==-4,"bit reader zero padding");
    wmadec::Vlc v;
    Expect(v.add(0b0,1,7) && v.add(0b10,2,-3) && v.add(0b11,2,42),"vlc add");
    Expect(!v.add(0b1,1,1),"vlc rejects a prefix collision");
    const std::uint8_t codes[]={0b01011000};   // 0 | 10 | 11 | 000
    wmadec::BitReader c(codes,1,8);
    Expect(v.decode(c)==7 && v.decode(c)==-3 && v.decode(c)==42,"vlc decode with negative symbols");
}
void TestImdct() {
    std::mt19937 rng(7);std::uniform_real_distribution<float> d(-1.f,1.f);
    for(int n=128;n<=2048;n*=2) {
        std::vector<float> in(static_cast<std::size_t>(n)),fast(static_cast<std::size_t>(2*n)),slow(static_cast<std::size_t>(2*n));
        for(auto& x:in) x=d(rng);
        wmadec::Imdct m;m.init(n,1.0/32768.0);m.run(fast.data(),in.data());
        wmadec::ImdctDirect(slow.data(),in.data(),n,1.0/32768.0);
        double worst=0;for(std::size_t i=0;i<fast.size();++i) worst=std::max(worst,static_cast<double>(std::fabs(fast[i]-slow[i])));
        Expect(worst<1e-7,"fast IMDCT matches the definition");
    }
}
void TestWmaFixture() {
    wmadec::Config c;c.channels=1;c.sampleRate=44100;c.bitRate=48000;c.blockAlign=wmafixture::kBlockAlign;c.flags2=1;
    std::vector<std::int16_t> pcm;int damaged=-1;std::string error;
    Expect(wmadec::DecodeStream(c,wmafixture::kPackets,sizeof(wmafixture::kPackets),pcm,&damaged,&error),"fixture decodes");
    Expect(damaged==0 && pcm.size()==static_cast<std::size_t>(wmafixture::kSamples),"fixture sample count (FFmpeg: frames + end frame - delay)");
    int worst=0;
    for(std::size_t i=0;i<std::size(wmafixture::kExpected) && i*wmafixture::kStep<pcm.size();++i)
        worst=std::max(worst,std::abs(pcm[i*wmafixture::kStep]-wmafixture::kExpected[i]));
    Expect(worst<=1,"fixture samples within 1 LSB of FFmpeg");
    // A truncated packet is reported, not decoded.
    wmadec::Decoder dec;Expect(dec.init(c),"decoder init");
    std::vector<float> out;
    Expect(!dec.decodePacket(wmafixture::kPackets,10,out) && out.empty(),"short packet refused");
    wmadec::Config bad=c;bad.channels=3;
    Expect(!dec.init(bad),"three channels refused");
}

// --------------------------------------------------------------- MS-ADPCM
// The BO1 block (512 samples, 262 bytes per channel), encoded by brute force
// so the decoder's reconstruction can be compared with the encoder's.
std::vector<std::uint8_t> EncodeAdpcmBlock(const std::vector<std::int16_t>& s,int channels,std::vector<std::int16_t>& recon) {
    static constexpr int kCoef1[7]={256,512,0,192,240,460,392},kCoef2[7]={0,-256,0,64,0,-208,-232};
    static constexpr int kAdapt[16]={230,230,230,230,307,409,512,614,768,614,512,409,307,230,230,230};
    std::vector<std::uint8_t> b(static_cast<std::size_t>(262*channels),0);
    const int pred=1;
    int delta[2]={64,64},s1[2],s2[2];
    for(int c=0;c<channels;++c) {b[static_cast<std::size_t>(c)]=pred;s2[c]=s[static_cast<std::size_t>(c)];s1[c]=s[static_cast<std::size_t>(channels+c)];}
    for(int c=0;c<channels;++c) Put<std::int16_t>(b.data()+channels+2*c,static_cast<std::int16_t>(delta[c]));
    for(int c=0;c<channels;++c) Put<std::int16_t>(b.data()+3*channels+2*c,static_cast<std::int16_t>(s1[c]));
    for(int c=0;c<channels;++c) Put<std::int16_t>(b.data()+5*channels+2*c,static_cast<std::int16_t>(s2[c]));
    recon.clear();
    for(int c=0;c<channels;++c) recon.push_back(static_cast<std::int16_t>(s2[c]));
    for(int c=0;c<channels;++c) recon.push_back(static_cast<std::int16_t>(s1[c]));
    std::size_t at=static_cast<std::size_t>(7*channels);
    int nibbleIndex=0;
    for(std::size_t i=static_cast<std::size_t>(2*channels);i<s.size();++i) {
        const int c=static_cast<int>(i%static_cast<std::size_t>(channels));
        const int predictor=(s1[c]*kCoef1[pred]+s2[c]*kCoef2[pred])/256;
        int best=0,bestErr=1<<30,bestValue=0;
        for(int n=-8;n<8;++n) {
            const int v=std::clamp(predictor+n*delta[c],-32768,32767);
            const int e=std::abs(v-s[i]);
            if(e<bestErr) {bestErr=e;best=n;bestValue=v;}
        }
        const int nibble=best&15;
        if(nibbleIndex%2==0) b[at]=static_cast<std::uint8_t>(nibble<<4);
        else b[at++]|=static_cast<std::uint8_t>(nibble);
        ++nibbleIndex;
        s2[c]=s1[c];s1[c]=bestValue;recon.push_back(static_cast<std::int16_t>(bestValue));
        delta[c]=std::max(16,(kAdapt[nibble]*delta[c])>>8);
    }
    return b;
}
void TestMsAdpcm() {
    for(int channels:{1,2}) {
        std::vector<std::int16_t> s(static_cast<std::size_t>(512*channels));
        for(std::size_t i=0;i<s.size();++i) s[i]=static_cast<std::int16_t>(9000*std::sin(0.03*static_cast<double>(i/static_cast<std::size_t>(channels)))
            +(i%static_cast<std::size_t>(channels)?2000:0));
        std::vector<std::int16_t> recon;
        auto block=EncodeAdpcmBlock(s,channels,recon);
        block.insert(block.end(),block.begin(),block.end());   // two blocks
        SndAsset a;a.version=1;a.format=t5::MsAdpcm;a.channels=static_cast<std::uint32_t>(channels);a.rate=48000;a.frames=1000;a.dataSize=block.size();
        Pcm p;std::string error;
        Expect(Decode(a,block.data(),p,&error),"MS-ADPCM decodes");
        Expect(p.frames()==1000 && p.channels==channels,"MS-ADPCM frame count");
        bool same=true;for(std::size_t i=0;i<recon.size();++i) same=same && p.samples[i]==recon[i];
        Expect(same,"MS-ADPCM matches the encoder's reconstruction");
        Expect(p.samples[recon.size()]==recon[0],"second block restarts from its header");
        a.frames=1100;Expect(!Decode(a,block.data(),p,&error),"MS-ADPCM refuses frames past the data");
        block[0]=7;a.frames=10;Expect(!Decode(a,block.data(),p,&error),"MS-ADPCM refuses a bad predictor");
    }
}

// -------------------------------------------------------------- T5 banks
// Writes a T5 SndBank as the zone stream holds it while replaying the
// virtual block, so shared sounds can point back with zone pointers.
struct T5Writer {
    std::vector<std::uint8_t> raw;
    std::uint64_t cur=0;
    static constexpr std::uint64_t kBase=0x0123450;   // virtual offset of the bank header
    std::uint64_t put(const void* p,std::size_t n,std::size_t align,bool counted=true) {
        std::uint64_t at=0;
        if(counted) {cur=(cur+align-1)&~static_cast<std::uint64_t>(align-1);at=cur;cur+=n;}
        const auto* b=static_cast<const std::uint8_t*>(p);raw.insert(raw.end(),b,b+n);
        return at;
    }
    void str(const std::string& s) {put(s.c_str(),s.size()+1,1);}
    static std::uint32_t Ref(std::uint64_t at) {return static_cast<std::uint32_t>(((4ull<<29)|(kBase+at))+1);}
};
struct SyntheticBank {
    std::vector<std::uint8_t> raw;
    std::size_t data1=0,data2=0;
};
SyntheticBank MakeBank() {
    T5Writer w;
    SyntheticBank out;
    w.raw.assign(37,0xab);                        // whatever precedes the asset
    std::uint8_t h[40]{};Put<std::uint32_t>(h,t5::kInline);Put<std::uint32_t>(h+4,3);Put<std::uint32_t>(h+8,t5::kInline);
    w.put(h,sizeof(h),4);
    w.str("zmb_test.all");
    std::uint8_t lists[60]{};
    for(int n=0;n<3;++n) {Put<std::uint32_t>(lists+20*n,t5::kInline);Put<std::uint32_t>(lists+20*n+8,t5::kInline);}
    Put<std::uint32_t>(lists+12,2);Put<std::uint32_t>(lists+20+12,3);Put<std::uint32_t>(lists+40+12,1);
    w.put(lists,sizeof(lists),4);
    auto alias=[](std::uint32_t soundFile,std::uint32_t flags,std::uint16_t volMax) {
        std::array<std::uint8_t,84> a{};Put<std::uint32_t>(a.data(),0x1234);Put<std::uint32_t>(a.data()+16,soundFile);
        Put<std::uint32_t>(a.data()+20,flags);Put<std::uint16_t>(a.data()+36+8,32845);Put<std::uint16_t>(a.data()+36+10,volMax);
        Put<std::uint16_t>(a.data()+36+14,32767);Put<std::uint16_t>(a.data()+36+16,32767);
        Put<std::uint16_t>(a.data()+36+20,150);Put<std::uint16_t>(a.data()+36+22,1000);return a;
    };
    auto loaded=[&](const std::string& file,std::uint32_t frames,std::uint32_t loadedRef,std::size_t& dataAt,std::uint64_t* soundFileAt) {
        std::uint8_t f[8]{};Put<std::uint32_t>(f,loadedRef?loadedRef:t5::kInline);f[4]=1;f[5]=1;
        const auto at=w.put(f,8,4);if(soundFileAt)*soundFileAt=at;
        if(loadedRef) return std::uint64_t{0};
        std::uint8_t ls[60]{};Put<std::uint32_t>(ls,t5::kInline);
        auto* a=ls+4;Put<std::uint32_t>(a,1);Put<std::uint32_t>(a+4,frames);Put<std::uint32_t>(a+8,44100);Put<std::uint32_t>(a+12,1);
        Put<std::uint32_t>(a+16,0x830);Put<std::uint32_t>(a+28,t5::Pcm16);Put<std::uint32_t>(a+40,2);Put<std::uint32_t>(a+44,t5::kInline);
        Put<std::uint32_t>(a+48,frames*2);Put<std::uint32_t>(a+52,t5::kInline);
        const auto lsAt=w.put(ls,60,4);
        w.str(file);
        const std::uint32_t seek[2]={frames,frames*2};w.put(seek,8,4);
        dataAt=w.raw.size();
        std::vector<std::uint8_t> pcm(frames*2);for(std::size_t i=0;i<pcm.size();++i) pcm[i]=static_cast<std::uint8_t>(i*7+frames);
        w.put(pcm.data(),pcm.size(),2048,false);
        return lsAt;
    };
    // List 1: two aliases with their own sounds (the second loops).
    w.str("zmb_vocals_test");
    auto a1=alias(t5::kInline,t5::kFlag3d,46395),a2=alias(t5::kInline,t5::kFlag3d|t5::kFlagLooping,46395);
    w.put(a1.data(),84,4);w.put(a2.data(),84,4);
    std::uint64_t file1=0,file2=0;
    const auto loaded1=loaded("sound\\vox\\test\\a.wav",300,0,out.data1,&file1);
    loaded("sound\\vox\\test\\b.wav",200,0,out.data2,&file2);
    // List 2: both sound files shared, then a sound file holding a shared loaded sound.
    w.str("zmb_shared_test");
    auto a3=alias(T5Writer::Ref(file1),0,65535),a3b=alias(T5Writer::Ref(file2),0,65535),a4=alias(t5::kInline,t5::kFlag3d,65535);
    w.put(a3.data(),84,4);w.put(a3b.data(),84,4);w.put(a4.data(),84,4);
    std::size_t unused=0;
    loaded("",0,T5Writer::Ref(loaded1),unused,nullptr);
    // List 3: a streamed sound.
    w.str("mus_test");
    auto a5=alias(t5::kInline,0,65535);w.put(a5.data(),84,4);
    std::uint8_t sf[8]{};Put<std::uint32_t>(sf,t5::kInline);sf[4]=2;sf[5]=1;w.put(sf,8,4);
    std::uint8_t ss[8]{};Put<std::uint32_t>(ss,t5::kInline);w.put(ss,8,4);
    w.str("sound\\mus\\test\\round.wav");
    w.raw.insert(w.raw.end(),16,0xcd);
    out.raw=std::move(w.raw);
    return out;
}
void TestBank() {
    const auto bank=MakeBank();
    const auto at=FindBank(bank.raw.data(),bank.raw.size(),"zmb_test.all");
    Expect(at && *at==37,"bank header found");
    Expect(!FindBank(bank.raw.data(),bank.raw.size(),"zmb_other.all"),"absent bank not found");
    Bank b;std::string error;
    Expect(ParseBank(bank.raw.data(),bank.raw.size(),*at,b,&error),"bank parses");
    Expect(b.name=="zmb_test.all" && b.lists.size()==3 && b.loaded==2 && b.streamed==1 && b.shared==3 && b.unresolved==0,"bank counts");
    const auto& l1=b.lists[0];const auto& l2=b.lists[1];const auto& l3=b.lists[2];
    Expect(l1.name=="zmb_vocals_test" && l1.aliases.size()==2 && l2.aliases.size()==3 && l3.aliases.size()==1,"list names and sizes");
    Expect(l1.aliases[0].sound.kind==SoundRef::Kind::Loaded && l1.aliases[0].sound.file=="sound\\vox\\test\\a.wav"
           && l1.aliases[0].sound.asset.frames==300 && l1.aliases[0].sound.asset.seek.size()==2 && l1.aliases[0].sound.asset.data==bank.data1,"loaded sound");
    Expect(l1.aliases[1].looping() && l1.aliases[0].positional() && !l3.aliases[0].positional(),"alias flags");
    Expect(std::fabs(l1.aliases[0].volMax()-0.708f)<0.001f && std::fabs(l1.aliases[0].pitchMin()-1.f)<0.001f
           && l1.aliases[0].distMin()==150.f && l1.aliases[0].distMax()==1000.f,"alias levels");
    Expect(l2.aliases[0].sound.kind==SoundRef::Kind::Loaded && l2.aliases[0].sound.file=="sound\\vox\\test\\a.wav"
           && l2.aliases[1].sound.file=="sound\\vox\\test\\b.wav" && l2.aliases[1].sound.asset.data==bank.data2,"shared sound files resolved");
    Expect(l2.aliases[2].sound.kind==SoundRef::Kind::Loaded && l2.aliases[2].sound.asset.data==bank.data1,"shared loaded sound resolved");
    Expect(l3.aliases[0].sound.kind==SoundRef::Kind::Streamed && l3.aliases[0].sound.file=="sound\\mus\\test\\round.wav","streamed sound");
    Pcm p;
    Expect(Decode(l1.aliases[0].sound.asset,bank.raw.data()+bank.data1,p,&error) && p.frames()==300 && p.rate==44100
           && Get<std::int16_t>(bank.raw.data()+bank.data1+2)==p.samples[1],"PCM16 decode");
    // A broken header is refused, not walked.
    auto broken=bank.raw;Put<std::uint32_t>(broken.data()+37+8,5);
    Expect(!ParseBank(broken.data(),broken.size(),37,b,&error),"bank without inline alias lists refused");
}
void TestStreamedFile() {
    std::vector<std::uint8_t> f(0x830+400,0);
    Put<std::uint32_t>(f.data(),1);Put<std::uint32_t>(f.data()+4,200);Put<std::uint32_t>(f.data()+8,48000);Put<std::uint32_t>(f.data()+12,1);
    Put<std::uint32_t>(f.data()+16,0x830);Put<std::uint32_t>(f.data()+28,t5::Pcm16);Put<std::uint32_t>(f.data()+40,1);
    Put<std::uint32_t>(f.data()+48,400);Put<std::uint32_t>(f.data()+56,400);
    SndAsset a;std::string error;
    Expect(ParseStreamedFile(f.data(),f.size(),a,&error) && a.data==0x830 && a.dataSize==400 && a.seek.size()==1 && a.seek[0]==400,"streamed file header");
    Put<std::uint32_t>(f.data()+48,401);
    Expect(!ParseStreamedFile(f.data(),f.size(),a,&error),"streamed file overrun refused");
}

// -------------------------------------------------------------- MW2 zone
void TestZone() {
    SoundZoneSpec spec;
    Pcm p;p.channels=1;p.rate=44100;p.samples.assign(100,5);
    spec.sounds.push_back({"bo1/a.wav",p});
    ZoneAliasList l;l.name="zmb_test";ZoneAlias a;a.sound=0;a.flags=mw2::Flags(mw2::kChannelAuto);
    l.aliases={a,a};spec.lists={l};
    SoundZoneResult r;std::string error;
    Expect(BuildSoundZone(spec,r,&error),"sound zone builds");
    Expect(r.references==1 && r.pcmBytes==200 && r.zone.assets==1,"shared sound written once");
    const auto& s=r.zone.stream;
    Expect(Get<std::uint32_t>(s.data())==s.size()-40,"XFile size");
    Expect(Get<std::uint32_t>(s.data()+40)==1 && Get<std::uint32_t>(s.data()+56)==1 && Get<std::int32_t>(s.data()+80)==mw2::kAssetSound,"asset list");
    Expect(mw2::Flags(mw2::kChannelAuto)==0x680 && mw2::Flags(mw2::kChannelBody)==0x1c80 && mw2::Flags(mw2::kChannelAuto2d)==0x1480
           && mw2::Flags(mw2::kChannelEffects1)==0x2680,"flags match MW2's own aliases (c4_bounce, bodyfall, arcade, knife hit)");
    spec.lists[0].aliases[0].sound=3;
    Expect(!BuildSoundZone(spec,r,&error),"missing sound refused");
    spec.lists[0].aliases[0].sound=0;spec.sounds[0].pcm.samples.clear();
    Expect(!BuildSoundZone(spec,r,&error),"empty sound refused");
    const auto ff=mod::bo1model::FastFile(SoundZoneResult{}.zone);
    Expect(ff.size()>21 && !std::memcmp(ff.data(),"IWffu100",8),"fastfile header");
}
void TestDownmix() {
    Pcm p;p.channels=2;p.rate=48000;p.samples={100,300,-5,-6,32767,32767};
    const auto m=Downmix(p);
    Expect(m.channels==1 && m.samples.size()==3 && m.samples[0]==200 && m.samples[1]==-5 && m.samples[2]==32767,"stereo downmix");
}

// -------------------------------------------------------------- real data
std::vector<std::uint8_t> ReadAll(const char* path) {
    std::ifstream f(path,std::ios::binary);
    return f?std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),{}):std::vector<std::uint8_t>{};
}
void TestRealBanks() {
    const char* commonPath=std::getenv("BO1_COMMON_DEC");
    if(!commonPath) {std::cout<<"skip real banks (BO1_COMMON_DEC unset)\n";return;}
    const auto common=ReadAll(commonPath);
    Expect(!common.empty(),"common zone readable");
    const auto at=FindBank(common.data(),common.size(),kCommonBank);
    Bank b;std::string error;
    Expect(at && ParseBank(common.data(),common.size(),*at,b,&error),"zmb_common.all parses");
    Expect(b.lists.size()>2000 && b.unresolved==0 && b.shared>5000,"every shared sound resolved");
    std::vector<std::uint8_t> theater;
    if(const char* t=std::getenv("BO1_THEATER_DEC")) theater=ReadAll(t);
    SoundZoneSpec spec;SoundBuildReport report;
    Expect(BuildSoundSpec(common.data(),common.size(),theater.data(),theater.size(),{},spec,report,&error),"zombie sound spec");
    Expect(report.decodeFailures==0 && spec.lists.size()>=45,"zombie lists decode");
    for(const auto* name:{"zmb_vocals_zombie_ambience","zmb_vocals_zombie_sprint","zmb_vocals_zombie_attack","zmb_vocals_zombie_death",
                          "zmb_vocals_zombie_behind","zmb_zombie_spawn","zmb_attack_whoosh","fly_step_zombie_dirt","fly_step_zombie_default",
                          "zmb_hellhound_bolt","zmb_hellhound_explode","zmb_powerup_grabbed","zmb_spawn_powerup"})
        Expect(report.listNames.count(name)==1,name);
    for(const auto& s:spec.sounds) Expect(s.pcm.rate>=8000 && !s.pcm.samples.empty(),"decoded sound");
    std::cout<<"real banks: lists="<<spec.lists.size()<<" aliases="<<report.aliases<<" sounds="<<spec.sounds.size()
             <<" audio_s="<<report.seconds10/10.0<<"\n";
}
} // namespace

int main() {
    TestBitsAndVlc();
    TestImdct();
    TestWmaFixture();
    TestMsAdpcm();
    TestBank();
    TestStreamedFile();
    TestZone();
    TestDownmix();
    TestRealBanks();
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::cout<<"bo1_sound_tests passed\n";
    return 0;
}
