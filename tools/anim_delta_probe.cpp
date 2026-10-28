// Test41: run the retail x64 root-motion sampler (XAnimGetRelDelta 0x29cbb0
// -> XAnimCalcRelDeltaParts 0x29b200) on every BO1 animation the mod
// converts, in the same private Linux MS-ABI image as the other probes.
// Each anim is sampled at many start/end pairs under a watchdog alarm, so a
// sampler that never returns, reads outside the converted pools, or returns a
// non-finite delta is caught here instead of in game. The BO1 zone is the
// owner's local decompressed common_zombie.ff (never committed).
#include "script_call.hpp"
#include "native_hooks.hpp"
#include "zombie_bindings.hpp"
#include "bo1_xanim.hpp"
#include <sys/mman.h>
#include <csetjmp>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::uintptr_t kBase=0x140000000;
constexpr std::size_t kSize=0x5000000,kScratch=0x4f00000;
constexpr std::uintptr_t kXAnimGetRelDeltaRva=0x29cbb0;
sigjmp_buf timeout;
volatile sig_atomic_t phase=0;
void OnSignal(int signal) {siglongjmp(timeout,signal);}
void Check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
using RelDeltaFn=void(MOD_NATIVE_ABI*)(std::uintptr_t,unsigned,float*,float*,float,float);

// One engine call under the watchdog alarm; nonzero is the signal caught.
int Sample(RelDeltaFn delta,std::uintptr_t tree,float* rot,float* tr,float start,float end) {
    alarm(2);
    if(const int s=sigsetjmp(timeout,1)) return s;
    delta(tree,1,rot,tr,start,end);
    alarm(0);
    return 0;
}
// Synthetic anims covering every root-motion shape the converter writes:
// none (with the BO1 delta flag set), translation only, yaw + translation,
// small (u8) translation frames, and wide (u16) key indices past 255 frames.
mod::bo1xanim::Anim Synthetic(const char* name,std::uint16_t frames,bool trans,bool quat,bool small) {
    mod::bo1xanim::Anim a;a.name=name;a.numframes=frames;a.framerate=30.f;a.frequency=30.f/frames;a.delta=true;
    const bool wide=frames>=256;
    auto indices=[&](std::size_t keys) {
        std::vector<std::uint8_t> out;
        for(std::size_t k=0;k<keys;++k) {
            const auto frame=static_cast<std::uint16_t>(k*frames/(keys-1));
            out.push_back(static_cast<std::uint8_t>(frame));if(wide)out.push_back(static_cast<std::uint8_t>(frame>>8));
        }
        return out;
    };
    if(trans) {
        auto& t=a.trans;t.present=true;t.size=7;t.smallTrans=small?1:0;t.mins={0.f,0.f,0.f};t.step={1.f,1.f,0.5f};
        t.indices=indices(8);
        for(int k=0;k<8;++k) for(int c=0;c<3;++c) {
            const auto v=static_cast<std::uint16_t>(k*(small?30:9000));
            t.frames.push_back(static_cast<std::uint8_t>(v));if(!small)t.frames.push_back(static_cast<std::uint8_t>(v>>8));
        }
    }
    if(quat) {
        auto& q=a.quat;q.present=true;q.size=4;q.indices=indices(5);
        for(int k=0;k<5;++k) {
            const auto s=static_cast<std::int16_t>(k*1000),c=static_cast<std::int16_t>(32767-k*20);
            for(const auto v:{s,c}) {q.frames.push_back(static_cast<std::uint8_t>(v));q.frames.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(v)>>8));}
        }
    }
    return a;
}
}

