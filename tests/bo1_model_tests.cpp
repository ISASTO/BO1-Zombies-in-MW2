// Test40: BO1 model conversion, IWI v13 textures, the MW2 zone writer, the
// zlib store and the .iwd (zip) reader. BO1_THEATER_DEC may name a local
// decompressed zombie_theater zone for a full carve of the Kino character.
#include "bo1_model_build.hpp"
#include "bo1_model_synthetic.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace mod::bo1model;
static int failures=0;
static void Check(bool ok,const char* what) {if(!ok){++failures;std::printf("FAIL %s\n",what);}}

static std::vector<std::uint8_t> Iwi13(std::uint8_t format,std::uint8_t flags,std::uint16_t w,std::uint16_t h,std::uint32_t* sizeOut=nullptr) {
    std::vector<std::uint8_t> f={'I','W','i',13,format,flags};
    auto put16=[&](std::uint16_t v){f.push_back(static_cast<std::uint8_t>(v));f.push_back(static_cast<std::uint8_t>(v>>8));};
    put16(w);put16(h);put16(1);
    f.resize(48,0);Put<float>(f.data()+12,2.2f);
    const std::uint32_t d3d=format==0xb?kD3dDxt1:kD3dDxt5;
    const unsigned levels=(flags&2)?1:MipCount(w,h);
    std::uint32_t total=48;
    for(int level=static_cast<int>(levels)-1;level>=0;--level) {
        const auto n=MipBytes(d3d,w>>level,h>>level);
        for(std::size_t k=0;k<n;++k) f.push_back(static_cast<std::uint8_t>(level*16+k%16));
        total+=static_cast<std::uint32_t>(n);
        if(level<8) Put<std::uint32_t>(f.data()+16+4*level,total);
    }
    if(sizeOut)*sizeOut=total;
    return f;
}

static void TestIwi() {
    std::uint32_t size=0;
    const auto file=Iwi13(0xb,0,16,8,&size);
    Texture t;std::string error;
    Check(ParseIwi13(file.data(),file.size(),t,&error) && t.format==kD3dDxt1 && t.width==16 && t.height==8 && t.levels==5,"IWI DXT1 parses");
    // Largest mip first: level 0 (16x8 = 4x2 blocks = 64 bytes) carries the level-0 pattern.
    Check(t.data.size()==64+16+8+8+8 && t.data[0]==0 && t.data[64]==16 && t.data[80]==32 && t.data.back()==4*16+7,"IWI mips reordered largest first");
    auto flat=Iwi13(0xd,2,8,8);
    Check(ParseIwi13(flat.data(),flat.size(),t) && t.levels==1 && t.format==kD3dDxt5 && t.data.size()==64,"IWI without mips");
    auto broken=file;broken.pop_back();
    Check(!ParseIwi13(broken.data(),broken.size(),t),"IWI shorter than its mips refused");
    auto padded=file;padded.push_back(0);
    Check(ParseIwi13(padded.data(),padded.size(),t) && t.data.size()==64+16+8+8+8,"IWI trailing bytes ignored");
    auto cube=Iwi13(0xb,4,8,8);
    Check(!ParseIwi13(cube.data(),cube.size(),t),"IWI cube refused");
    auto rgba=Iwi13(0xb,0,8,8);rgba[4]=1;
    Check(!ParseIwi13(rgba.data(),rgba.size(),t),"IWI bitmap format refused");
    auto v8=file;v8[3]=8;
    Check(!ParseIwi13(v8.data(),v8.size(),t),"IWI v8 refused");
}

