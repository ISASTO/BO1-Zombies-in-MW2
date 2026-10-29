// Test45: soldier-sound classification, BO1 dismemberment rules, the HUD
// image decoder and pack, the ambience bed's smaller copy, surface removal
// and used-bone detection, and (with BO1_THEATER_DEC / BO1_COMMON_DEC set to
// the owner's inflated zones) the powerup and dismemberment carves.
#include "bo1_sounds.hpp"
#include "bo1_gib_rules.hpp"
#include "bo1_hud.hpp"
#include "bo1_sound_build.hpp"
#include "bo1_extra_models.hpp"
#include "bo1_model_synthetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {
int failures=0;
void Check(bool ok,const char* what) {if(!ok){std::printf("FAIL: %s\n",what);++failures;}}

void TestSoldierSounds() {
    using mod::bo1sounds::ClassifySoldierSound;using S=mod::bo1sounds::SoldierSound;
    Check(ClassifySoldierSound("step_run_dirt")==S::Step && ClassifySoldierSound("Step_Run_Concrete")==S::Step,"footsteps are redirected");
    Check(ClassifySoldierSound("gear_rattle_run")==S::Gear,"the gear rattle of every running step is dropped");
    Check(ClassifySoldierSound("step_scrape_dirt")==S::Scrape && ClassifySoldierSound("land_dirt")==S::Land
          && ClassifySoldierSound("Land_metal")==S::Land,"scrapes and landings are dropped");
    Check(ClassifySoldierSound("weap_reload_smg_clipout_npc")==S::Weapon && ClassifySoldierSound("ai_shotgun_pump")==S::Weapon,"reload foley is dropped");
    Check(ClassifySoldierSound("generic_pain_american_1")==S::Voice && ClassifySoldierSound("german_death")==S::Voice
          && ClassifySoldierSound("allied_death")==S::Voice,"soldier voices are dropped");
    Check(ClassifySoldierSound("bodyfall_dirt_large")==S::Keep,"a body falling keeps its thud");
    Check(ClassifySoldierSound("dive_wall")==S::Other && ClassifySoldierSound("melee_knife_hit")==S::Other,"everything else is dropped");
}

void TestGibRules() {
    using namespace mod::bo1gib;
    int next=0;std::vector<int> script;
    auto random=Random([&](int n){const int v=script.empty()?0:script[static_cast<std::size_t>(next++)%script.size()];return v%n;});
    Inputs in;in.byPlayer=true;in.maxHealth=150;
    // A pistol shot to the head that leaves 10 of 150 health: the head pops.
    in.mod=kModPistolBullet;in.hit=Head;in.damage=40;in.healthAfter=10;
    auto d=Decide(in,random);Check(d.head && d.limb==Ref::None,"low-health head shot pops the head");
    in.healthAfter=16;d=Decide(in,random);Check(!d.head && d.limb==Ref::None,"above 10% health the head stays (and a head hit never gibs a limb)");
    in.healthAfter=-20;in.headGibbed=true;d=Decide(in,random);Check(!d.head,"a head pops once");
    in.headGibbed=false;in.mod=kModMelee;d=Decide(in,random);Check(!d.head,"melee never gibs");
    in.mod=kModHeadShot;in.hit=Helmet;d=Decide(in,random);Check(d.head,"a helmet hit counts as the head");
    // Limbs: at least 10% of the health before the hit.
    in={};in.byPlayer=true;in.maxHealth=1000;in.mod=kModRifleBullet;in.hit=RightArmLower;in.damage=50;in.healthAfter=900;
    d=Decide(in,random);Check(d.limb==Ref::None,"a hit under 10% of the remaining health does not gib");
    in.healthAfter=400;d=Decide(in,random);Check(d.limb==Ref::RightArm,"right forearm -> right arm");
    in.hit=LeftHand;d=Decide(in,random);Check(d.limb==Ref::LeftArm,"left hand -> left arm");
    in.gibbed=true;d=Decide(in,random);Check(d.limb==Ref::None,"one limb per zombie");
    in.gibbed=false;in.hit=RightLegUpper;d=Decide(in,random);Check(d.limb==Ref::None,"a leg hit on a living zombie keeps the leg");
    in.healthAfter=-5;script={4};next=0;d=Decide(in,random);Check(d.limb==Ref::NoLegs,"a killing leg hit can take both legs");
    script={0};next=0;d=Decide(in,random);Check(d.limb==Ref::RightLeg,"... or one");
    in.hit=TorsoUpper;in.healthAfter=100;script={0};next=0;d=Decide(in,random);Check(d.limb==Ref::Guts,"torso -> guts or right arm");
    script={1};next=0;d=Decide(in,random);Check(d.limb==Ref::RightArm,"torso -> right arm");
    in.byPlayer=false;d=Decide(in,random);Check(d.limb==Ref::None,"only the player's damage gibs");
    // Explosive kills: half of the time, any of the six.
    in={};in.byPlayer=true;in.maxHealth=150;in.mod=kModGrenadeSplash;in.damage=300;in.healthAfter=-150;
    script={1};next=0;d=Decide(in,random);Check(d.limb==Ref::None,"explosive: no gib half of the time");
    script={0,5};next=0;d=Decide(in,random);Check(d.limb==Ref::NoLegs,"explosive: a random gib");
    in.healthAfter=50;script={0,1};next=0;d=Decide(in,random);Check(d.limb==Ref::None,"explosive: only on the kill");
    Check(LimbData(Ref::RightArm).torso==1 && LimbData(Ref::RightArm).legs==0 && LimbData(Ref::RightArm).pieces==1u,"right arm limb data");
    Check(LimbData(Ref::NoLegs).torso==0 && LimbData(Ref::NoLegs).legs==3 && LimbData(Ref::NoLegs).pieces==12u,"no legs limb data");
    Check(LimbData(Ref::Guts).torso==3 && !LimbData(Ref::Guts).pieces,"guts throw nothing");
}

