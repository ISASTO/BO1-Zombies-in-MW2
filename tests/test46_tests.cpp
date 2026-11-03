// Test46: the weapon-option alignment fix, the feature-update cadence, the
// level-dialogue classification, the floor-sink rule, the hellhound bite
// notetrack, IWI bitmap formats, and the BO1 effect conversion (a synthetic
// Black Ops zone with misc/fx_zombie_powerup_on: parsing, alias tables,
// elem type and flag mapping, MW2 effect materials, and the generated zone).
// With BO1_COMMON_DEC / BO1_THEATER_DEC set to the owner's inflated zones,
// every effect and the hellhound are carved from the real ones too.
#include "control_extensions.hpp"
#include "zombie_level_quiet.hpp"
#include "bo1_zombie_rules.hpp"
#include "bo1_fx.hpp"
#include "bo1_extra_models.hpp"
#include "bo1_model_synthetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures=0;
void Check(bool ok,const char* what) {if(!ok){std::printf("FAIL: %s\n",what);++failures;}}
using mod::bo1xanim::Put;
using mod::bo1xanim::Get;

void TestAlignmentAndCadence() {
    using namespace mod;
    // Zone assets are 4-byte aligned: a weapon at ...4 is valid.
    const std::uintptr_t weapon=0x140123454;
    Check((weapon&kZoneAssetAlignMask)==0,"a 4-byte aligned weapon definition is accepted");
    Check(((weapon+2)&kZoneAssetAlignMask)!=0,"a misaligned pointer is still refused");
    Check(FeatureUpdateInterval(true,false)==kFeatureUpdateActiveMs && FeatureUpdateInterval(false,true)==kFeatureUpdateMenuMs
          && FeatureUpdateInterval(false,false)==kFeatureUpdateIdleMs,"player features update often only when something is enabled");
    Check(kFeatureUpdateActiveMs<kFeatureUpdateMenuMs && kFeatureUpdateMenuMs<kFeatureUpdateIdleMs,"cadence order");
}

void TestQuietLevel() {
    using namespace mod::quiet;
    Check(DialogueFile("voiceovers/so_ac130/so_ac130_hqr_start.wav") && DialogueFile("Voiceovers\\Favela\\x.wav"),"voiceover files are dialogue");
    Check(!DialogueFile("weapons/ak47/fire.wav") && !DialogueFile("bo1/zmb/vox.wav"),"other files are not");
    Check(DialogueAlias("US_1_order_move_generic") && DialogueAlias("ru_3_inform_reloading") && DialogueAlias("so_hqr_ready_up")
          && DialogueAlias("fb_hint") && DialogueAlias("generic_pain_american_1"),"battle chatter and Spec Ops lines are dialogue");
    Check(!DialogueAlias("zmb_vocals_zombie_attack") && !DialogueAlias("weap_ak47_fire_npc") && !DialogueAlias("us_flag"),"zombie and weapon sounds are not");
    Check(Dialogue("anything","voiceovers/a.wav") && Dialogue("ru_1_inform_x","") && !Dialogue("zmb_hellhound_bolt","bo1/x.wav"),"either rule makes dialogue");
}

void TestFloorAndBite() {
    using namespace mod::bo1;
    Check(SunkBelowFloor(false,.3f,100.f,80.f),"a floor 20 units above the feet is a sink");
    Check(!SunkBelowFloor(false,.3f,85.f,80.f),"a step within the margin is not");
    Check(!SunkBelowFloor(true,.3f,100.f,80.f) && !SunkBelowFloor(false,1.f,100.f,80.f),"start-solid or no floor found: no decision");
    std::vector<std::pair<std::string,float>> notes={{"sound_dog_attack_jump",.11f},{"stop_tracking",.16f},{"dog_melee",.2f},{"end",1.f}};
    int out[4]{};
    Check(AttackFireTimes(notes,1500,out,4)==1 && out[0]==300,"a hellhound bites at its dog_melee notetrack");
}