static void TestSpecGloss() {
    // One 4x4 block each: spec colour, gloss with green 0 (index 1) / 255 (index 0).
    Texture spec;spec.width=4;spec.height=4;spec.levels=1;spec.format=kD3dDxt1;
    spec.data={0x00,0xf8,0x1f,0x00,0x55,0x55,0x55,0x55};
    Texture gloss=spec;
    gloss.data={0xe0,0x07,0x00,0x00,0x00,0x00,0xff,0xff};   // c0 green 255, c1 0; texels 0-7 index 0, 8-15 index 3 (1/3 of 255 toward c1)
    Texture out;
    Check(CombineSpecGloss(spec,gloss,out) && out.format==kD3dDxt5 && out.data.size()==16,"spec+gloss combine");
    Check(out.data[0]==255 && out.data[1]==85 && !std::memcmp(out.data.data()+8,spec.data.data(),8),"alpha endpoints and colour block");
    // Decode the alpha block: texels 0-7 must be 255, 8-15 85.
    std::uint64_t bits=0;for(int n=0;n<6;++n)bits|=static_cast<std::uint64_t>(out.data[2+n])<<(8*n);
    bool ok=true;
    for(unsigned n=0;n<16;++n){const auto idx=(bits>>(3*n))&7;const int v=idx==0?255:idx==1?85:0;if(v!=(n<8?255:85))ok=false;}
    Check(ok,"alpha indices");
    Texture mismatch=gloss;mismatch.width=8;
    Check(!CombineSpecGloss(spec,mismatch,out),"spec/gloss size mismatch refused");
    const auto solid=SolidDxt5(8,0x4208,0x40);
    Check(solid.levels==4 && solid.data.size()==(4+1+1+1)*16 && solid.data[0]==0x40 && solid.data[8]==0x08,"solid DXT5");
}

static void TestZlib() {
    std::vector<std::uint8_t> data(200000);for(std::size_t n=0;n<data.size();++n)data[n]=static_cast<std::uint8_t>(n*31+n/7);
    const auto z=ZlibStore(data);
    std::size_t length=0;
    void* back=tinfl_decompress_mem_to_heap(z.data(),z.size(),&length,TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_COMPUTE_ADLER32);
    Check(back && length==data.size() && !std::memcmp(back,data.data(),length),"zlib stored round trip (adler checked)");
    std::free(back);
    auto bad=z;bad[bad.size()-1]^=1;
    back=tinfl_decompress_mem_to_heap(bad.data(),bad.size(),&length,TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_COMPUTE_ADLER32);
    Check(!back,"zlib adler mismatch detected");
    std::free(back);
}