std::vector<std::uint8_t> Iwi(std::uint8_t format,std::uint8_t flags,std::uint16_t w,std::uint16_t h,const std::vector<std::uint8_t>& top,std::size_t smaller) {
    std::vector<std::uint8_t> f(48,0);f[0]='I';f[1]='W';f[2]='i';f[3]=13;f[4]=format;f[5]=flags;
    std::memcpy(f.data()+6,&w,2);std::memcpy(f.data()+8,&h,2);const std::uint16_t d=1;std::memcpy(f.data()+10,&d,2);
    f.insert(f.end(),smaller,0xee);f.insert(f.end(),top.begin(),top.end());
    return f;
}
void TestHud() {
    using namespace mod::bo1hud;
    // Luminance-alpha 2x2, no mips (flag 2): the chalk marks' format.
    auto la=Iwi(3,2|0xc0,2,2,{200,255,10,0,128,64,0,255},0);
    Bgra img;std::string error;
    Check(DecodeIwi(la.data(),la.size(),img,&error) && img.width==2 && img.pixels.size()==16,"luminance-alpha decodes");
    Check(img.pixels[0]==200 && img.pixels[2]==200 && img.pixels[3]==255 && img.pixels[7]==0 && img.pixels[11]==64,"luminance-alpha texels");
    // DXT1 4x4 with mips: the largest mip follows the 1x1 and 2x2 ones (8 bytes each).
    std::vector<std::uint8_t> block={0x00,0xf8,0x1f,0x00,0x00,0x00,0x00,0x00};   // c0 red, c1 blue, all index 0
    auto dxt=Iwi(0xb,0,4,4,block,16);
    Check(DecodeIwi(dxt.data(),dxt.size(),img,&error) && img.pixels[2]==255 && img.pixels[0]==0 && img.pixels[3]==255,"DXT1 largest mip");
    // DXT5 alpha: a0=255 a1=0, index 1 everywhere -> alpha 0.
    std::vector<std::uint8_t> b5={255,0,0x49,0x92,0x24,0x49,0x92,0x24, 0xff,0xff,0xff,0xff,0,0,0,0};
    auto dxt5=Iwi(0xd,2,4,4,b5,0);
    Check(DecodeIwi(dxt5.data(),dxt5.size(),img,&error) && img.pixels[3]==0 && img.pixels[0]==255,"DXT5 alpha and colour");
    auto bad=la;bad.resize(50);Check(!DecodeIwi(bad.data(),bad.size(),img,&error),"short image refused");
    auto cube=la;cube[5]|=4;Check(!DecodeIwi(cube.data(),cube.size(),img,&error),"cube image refused");
    std::map<std::string,std::vector<std::uint8_t>> files{{"chalkmarks_1",la},{"specialty_2x_zombies",dxt}},back;
    const auto pack=WritePack(files);
    Check(ReadPack(pack.data(),pack.size(),back) && back==files,"HUD pack round trip");
    auto broken=pack;broken.pop_back();Check(!ReadPack(broken.data(),broken.size(),back),"truncated pack refused");
}