std::vector<std::uint8_t> IwiFile(std::uint8_t format,std::uint16_t w,std::uint16_t h,std::size_t pixelBytes) {
    std::vector<std::uint8_t> f={'I','W','i',13,format,2};      // no mipmaps
    auto put16=[&](std::uint16_t v){f.push_back(static_cast<std::uint8_t>(v));f.push_back(static_cast<std::uint8_t>(v>>8));};
    put16(w);put16(h);put16(1);f.resize(48,0);
    for(std::size_t k=0;k<static_cast<std::size_t>(w)*h*pixelBytes;++k) f.push_back(static_cast<std::uint8_t>(k*7+1));
    return f;
}
void TestIwiBitmaps() {
    using namespace mod::bo1model;
    Texture t;
    auto rgba=IwiFile(1,4,2,4);
    Check(ParseIwi13(rgba.data(),rgba.size(),t) && t.format==kD3dA8R8G8B8 && t.data.size()==32 && t.data[0]==1,"IWI RGBA is A8R8G8B8 as stored");
    auto rgb=IwiFile(2,2,2,3);
    Check(ParseIwi13(rgb.data(),rgb.size(),t) && t.format==kD3dA8R8G8B8 && t.data.size()==16 && t.data[3]==0xff && t.data[4]==rgb[51],
          "IWI RGB widens to A8R8G8B8 with opaque alpha");
    auto la=IwiFile(3,4,4,2);
    Check(ParseIwi13(la.data(),la.size(),t) && t.format==kD3dA8L8 && t.data.size()==32,"IWI luminance+alpha is A8L8");
    auto l=IwiFile(4,4,4,1),a=IwiFile(5,4,4,1);
    Check(ParseIwi13(l.data(),l.size(),t) && t.format==kD3dL8 && ParseIwi13(a.data(),a.size(),t) && t.format==kD3dA8,"IWI luminance and alpha");
    auto wavelet=IwiFile(6,4,4,4);
    Check(!ParseIwi13(wavelet.data(),wavelet.size(),t),"wavelet images are refused");
    Check(MipBytes(kD3dA8R8G8B8,4,4)==64 && MipBytes(kD3dA8L8,2,1)==4 && MipBytes(kD3dA8,1,1)==1 && MipBytes(kD3dDxt1,4,4)==8,"mip sizes per format");
}