// A zip with one stored and one deflated (stored-block deflate) entry.
struct ZipFile {std::string name;std::vector<std::uint8_t> data;std::uint16_t method;};
static std::vector<std::uint8_t> MakeZip(std::vector<ZipFile> files) {
    std::vector<std::uint8_t> z,dir;
    auto put16=[](std::vector<std::uint8_t>& v,std::uint16_t x){v.push_back(static_cast<std::uint8_t>(x));v.push_back(static_cast<std::uint8_t>(x>>8));};
    auto put32=[&](std::vector<std::uint8_t>& v,std::uint32_t x){put16(v,static_cast<std::uint16_t>(x));put16(v,static_cast<std::uint16_t>(x>>16));};
    auto crc32=[](const std::vector<std::uint8_t>& d){std::uint32_t c=0xffffffffu;for(auto b:d){c^=b;for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return c^0xffffffffu;};
    for(auto& f:files) {
        std::vector<std::uint8_t> packed=f.data;
        if(f.method==8) {   // stored deflate blocks (<= 65535 bytes each)
            packed.clear();std::size_t at=0;
            do {
                const std::size_t n=std::min<std::size_t>(65535,f.data.size()-at);
                packed.push_back(at+n==f.data.size()?1:0);
                put16(packed,static_cast<std::uint16_t>(n));put16(packed,static_cast<std::uint16_t>(~n));
                packed.insert(packed.end(),f.data.begin()+static_cast<std::ptrdiff_t>(at),f.data.begin()+static_cast<std::ptrdiff_t>(at+n));at+=n;
            } while(at<f.data.size());
        }
        const auto offset=static_cast<std::uint32_t>(z.size());
        put32(z,0x04034b50u);put16(z,20);put16(z,0);put16(z,f.method);put32(z,0);put32(z,crc32(f.data));
        put32(z,static_cast<std::uint32_t>(packed.size()));put32(z,static_cast<std::uint32_t>(f.data.size()));
        put16(z,static_cast<std::uint16_t>(f.name.size()));put16(z,3);
        z.insert(z.end(),f.name.begin(),f.name.end());z.insert(z.end(),{9,9,9});z.insert(z.end(),packed.begin(),packed.end());
        put32(dir,0x02014b50u);put16(dir,20);put16(dir,20);put16(dir,0);put16(dir,f.method);put32(dir,0);put32(dir,crc32(f.data));
        put32(dir,static_cast<std::uint32_t>(packed.size()));put32(dir,static_cast<std::uint32_t>(f.data.size()));
        put16(dir,static_cast<std::uint16_t>(f.name.size()));put16(dir,0);put16(dir,0);put16(dir,0);put16(dir,0);put32(dir,0);put32(dir,offset);
        dir.insert(dir.end(),f.name.begin(),f.name.end());
    }
    const auto dirOffset=static_cast<std::uint32_t>(z.size());
    z.insert(z.end(),dir.begin(),dir.end());
    put32(z,0x06054b50u);put16(z,0);put16(z,0);put16(z,static_cast<std::uint16_t>(files.size()));put16(z,static_cast<std::uint16_t>(files.size()));put32(z,static_cast<std::uint32_t>(dir.size()));put32(z,dirOffset);put16(z,4);
    z.insert(z.end(),{'z','i','p','!'});
    return z;
}
static std::vector<std::uint8_t> Zip() {
    return MakeZip({{"images/stored.iwi",{1,2,3,4,5},0},{"images/Deflated.IWI",std::vector<std::uint8_t>(3000,7),8}});
}
static void TestZip() {
    const auto z=Zip();
    mod::zip::ReadAt read=[&](std::uint64_t at,void* out,std::size_t n){if(at+n>z.size())return false;std::memcpy(out,z.data()+at,n);return true;};
    std::vector<mod::zip::Entry> entries;std::string error;
    Check(mod::zip::ReadDirectory(read,z.size(),entries,&error) && entries.size()==2,"zip directory");
    std::vector<std::uint8_t> out;
    Check(mod::zip::ReadEntry(read,z.size(),entries[0],out,&error) && out==std::vector<std::uint8_t>({1,2,3,4,5}),"zip stored entry");
    Check(mod::zip::SameName(entries[1].name,"images\\deflated.iwi"),"zip names compare case-insensitively");
    Check(mod::zip::ReadEntry(read,z.size(),entries[1],out,&error) && out.size()==3000 && out[2999]==7,"zip deflated entry");
    auto corrupt=entries[1];corrupt.crc^=1;
    Check(!mod::zip::ReadEntry(read,z.size(),corrupt,out),"zip CRC mismatch refused");
    Check(!mod::zip::ReadDirectory(read,10,entries),"tiny zip refused");
}

static void TestZone() {
    synthetic::Scene scene;synthetic::Build(scene);
    ZoneResult z;std::string error;
    Check(BuildZone(scene.spec,z,&error),"synthetic zone builds");
    Check(z.assets==2 && z.scriptStrings==6,"asset and script string counts");
    Check(Get<std::uint32_t>(z.stream.data())==z.stream.size()-mw2::kXFileBytes,"XFile size");
    // Vertex block: each surface's verts start 16-aligned.
    std::size_t vertex=0,index=0;
    for(const auto* m:{&scene.a,&scene.b}) for(const auto& s:m->surfs){vertex=((vertex+15)&~std::size_t(15))+s.verts.size();index=((index+15)&~std::size_t(15))+s.tris.size();}
    Check(z.blockSize[mw2::Vertex]==((vertex+15)&~std::size_t(15))+4096 && z.blockSize[mw2::Index]==((index+15)&~std::size_t(15))+4096,"vertex/index block sizes");
    Check(!z.blockSize[mw2::Physical] && !z.blockSize[mw2::Runtime] && !z.blockSize[mw2::Large] && !z.blockSize[mw2::Callback],"unused blocks stay empty");
    const auto ff=FastFile(z);
    Check(!std::memcmp(ff.data(),"IWffu100",8) && Get<std::uint32_t>(ff.data()+8)==0x114 && ff[21]==0x78,"fastfile header");
    // Refusals.
    auto spec=scene.spec;spec.models[0].surfaceMaterial.pop_back();
    Check(!BuildZone(spec,z),"missing surface material refused");
    spec=scene.spec;spec.materials[0].color="no_such_image";
    Check(!BuildZone(spec,z),"missing image refused");
    spec=scene.spec;auto bad=scene.a;bad.surfs[1].rigid[0].boneOffset=65;spec.models[0].source=&bad;
    Check(!BuildZone(spec,z),"misaligned rigid bone refused");
}

// A three-bone chain: root at (0,0,10), a child 5 units along x rotated 90
// degrees about z, and a grandchild 2 units along its parent's x (world +y).
static void TestSkeleton() {
    Model m;m.numBones=3;m.numRootBones=1;m.bones={"tag_origin","j_a","j_b"};
    m.parentList={1,1};
    auto quat=[&](std::int16_t x,std::int16_t y,std::int16_t z,std::int16_t w){for(auto v:{x,y,z,w}){m.quats.push_back(static_cast<std::uint8_t>(v));m.quats.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(v)>>8));}};
    auto vec=[](std::vector<std::uint8_t>& out,std::initializer_list<float> v){for(float f:v){std::uint8_t b[4];std::memcpy(b,&f,4);out.insert(out.end(),b,b+4);}};
    const std::int16_t h=23170;   // 32767*sqrt(1/2)
    quat(0,0,h,h);quat(0,0,0,32767);
    vec(m.trans,{5.f,0.f,0.f});vec(m.trans,{2.f,0.f,0.f});
    const float s=0.70710677f;
    vec(m.baseMat,{0,0,0,1, 0,0,10, 2});vec(m.baseMat,{0,0,s,s, 5,0,10, 2});vec(m.baseMat,{0,0,s,s, 5,2,10, 2});
    Check(SkeletonError(m)<0.01f,"consistent chain reproduces its base pose");
    auto broken=m;broken.trans[3]^=0x01;   // 5.0f -> 20.0f
    Check(SkeletonError(broken)>0.5f,"a moved bone is caught");
    auto bad=m;bad.parentList[1]=3;
    Check(SkeletonError(bad)>1e8f,"a parent past the root is refused");
}