void TestShrink() {
    using namespace mod::bo1sound;
    Pcm p;p.channels=2;p.rate=48000;p.samples.assign(48000*2*4,1000);   // 4 s of stereo
    auto s=detail::Smaller(p,detail::Shrink{true,24000,3});
    Check(s.channels==1 && s.rate==24000 && s.frames()==72000,"bed: mono, 24 kHz, at most 3 s");
    Check(s.samples.front()==0 && s.samples[36000]==1000 && std::abs(s.samples.back())<20,"bed: faded at both ends when cut");
    Pcm q;q.channels=1;q.rate=44100;q.samples.assign(44100,500);
    auto t=detail::Smaller(q,detail::Shrink{true,24000,150});
    Check(t.rate==22050 && t.frames()==22050 && t.samples[100]==500,"short bed: rate halved, nothing cut");
}

void TestSurfaces() {
    using namespace mod::bo1model;
    auto m=synthetic::Character({"tag_origin","j_mainroot","j_knee_le","j_ankle_le"},3);
    m.materials[0].kind=MaterialRef::Kind::Inline;m.materials[0].name="a";
    m.materials[1].kind=MaterialRef::Kind::Inline;m.materials[1].name="b";
    Model out;std::string error;
    Check(WithoutSurfaces(m,{false,true,false},out,&error) && out.numsurfs==2 && out.lods[0].numsurfs==1 && out.lods[1].surfIndex==1
          && out.materials[0].name=="a","a dropped surface leaves its LOD");
    Check(!WithoutSurfaces(m,{false,false,true},out,&error),"a LOD may not lose every surface");
    const auto used=UsedBones(m);
    // LOD0: the skinned surface binds bones 1, 2, 3 (offsets 64..192), the rigid list bone 2.
    Check(used.count("j_mainroot") && used.count("j_knee_le") && used.count("j_ankle_le") && !used.count("tag_origin"),"bones the vertices use");
    Check(ExtraImageName("~x2_icon_s-r&$white-r&$black-~ecfa3406")=="bo1x__x2_icon_s_r__white_r__black__ecfa3406","image names are plain");
}