// --------------------------------------------- a synthetic Black Ops zone
struct Zone {
    std::vector<std::uint8_t> bytes;
    void raw(const void* p,std::size_t n) {const auto* b=static_cast<const std::uint8_t*>(p);bytes.insert(bytes.end(),b,b+n);}
    void u32(std::uint32_t v) {raw(&v,4);}
    void str(const std::string& s) {raw(s.c_str(),s.size()+1);}
    void pad(std::size_t n,std::uint8_t v=0x5a) {bytes.insert(bytes.end(),n,v);}
};
std::array<std::uint8_t,32> Constant(std::uint32_t hash,const char* name,float a,float b) {
    std::array<std::uint8_t,32> c{};Put<std::uint32_t>(c.data(),hash);std::memcpy(c.data()+4,name,std::min<std::size_t>(12,std::strlen(name)));
    Put<float>(c.data()+16,a);Put<float>(c.data()+20,b);Put<float>(c.data()+24,a);Put<float>(c.data()+28,b);
    return c;
}
// A T5 inline material: header, name, one colour map (image inline), constants, state bits.
void Material(Zone& z,const std::string& name,const std::string& image,std::uint8_t rows,std::uint8_t cols,
              const std::vector<std::array<std::uint8_t,32>>& constants,std::uint64_t stateBits0) {
    std::array<std::uint8_t,192> h{};
    Put<std::uint32_t>(h.data(),0xffffffffu);h[9]=0x30;h[10]=rows;h[11]=cols;
    h[170]=1;h[171]=static_cast<std::uint8_t>(constants.size());h[172]=2;
    Put<std::uint32_t>(h.data()+176,0x80001235u);Put<std::uint32_t>(h.data()+180,0xffffffffu);
    Put<std::uint32_t>(h.data()+184,constants.empty()?0u:0xffffffffu);Put<std::uint32_t>(h.data()+188,0xffffffffu);
    z.raw(h.data(),h.size());z.str(name);
    std::array<std::uint8_t,16> d{};Put<std::uint32_t>(d.data(),mod::bo1model::kHashColorMap);d[4]='c';d[5]='p';d[6]=0xea;d[7]=2;
    Put<std::uint32_t>(d.data()+12,0xffffffffu);z.raw(d.data(),d.size());
    std::array<std::uint8_t,52> img{};Put<std::uint32_t>(img.data(),0xffffffffu);Put<std::uint32_t>(img.data()+44,0xffffffffu);
    Put<std::uint16_t>(img.data()+20,32);Put<std::uint16_t>(img.data()+22,32);
    z.raw(img.data(),img.size());z.str(image);
    z.u32(0);z.u32(0x31545844u);z.u32(0);       // loadDef: streamed (no data in the zone)
    for(const auto& c:constants) z.raw(c.data(),c.size());
    const std::uint64_t second=0xe49e492c88128812ull;
    z.raw(&stateBits0,8);z.raw(&second,8);
}
std::array<std::uint8_t,0x124> Elem(std::uint8_t type,std::uint32_t flags,std::uint8_t vc,bool vel,bool vis,std::uint32_t visual) {
    std::array<std::uint8_t,0x124> e{};
    for(std::size_t k=4;k<152;++k) e[k]=static_cast<std::uint8_t>(k);
    Put<std::uint32_t>(e.data(),flags);
    Put<float>(e.data()+156,0.f);Put<float>(e.data()+160,0.f);
    e[172]=1;e[175]=1;Put<std::uint16_t>(e.data()+178,0x0201);    // atlas: entry count 1 (index range 2)
    e[184]=type;e[185]=vc;e[186]=0;e[187]=1;
    Put<std::uint32_t>(e.data()+188,vel?0xffffffffu:0u);Put<std::uint32_t>(e.data()+192,vis?0xffffffffu:0u);
    Put<std::uint32_t>(e.data()+196,visual);
    for(std::size_t k=0;k<3;++k) {Put<float>(e.data()+200+4*k,-2.f);Put<float>(e.data()+212+4*k,6.f);}
    e[260]=5;e[261]=0;
    return e;
}
std::vector<std::uint8_t> VisSamples(float w,float h) {
    std::vector<std::uint8_t> v(2*48,0);
    for(std::size_t s=0;s<2;++s) {v[48*s]=128;v[48*s+1]=255;v[48*s+2]=128;v[48*s+3]=255;Put<float>(v.data()+48*s+12,w);Put<float>(v.data()+48*s+16,h);}
    return v;
}
std::vector<std::uint8_t> PowerupZone() {
    using namespace mod;
    Zone z;z.pad(64);
    // misc/fx_zombie_powerup_on: a still tail (inline material), a billboard
    // and a flare (aliased material), an omni light; 3 looping + 1 one-shot.
    std::array<std::uint8_t,60> h{};
    Put<std::uint32_t>(h.data(),0xffffffffu);h[4]=0;Put<std::int32_t>(h.data()+8,2406);Put<std::int32_t>(h.data()+12,0x7fffffff);
    Put<std::int32_t>(h.data()+16,3);Put<std::int32_t>(h.data()+20,1);Put<std::int32_t>(h.data()+24,0);Put<std::uint32_t>(h.data()+28,0xffffffffu);
    z.raw(h.data(),h.size());z.str("misc/fx_zombie_powerup_on");
    const auto e0=Elem(bo1fx::t5::Tail,0x11000192u,1,true,true,0xffffffffu);
    const auto e1=Elem(bo1fx::t5::Billboard,0x11000192u,1,false,true,0x81d99b89u);
    const auto e2=Elem(bo1fx::t5::Omni,0x04000082u,1,false,true,0);
    const auto e3=Elem(bo1fx::t5::Billboard,0x84000882u,1,false,true,0x81d99b89u);
    for(const auto* e:{&e0,&e1,&e2,&e3}) z.raw(e->data(),e->size());
    // elem 0: velocity (zero: a still tail), visual states, its material.
    z.pad(96,0);
    const auto vis0=VisSamples(23.f,1.6f);z.raw(vis0.data(),vis0.size());
    Material(z,"gfx_fxt_muz_vert_gen","fxt_muz_vert_gen",1,4,
             {Constant(bo1fx::kHashColorTint,"colorTint",1.f,1.f),Constant(bo1fx::kHashFeatherParms,"featherParms",.025f,40.f)},0x0000000c19218922ull);
    const auto vis=VisSamples(8.f,8.f);
    z.raw(vis.data(),vis.size());                 // elem 1
    z.raw(vis.data(),vis.size());                 // elem 2 (light)
    z.raw(vis.data(),vis.size());                 // elem 3
    z.pad(300);
    // The aliased material, defined elsewhere in the zone (no depth test).
    Material(z,"gfx_fxt_light_flare_halogen","fxt_light_flare_halogen",1,1,{Constant(bo1fx::kHashColorTint,"colorTint",1.f,.5f)},0x0000000219218922ull);
    z.pad(64);
    return z.bytes;
}

