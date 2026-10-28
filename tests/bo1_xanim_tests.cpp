// Test39: BO1 (T5 v473) XAnimParts reader and MW2 x64 image builder.
// A synthetic zone exercises every part type; BO1_ZONE_DEC may name a real
// decompressed common_zombie zone (local only) for a full carve.
#include "bo1_xanim.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace mod::bo1xanim;
static int failures=0;
static void Check(bool ok,const char* what) {if(!ok){++failures;std::printf("FAIL %s\n",what);}}

struct Writer {
    std::vector<std::uint8_t> b;
    template<class T> void put(T v) {const auto* p=reinterpret_cast<const std::uint8_t*>(&v);b.insert(b.end(),p,p+sizeof(T));}
    void str(const char* s) {while(*s)b.push_back(static_cast<std::uint8_t>(*s++));b.push_back(0);}
    void bytes(std::initializer_list<std::uint8_t> v) {b.insert(b.end(),v);}
};

// Bones: 0 j_mainroot (NO_QUAT, NO_TRANS), 1 j_spine4 (FULL_QUAT, SMALL_TRANS),
// 2 j_head (FULL_QUAT_NO_SIZE, TRANS_NO_SIZE). 10 frames: byte indices.
static std::vector<std::uint8_t> SyntheticZone(std::uint16_t storedQuatSize=2) {
    Writer w;
    for(int n=0;n<9;++n)w.put<std::uint32_t>(0);               // XFile header (size patched below)
    const char* strings[]={nullptr,"j_mainroot","j_spine4","j_head","fire","sndnt#zmb_vox"};
    w.put<std::uint32_t>(6);w.put<std::uint32_t>(kT5Inline);w.put<std::uint32_t>(1);w.put<std::uint32_t>(kT5Inline);
    for(auto* s:strings)w.put<std::uint32_t>(s?kT5Inline:0);
    for(auto* s:strings)if(s)w.str(s);
    w.put<std::uint32_t>(kAssetTypeXAnim);w.put<std::uint32_t>(kT5Inline);   // asset list
    w.put<std::uint32_t>(0x11111111);                                       // unrelated stream bytes
    // XAnimParts header (104 bytes)
    w.put<std::uint32_t>(kT5Inline);
    w.put<std::uint16_t>(8);w.put<std::uint16_t>(6);w.put<std::uint16_t>(9);   // dataByte, dataShort, dataInt
    w.put<std::uint16_t>(6);w.put<std::uint16_t>(0);w.put<std::uint16_t>(10);  // randomDataByte, randomDataInt, numframes
    w.bytes({1,1,0,0});w.put<std::uint32_t>(0);                               // loop, delta, ik, streamable; streamed size
    w.bytes({1,0,1,0,1,1,0,1,1,3});                                          // boneCount[10]
    w.bytes({2,2,0,0,0,0});                                                  // notifyCount, assetType, isDefault, pad
    w.put<std::uint32_t>(12);w.put<std::uint32_t>(0);                        // randomDataShortCount, indexCount
    w.put<float>(30.f);w.put<float>(3.f);w.put<float>(0.f);w.put<float>(0.f);
    for(int n=0;n<10;++n)w.put<std::uint32_t>(n==6 || n==7?0:kT5Inline);     // randomDataInt, indices null
    w.str("ai_zombie_test");
    w.put<std::uint16_t>(1);w.put<std::uint16_t>(2);w.put<std::uint16_t>(3); // names
    w.put<std::uint16_t>(4);w.put<std::uint16_t>(0);w.put<float>(.5f);       // notify "fire" at 0.5
    w.put<std::uint16_t>(5);w.put<std::uint16_t>(0);w.put<float>(.1f);       // notify sound note
    w.put<std::uint32_t>(kT5Inline);w.put<std::uint32_t>(kT5Inline);         // deltaPart trans, quat
    w.put<std::uint16_t>(1);w.put<std::uint8_t>(1);w.put<std::uint8_t>(0);  // trans: size 1, small
    for(int n=0;n<6;++n)w.put<float>(static_cast<float>(n));                 // mins, size
    w.put<std::uint32_t>(kT5Inline);w.bytes({0,9});w.bytes({1,2,3,4,5,6});   // frames ptr, indices[2], frames[2][3]
    w.put<std::uint16_t>(0);w.put<std::uint16_t>(0);w.put<std::int16_t>(7);w.put<std::int16_t>(8);  // quat frame0
    w.bytes({0,4,9, 2, 0,9, 1, 0});                                          // dataByte
    w.put<std::uint16_t>(storedQuatSize);for(int n=0;n<4;++n)w.put<std::int16_t>(100);w.put<std::uint16_t>(1);  // dataShort
    for(int n=0;n<9;++n)w.put<float>(1.f);                                   // dataInt
    for(int n=0;n<12;++n)w.put<std::int16_t>(static_cast<std::int16_t>(n)); // randomDataShort
    w.bytes({10,11,12,13,14,15});                                            // randomDataByte
    w.put<std::uint32_t>(kT5Inline);                                         // next asset's name pointer
    const auto size=static_cast<std::uint32_t>(w.b.size()-kT5HeaderBytes);std::memcpy(w.b.data(),&size,4);
    return w.b;
}