std::vector<std::uint8_t> ReadAll(const char* path) {
    std::ifstream f(path,std::ios::binary);
    return f?std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),{}):std::vector<std::uint8_t>{};
}
void TestRealCarves() {
    using namespace mod::bo1model;
    const char* theaterPath=std::getenv("BO1_THEATER_DEC");
    if(!theaterPath) {std::puts("SKIP real dismemberment carve (set BO1_THEATER_DEC)");}
    else {
        const auto raw=ReadAll(theaterPath);
        const auto strings=mod::bo1xanim::ParseZoneStrings(raw.data(),raw.size());
        std::string error;
        const auto c=CarveCharacter(raw.data(),raw.size(),strings->strings,strings->assetsOffset,&error);
        Check(!!c,"character");
        const auto g=c?CarveGibs(raw.data(),raw.size(),strings->strings,*c,&error):std::nullopt;
        Check(!!g && g->piecesFound,"dismemberment models and pieces");
        if(g) {
            auto used=[](const GibModel& m,const char* bone){return UsedBones(m.model).count(bone)!=0;};
            Check(!used(g->rightArmOff,"j_elbow_ri") && used(g->rightArmOff,"j_wrist_le"),"right arm off at the elbow");
            Check(!used(g->leftArmOff,"j_wrist_le") && used(g->leftArmOff,"j_wrist_ri"),"left arm off");
            Check(used(g->upper,"j_wrist_le") && used(g->upper,"j_wrist_ri") && !used(g->upper,"j_knee_le"),"upper body without legs");
            Check(used(g->legs,"j_ankle_le") && used(g->legs,"j_ankle_ri") && !g->legs.model.hasBone("j_spine4"),"legs without upper body");
            Check(!used(g->rightLegOff,"j_ankle_ri") && used(g->rightLegOff,"j_ankle_le"),"right leg off");
            Check(!used(g->leftLegOff,"j_ankle_le") && used(g->leftLegOff,"j_ankle_ri"),"left leg off");
            Check(!used(g->noLegs,"j_knee_le") && !used(g->noLegs,"j_knee_ri"),"no legs");
            Check(g->neck.model.bones.back()=="j_neck" && !g->neck.model.hasBone("j_head"),"neck stump");
            Check(g->gore.name.find("gore_limbs")!=std::string::npos,"gore material");
            int gore=0;for(const auto s:g->rightArmOff.skins) if(s==GibSkin::Gore) ++gore;
            Check(gore>=1,"the torn arm has a gore cap");
            for(const auto& p:g->pieces) Check(p.model.numBones==2 && p.model.physics && p.model.bones[1]=="tag_fx","thrown piece");
            for(const auto* m:{&g->upper,&g->rightArmOff,&g->leftArmOff,&g->guts,&g->neck,&g->legs,&g->rightLegOff,&g->leftLegOff,&g->noLegs})
                Check(m->skins.size()==m->model.numsurfs && SkeletonError(m->model)<0.01f,"dismemberment model skeleton and skins");
        }
    }
    const char* commonPath=std::getenv("BO1_COMMON_DEC");
    if(!commonPath) {std::puts("SKIP real powerup carve (set BO1_COMMON_DEC)");return;}
    const auto common=ReadAll(commonPath);
    const auto cs=mod::bo1xanim::ParseZoneStrings(common.data(),common.size());
    std::string error;
    const auto p=CarvePowerups(common.data(),common.size(),cs->strings,&error);
    Check(!!p,error.c_str());
    if(p) {
        Check(p->models[3].bones.size()==1 && p->models[3].bones[0]=="tag_origin","the skull's unnamed bone");
        Check(p->surfaceMaterial[0].size()==2 && p->surfaceMaterial[0][0]==1 && p->surfaceMaterial[0][1]==0,"ammo can: ammo box and gold");
        Check(ImageByHash(p->gold,kHashColorMap)=="~-gx2_icon_c" && ImageByHash(p->ammo,kHashNormalMap)=="zombie_ammo_box_n","powerup textures");
    }
}
void TestRealAnnouncer() {
    using namespace mod::bo1sound;
    const char* path=std::getenv("BO1_LOCALIZED_DEC");
    if(!path) {std::puts("SKIP real announcer bank (set BO1_LOCALIZED_DEC to the inflated en_zombie_theater zone)");return;}
    const auto raw=ReadAll(path);
    const auto at=FindBank(raw.data(),raw.size(),"zmb_theater.english");
    Bank bank;std::string error;
    Check(at && ParseBank(raw.data(),raw.size(),*at,bank,&error),"zmb_theater.english parses");
    std::set<std::string> files;
    detail::StreamedFiles(bank,kAnnouncerLists,files);
    Check(files.size()==6 && files.count("english/sound/voiceovers/zmb/ann/ann_maxammo.wav") && files.count("english/sound/voiceovers/zmb/ann/ann_instakill.wav"),
          "announcer files are the localized archives' paths");
    // Without the archives the lists are missing, never a failure.
    SoundZoneSpec spec;SoundBuildReport report;
    const char* commonPath=std::getenv("BO1_COMMON_DEC");
    if(commonPath) {
        const auto common=ReadAll(commonPath);
        LocalizedZone localized{raw.data(),raw.size(),"zmb_theater.english"};
        Check(BuildSoundSpec(common.data(),common.size(),nullptr,0,{},spec,report,&error,localized) && report.localizedBank=="zmb_theater.english"
              && report.announcer==0 && report.streamedMissing==6,"announcer read, files missing offline");
        Check(report.listNames.count("zmb_zombie_head_gib") && report.listNames.count("zmb_death_gibs"),"gib sounds");
    }
}
} // namespace

int main() {
    TestSoldierSounds();TestGibRules();TestHud();TestShrink();TestSurfaces();TestRealCarves();TestRealAnnouncer();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::puts("test45_tests passed");
    return 0;
}