static void TestCharacter() {
    const char* path=std::getenv("BO1_THEATER_DEC");
    if(!path) {std::puts("SKIP real zombie_theater carve (set BO1_THEATER_DEC)");return;}
    std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
    const auto strings=mod::bo1xanim::ParseZoneStrings(raw.data(),raw.size());
    Check(!!strings,"real zone strings");
    if(!strings) return;
    std::string error;
    const auto c=CarveCharacter(raw.data(),raw.size(),strings->strings,strings->assetsOffset,&error);
    Check(!!c,error.c_str());
    if(!c) return;
    Check(c->body1.numBones==73 && c->body2.numBones==73 && c->body1.bones==c->body2.bones && c->heads[0].numBones==28,"bodies and heads");
    // Test43: packed vec3 bone translations; every skeleton lands on its base pose.
    for(const auto* m:{&c->body1,&c->body2,&c->heads[0],&c->heads[1],&c->heads[2],&c->heads[3]})
        Check(SkeletonError(*m)<0.01f,"real skeleton reproduces its base pose");
    {
        const auto knee=static_cast<std::size_t>(std::find(c->body1.bones.begin(),c->body1.bones.end(),"j_knee_le")-c->body1.bones.begin());
        const auto hip=static_cast<std::size_t>(std::find(c->body1.bones.begin(),c->body1.bones.end(),"j_hip_le")-c->body1.bones.begin());
        const float kneeX=Get<float>(c->body1.trans.data()+12*(knee-1)),hipZ=Get<float>(c->body1.trans.data()+12*(hip-1)+8);
        Check(std::fabs(kneeX-17.51f)<0.01f && std::fabs(hipZ-4.87f)<0.01f,"thigh 17.51 and hip 4.87 units, as MW2's defaultactor");
    }
    Check(c->headSpec=="c_ger_zombie_head1_s" && c->bodyNormal=="char_ger_honorguard_body1_n","texture names");
    int skin=0;for(std::size_t n=0;n<c->body1.numsurfs;++n)skin+=c->skinOf(c->body1,n)==Skin::HeadSkin;
    Check(skin==4,"body hands/neck use the head skin");
    std::map<std::string,Texture> tex;
    for(const auto* n:{&c->headColor,&c->headNormal,&c->headSpec,&c->eyeColor,&c->bodyColor,&c->bodyNormal,&c->bodySpec}) tex[*n]=synthetic::Dxt1(16,16,5);
    ZoneSpec spec;ZoneResult z;
    Check(CharacterSpec(*c,tex,spec,&error) && BuildZone(spec,z,&error),"Kino zone builds");
    std::printf("kino zone: %zu bytes, %zu strings\n",z.stream.size(),z.scriptStrings);
}