void TestBlendMapping() {
    using namespace mod::bo1fx;
    Check(BlendOf(0x19218922u)==Blend::Add && BlendOf(0x19289165u)==Blend::Blend && BlendOf(0x1921892au)==Blend::Screen && BlendOf(0x00128812u)==Blend::None,
          "Black Ops blend factors");
    Check(std::string(TechniqueFor(Blend::Add,true).techniqueSet)=="effect_zfeather_add" && TechniqueFor(Blend::Add,true).loadBits0==0x09218922u
          && std::string(TechniqueFor(Blend::Blend,false).techniqueSet)=="effect_blend" && !TechniqueFor(Blend::None,false).techniqueSet,
          "MW2 effect technique sets and state bits");
    Check(DepthBits(0x0004004cu)==0xcu && DepthBits(0x2u)==0x2u,"depth test kept, stencil dropped");
    Check(std::string(EffectName(Effect::DogEyes))=="maps/zombie/fx_zombie_dog_eyes" && kEffects.size()==static_cast<std::size_t>(Effect::Count),"effect names");
    Check(AliasedMaterial("misc/fx_zombie_powerup_on",1,0)==std::string("gfx_fxt_light_flare_halogen") && !AliasedMaterial("misc/fx_zombie_powerup_on",2,0),
          "aliased visuals resolve by name, unknown ones stay unresolved");
    Check(AliasedImage("gfx_fxt_smk_gen_z20")==std::string("fxt_smk_gen") && !AliasedImage("gfx_unknown"),"aliased colour maps");
}

void TestEffectConversion() {
    using namespace mod;
    const auto raw=PowerupZone();
    const auto plan=bo1fx::PlanEffects(raw.data(),raw.size());
    Check(plan.effects.size()==1 && plan.effects[0].name=="misc/fx_zombie_powerup_on" && plan.effects[0].elems.size()==4,"the powerup glow parses");
    Check(plan.materials.count("gfx_fxt_muz_vert_gen") && plan.materials.count("gfx_fxt_light_flare_halogen"),"inline and aliased materials found");
    Check(plan.images()==std::set<std::string>{"fxt_muz_vert_gen","fxt_light_flare_halogen"},"their colour maps");
    std::map<std::string,bo1model::Texture> textures;
    textures["fxt_muz_vert_gen"]=bo1model::synthetic::Dxt1(32,32,3);textures["fxt_light_flare_halogen"]=bo1model::synthetic::Dxt1(16,16,4);
    bo1model::ZoneSpec spec;bo1fx::Report report;std::string error;
    Check(bo1fx::AppendEffects(plan,textures,spec,report,&error) && spec.effects.size()==1,"effects append");
    if(spec.effects.size()!=1) return;
    const auto& fx=spec.effects[0];
    Check(fx.counts[0]==3 && fx.counts[1]==1 && fx.counts[2]==0 && fx.elems.size()==4 && fx.loopingLife==0x7fffffff,"elem groups kept");
    const auto& e0=fx.elems[0];const auto& e1=fx.elems[1];const auto& e2=fx.elems[2];const auto& e3=fx.elems[3];
    Check(e0.raw[0xb0]==bo1fx::mw2::Billboard && (Get<std::uint32_t>(e0.raw.data())&bo1fx::mw2::kNonUniformScale),"a still tail becomes a non-uniform billboard");
    Check(e2.raw[0xb0]==bo1fx::mw2::Omni && e2.raw[0xb1]==1 && e2.materials.empty(),"the light keeps one visual slot and no material");
    Check((Get<std::uint32_t>(e3.raw.data())&0x80000800u)==0 && (Get<std::uint32_t>(e3.raw.data())&0x04000082u)==0x04000082u,"T5-only flags dropped, shared ones kept");
    Check(e1.materials.size()==1 && e3.materials.size()==1 && e1.materials[0]==e3.materials[0] && e0.materials[0]!=e1.materials[0],"materials shared by name");
    Check(Get<float>(e0.raw.data()+0xd0)==2.f && Get<float>(e0.raw.data()+0xdc)==4.f,"collision bounds become centre and half size");
    Check(e0.raw[0x118]==5 && Get<std::uint16_t>(e0.raw.data()+0xae)==1,"sort order and atlas entry count");
    bool copied=true;for(std::size_t k=4;k<152;++k) if(e1.raw[k]!=static_cast<std::uint8_t>(k)) copied=false;
    Check(copied,"spawn, life, origin, angles and rotation copied field by field");
    const auto& m0=spec.effectMaterials[e0.materials[0]];const auto& m1=spec.effectMaterials[e1.materials[0]];
    Check(m0.techniqueSet=="effect_zfeather_add" && m0.stateBits[0]==0x0000000c09218922ull && m0.atlasColumns==4 && m0.sampler==0xea,"feathered additive material");
    Check(m0.constants.size()==2 && Get<std::uint32_t>(m0.constants[0].data())==bo1fx::kHashFeatherParms && Get<std::uint32_t>(m0.constants[1].data())==bo1fx::kHashColorTint,
          "constants sorted by hash (featherParms, colorTint)");
    Check(m1.techniqueSet=="effect_add" && m1.stateBits[0]==0x0000000219218922ull && m1.constants.size()==1 && Get<float>(m1.constants[0].data()+20)==.5f,
          "plain additive material without depth test keeps its tint");
    Check(spec.images.count("bo1fx_fxt_muz_vert_gen") && spec.images.count("bo1fx_fxt_light_flare_halogen") && m0.image=="bo1fx_fxt_muz_vert_gen","images named for the zone");
    bo1model::ZoneResult zone;
    Check(bo1model::BuildZone(spec,zone,&error) && zone.assets==1,"the effect zone builds");
    // Without the textures only the light survives.
    bo1model::ZoneSpec bare;bo1fx::Report bareReport;
    Check(bo1fx::AppendEffects(plan,{},bare,bareReport) && bare.effects.size()==1 && bare.effects[0].elems.size()==1 && bare.effects[0].counts[0]==1
          && bare.effects[0].counts[1]==0 && bareReport.droppedElems==3,"elems without their image are dropped, counts follow");
    // A line much longer than wide is dropped; a short one is a billboard.
    bo1fx::T5Elem line;line.raw[184]=bo1fx::t5::Line;line.vis=VisSamples(10.f,250.f);
    bool nonUniform=false;
    Check(!bo1fx::MapElemType(line,nonUniform),"a long still line is dropped");
    line.vis=VisSamples(250.f,750.f);
    Check(bo1fx::MapElemType(line,nonUniform)==bo1fx::mw2::Billboard && nonUniform,"a short still line is a billboard");
}