int main() {
    auto zone=SyntheticZone();
    auto strings=ParseZoneStrings(zone.data(),zone.size());
    Check(strings && strings->strings.size()==6 && strings->strings[2]=="j_spine4" && strings->assetCount==1,"zone script strings");
    if(!strings) {std::printf("%d failures\n",failures);return 1;}
    std::size_t rejected=0;
    auto anims=CarveAnims(zone.data(),zone.size(),*strings,{"ai_zombie_test"},&rejected);
    Check(anims.size()==1 && rejected==0,"synthetic anim carved");
    if(anims.empty()) {std::printf("%d failures\n",failures);return 1;}
    const auto& a=anims[0];
    Check(a.numframes==10 && a.loop && a.delta && a.byteIndices() && a.framerate==30.f,"header fields");
    Check(a.bones==std::vector<std::string>({"j_mainroot","j_spine4","j_head"}),"bone names via zone strings");
    Check(a.notify.size()==2 && a.notetrack("fire")==.5f,"notetracks");
    Check(a.trans.present && a.trans.size==1 && a.trans.smallTrans==1 && a.trans.frames.size()==6 && a.trans.indices.size()==2,"delta translation");
    Check(a.quat.present && a.quat.size==0 && a.quat.frame0[0]==7 && a.quat.frame0[1]==8,"delta yaw");
    Check(ValidateFlat(a),"flat pools consumed exactly");
    Check(std::abs(a.lengthSeconds()-10.f/30.f)<1e-6f,"length");

    // A wrong stored frame count changes the pool demand: rejected.
    auto bad=SyntheticZone(3);auto badStrings=ParseZoneStrings(bad.data(),bad.size());std::size_t badRejected=0;
    Check(badStrings && CarveAnims(bad.data(),bad.size(),*badStrings,{"ai_zombie_test"},&badRejected).empty() && badRejected==1,"inconsistent pools rejected");
    Check(CarveAnims(zone.data(),zone.size(),*strings,{"ai_zombie_other"}).empty(),"unwanted names skipped");

    std::unordered_map<std::string,std::uint16_t> ids;std::uint16_t next=40;
    auto intern=[&](const std::string& s){auto it=ids.find(s);if(it!=ids.end())return it->second;return ids[s]=next++;};
    Image image;
    Check(Build(a,intern,0x1122334455667788ull,image),"image built");
    const auto* h=image.header.data();
    Check(Get<std::uint64_t>(h)==0x1122334455667788ull,"MW2 name pointer kept");
    Check(Get<std::uint16_t>(h+0x12)==10 && Get<float>(h+0x2c)==30.f && h[0x1f]==2 && h[0x14]==(mw2::kFlagLoop|mw2::kFlagDelta),"engine-read offsets (numframes, framerate, notifyCount, flags)");
    Check(h[0x15+2]==1 && h[0x15+9]==3 && Get<std::uint32_t>(h+0x24)==12 && Get<std::uint16_t>(h+0x08)==8,"counts");
    Check(image.names.size()==3 && image.names[0]==ids["j_mainroot"],"bone names interned in order");
    Check(Get<std::uint16_t>(image.notify.data())==ids["fire"] && Get<float>(image.notify.data()+4)==.5f,"notify records");
    Check(Get<std::uint64_t>(h+0x78)==Address(image.notify.data()) && Get<std::uint64_t>(h+0x38)==Address(image.names.data()),"notify and names pointers");
    Check(Get<std::uint64_t>(h+0x68)==0 && Get<std::uint64_t>(h+0x70)==0,"absent pools stay null");
    const auto delta=Get<std::uint64_t>(h+0x80);
    Check(delta==Address(image.deltaPart.data()),"delta part pointer");
    const auto* trans=reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(Get<std::uint64_t>(image.deltaPart.data())));
    Check(trans==image.trans.data() && Get<std::uint16_t>(trans)==1 && trans[2]==1 && Get<float>(trans+8)==0.f && Get<float>(trans+20)==3.f,"x64 XAnimPartTrans: union at +8");
    Check(Get<std::uint64_t>(trans+32)==Address(image.transFrames.data()) && trans[40]==0 && trans[41]==9,"frames pointer at +32, inline indices at +40");
    const auto* quat=reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(Get<std::uint64_t>(image.deltaPart.data()+8)));
    Check(quat==image.quat2.data() && Get<std::int16_t>(quat+8)==7 && Get<std::uint64_t>(image.deltaPart.data()+16)==0,"yaw-only delta becomes quat2; 3D quat null");
    Check(ImageMatches(image,a),"image self-check");
    Check(!Build(a,[](const std::string&){return std::uint16_t{0};},0,image),"refused when a name cannot be interned");

    // Test41: the delta flag without any delta part (BO1 ai_zombie_attack_v1/v4/v6).
    // MW2's sampler reads deltaPart whenever the flag is set, so the image is
    // written without root motion instead of with a null deltaPart.
    {
        Anim noDelta=a;noDelta.trans=DeltaTrans{};noDelta.quat=DeltaQuat{};
        Image img;
        Check(noDelta.delta && !HasDelta(noDelta) && HasDelta(a),"delta flag needs a delta part");
        Check(Build(noDelta,intern,0,img) && img.header[0x14]==mw2::kFlagLoop && Get<std::uint64_t>(img.header.data()+0x80)==0,"flagged anim without parts written as non-delta");
        Check(ImageMatches(img,noDelta),"non-delta self-check");
        img.header[0x14]|=mw2::kFlagDelta;
        Check(!ImageMatches(img,noDelta),"self-check refuses delta flag with null deltaPart");
    }

    if(const char* path=std::getenv("BO1_ZONE_DEC")) {
        std::ifstream f(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(f)),{});
        auto z=ParseZoneStrings(raw.data(),raw.size());
        const std::unordered_set<std::string> want={"ai_zombie_walk_v1","ai_zombie_sprint_v1","ai_zombie_attack_v1",
            "ai_zombie_traverse_ground_v1_walk","ai_zombie_traverse_ground_climbout_fast","ai_zombie_death_v1","ai_zombie_idle_v1"};
        std::size_t rej=0;auto real=z?CarveAnims(raw.data(),raw.size(),*z,want,&rej):std::vector<Anim>{};
        Check(real.size()==want.size() && rej==0,"real zone: all wanted anims carved");
        for(const auto& r:real) {
            Image img;Check(Build(r,intern,0,img) && ImageMatches(img,r),r.name.c_str());
            if(r.name=="ai_zombie_attack_v1") Check(r.delta && !HasDelta(r) && !(img.header[0x14]&mw2::kFlagDelta),"attack_v1 has no root motion");
        }
        std::printf("real zone: %zu anims\n",real.size());
    }
    std::printf("%s (%d failures)\n",failures?"FAILED":"bo1_xanim_tests passed",failures);
    return failures?1:0;
}