static void TestPipeline() {
    const char* path=std::getenv("BO1_THEATER_DEC");
    if(!path) {std::puts("SKIP real zombie_theater pipeline (set BO1_THEATER_DEC)");return;}
    std::ifstream in(path,std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
    // An older archive with a broken head colour map, a newer one with the
    // good textures; the body specular map is missing (MW2 fallback).
    auto dxt1=[](std::uint16_t w){return Iwi13(0xb,0,w,w);};
    auto dxt5=[](std::uint16_t w){return Iwi13(0xd,0,w,w);};
    auto broken=dxt1(64);broken.resize(100);
    const auto older=MakeZip({{"images/c_ger_zombie_head1_c.iwi",broken,8},{"images/zombie_unrelated.iwi",dxt1(8),0}});
    const auto newer=MakeZip({{"images/c_ger_zombie_head1_c.iwi",dxt1(64),8},{"IMAGES/C_GER_ZOMBIE_HEAD1_N.IWI",dxt5(64),8},
        {"images/c_ger_zombie_head1_s.iwi",dxt1(64),8},{"images/c_ger_zombie_head1_g.iwi",dxt1(64),8},{"images/~-gzombie_eye.iwi",dxt1(32),0},
        {"images/~-gchar_ger_honorguard_body1_c.iwi",dxt1(128),8},{"images/char_ger_honorguard_body1_n.iwi",dxt5(128),8}});
    auto archive=[](const std::string& name,const std::vector<std::uint8_t>& bytes) {
        Archive a;a.name=name;a.size=bytes.size();
        const auto* data=&bytes;
        a.read=[data](std::uint64_t at,void* out,std::size_t n){if(at+n>data->size())return false;std::memcpy(out,data->data()+at,n);return true;};
        return a;
    };
    std::vector<Archive> archives={archive("iw_00.iwd",older),archive("iw_01.iwd",newer)};
    std::vector<std::uint8_t> ff;BuildReport report;std::string error;
    const bool built=BuildCharacterFastFile(raw.data(),raw.size(),archives,ff,report,&error);
    if(!built) std::printf("pipeline error: %s\n",error.c_str());
    for(const auto& n:report.notes) std::printf("  note: %s\n",n.c_str());
    Check(built,"pipeline builds");
    Check(report.textures==7 && report.fallbacks.size()==1 && report.fallbacks[0].find("body specular")==0,"pipeline textures and fallback");
    Check(report.models==6 && report.images==8 && !std::memcmp(ff.data(),"IWffu100",8),"pipeline fastfile");
    std::printf("pipeline: %zu byte fastfile, %zu images (%zu bytes), notes=%zu\n",ff.size(),report.images,report.imageBytes,report.notes.size());
    // Every texture missing except colour maps -> still builds; no colour -> refused.
    const auto colours=MakeZip({{"images/c_ger_zombie_head1_c.iwi",dxt1(16),0},{"images/~-gzombie_eye.iwi",dxt1(16),0},
        {"images/~-gchar_ger_honorguard_body1_c.iwi",dxt1(16),0}});
    archives={archive("iw_00.iwd",colours)};
    Check(BuildCharacterFastFile(raw.data(),raw.size(),archives,ff,report,&error) && report.fallbacks.size()==4,"colour-only build uses fallbacks");
    const auto none=MakeZip({{"images/other.iwi",dxt1(16),0}});
    archives={archive("iw_00.iwd",none)};
    Check(BuildCharacterFastFile(raw.data(),raw.size(),archives,ff,report,&error) && report.fallbacks.size()==7 && !report.textures,"untextured build uses grey");
}

int main() {
    TestIwi();TestSpecGloss();TestZlib();TestZip();TestZone();TestSkeleton();TestCharacter();TestPipeline();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::puts("bo1_model_tests: all passed");
    return 0;
}