int main(int argc,char** argv) {
    if(argc<3)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4071280 && bytes.size()<kScratch,"Invalid private image");
        const bool synthetic=!std::strcmp(argv[2],"--synthetic");
        auto* image=static_cast<std::uint8_t*>(mmap(reinterpret_cast<void*>(kBase),kSize,PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
        Check(image==reinterpret_cast<std::uint8_t*>(kBase),"Preferred image base unavailable");
        std::memcpy(image,bytes.data(),bytes.size());
        auto* scratch=image+kScratch;std::size_t used=0;
        auto alloc=[&](std::size_t n){auto* p=scratch+used;used=(used+n+15)&~std::size_t(15);Check(used<kSize-kScratch,"scratch");return p;};

        std::vector<mod::bo1xanim::Anim> anims;
        if(synthetic) {
            anims.push_back(Synthetic("synthetic_flag_without_parts",54,false,false,false));
            anims.push_back(Synthetic("synthetic_trans",94,true,false,false));
            anims.push_back(Synthetic("synthetic_trans_yaw",70,true,true,false));
            anims.push_back(Synthetic("synthetic_small_trans",215,true,true,true));
            anims.push_back(Synthetic("synthetic_wide_indices",303,true,true,false));
        } else {
            std::ifstream zoneFile(argv[2],std::ios::binary);
            std::vector<std::uint8_t> zone((std::istreambuf_iterator<char>(zoneFile)),{});
            Check(!zone.empty(),"BO1 zone unreadable");
            std::unordered_set<std::string> wanted;
            for(int i=3;i<argc;++i) wanted.insert(argv[i]);
            const auto strings=mod::bo1xanim::ParseZoneStrings(zone.data(),zone.size());
            Check(strings.has_value(),"BO1 zone strings unreadable");
            anims=mod::bo1xanim::CarveAnims(zone.data(),zone.size(),*strings,wanted);
            Check(anims.size()==wanted.size(),"not every requested anim carved");
        }

        struct sigaction action{};action.sa_handler=OnSignal;sigemptyset(&action.sa_mask);
        sigaction(SIGALRM,&action,nullptr);sigaction(SIGSEGV,&action,nullptr);sigaction(SIGBUS,&action,nullptr);
        const auto delta=reinterpret_cast<RelDeltaFn>(image+kXAnimGetRelDeltaRva);
        std::vector<std::unique_ptr<mod::bo1xanim::Image>> images;
        std::cout<<"{\"probe\":\"exact-native root motion (XAnimGetRelDelta 0x29cbb0)\",\"source\":\""<<(synthetic?"synthetic":"owner BO1 zone (local)")<<"\",\"anims\":[";
        bool first=true;unsigned failures=0;
        for(const auto& a:anims) {
            images.push_back(std::make_unique<mod::bo1xanim::Image>());
            auto& converted=*images.back();
            char* name=reinterpret_cast<char*>(alloc(a.name.size()+1));std::memcpy(name,a.name.c_str(),a.name.size()+1);
            std::uint16_t next=100;
            Check(mod::bo1xanim::Build(a,[&](const std::string&){return next++;},mod::bo1xanim::Address(name),converted),a.name+": not built");
            auto* parts=alloc(mod::bo1xanim::kMw2XAnimBytes);std::memcpy(parts,converted.header.data(),converted.header.size());
            auto* tree=alloc(8+2*mod::kXAnimEntryStride);std::memset(tree,0,8+2*mod::kXAnimEntryStride);
            *reinterpret_cast<std::uint32_t*>(tree)=2;
            *reinterpret_cast<std::uint16_t*>(tree+mod::kXAnimEntries)=1;   // root blend node
            *reinterpret_cast<std::uint64_t*>(tree+mod::kXAnimEntries+mod::kXAnimEntryStride+mod::kXAnimEntryParts)=mod::bo1xanim::Address(parts);
            unsigned samples=0,nonFinite=0;const char* fault=nullptr;
            float total[2]{},trans[3]{},maxStep=0.f;
            const int steps=4*a.numframes;
            for(int i=0;i<=steps && !fault;++i) {
                const float end=static_cast<float>(i)/static_cast<float>(steps);
                for(const float start:{0.f,end*0.5f,end>0.01f?end-0.01f:0.f}) {
                    float rot[2]{},tr[3]{};
                    if(const int s=Sample(delta,reinterpret_cast<std::uintptr_t>(tree),rot,tr,start,end)) {fault=s==SIGALRM?"timeout":"fault";break;}
                    ++samples;
                    if(!std::isfinite(rot[0]) || !std::isfinite(rot[1]) || !std::isfinite(tr[0]) || !std::isfinite(tr[1]) || !std::isfinite(tr[2])) ++nonFinite;
                    if(start==0.f && i==steps) {total[0]=rot[0];total[1]=rot[1];std::memcpy(trans,tr,sizeof(trans));}
                    if(start>0.f && end-start<0.011f) maxStep=std::max(maxStep,std::sqrt(tr[0]*tr[0]+tr[1]*tr[1]+tr[2]*tr[2]));
                }
            }
            alarm(0);
            const bool ok=!fault && !nonFinite;
            if(!ok) ++failures;
            char line[512];
            std::snprintf(line,sizeof(line),"%s{\"name\":\"%s\",\"frames\":%u,\"bo1_delta_flag\":%d,\"root_motion\":%d,\"wide_indices\":%d,\"samples\":%u,\"non_finite\":%u,\"fault\":\"%s\","
                "\"yaw_sin_cos\":[%.4f,%.4f],\"trans\":[%.2f,%.2f,%.2f],\"max_step\":%.2f}",
                first?"":",",a.name.c_str(),a.numframes,a.delta,mod::bo1xanim::HasDelta(a),a.numframes>=256,samples,nonFinite,fault?fault:"none",total[0],total[1],trans[0],trans[1],trans[2],maxStep);
            std::cout<<line;first=false;
        }
        std::cout<<"],\"failures\":"<<failures<<"}\n";
        munmap(image,kSize);
        return failures?1:0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