void TestRealZones() {
    using namespace mod;
    if(const char* path=std::getenv("BO1_COMMON_DEC")) {
        std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
        const auto plan=bo1fx::PlanEffects(raw.data(),raw.size());
        std::map<std::string,bo1model::Texture> textures;std::uint8_t seed=1;
        for(const auto& n:plan.images()) textures[n]=bo1model::synthetic::Dxt1(16,16,seed++);
        bo1model::ZoneSpec spec;bo1fx::Report report;std::string error;
        Check(plan.effects.size()==bo1fx::kEffects.size(),"every effect is in common_zombie.ff");
        Check(bo1fx::AppendEffects(plan,textures,spec,report,&error) && report.effects==bo1fx::kEffects.size() && report.elems>=60,"every effect converts");
        bo1model::ZoneResult zone;
        Check(bo1model::BuildZone(spec,zone,&error),"the real effects build");
        std::printf("real effects: %zu effects, %zu elems (%zu dropped), %zu materials, %zu images\n",report.effects,report.elems,report.droppedElems,
                    report.materials,report.images);
    }
    if(const char* path=std::getenv("BO1_THEATER_DEC")) {
        std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
        const auto strings=bo1xanim::ParseZoneStrings(raw.data(),raw.size());
        std::string error;
        const auto wolf=strings?bo1model::CarveWolf(raw.data(),raw.size(),strings->strings,&error):std::nullopt;
        Check(wolf && wolf->model.numBones==58 && wolf->model.hasBone("tag_eye") && wolf->model.hasBone("j_helmet")
              && std::count(wolf->skins.begin(),wolf->skins.end(),bo1model::WolfSkin::Eyes)>0,"the hellhound carves");
        if(wolf) std::printf("hellhound: %u surfaces, %zu bones\n",wolf->model.numsurfs,wolf->model.bones.size());
    }
}
} // namespace

int main() {
    TestAlignmentAndCadence();
    TestQuietLevel();
    TestFloorAndBite();
    TestIwiBitmaps();
    TestBlendMapping();
    TestEffectConversion();
    TestRealZones();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::puts("test46_tests: all passed");
    return 0;
}
