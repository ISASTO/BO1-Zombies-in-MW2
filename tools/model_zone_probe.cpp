// Load a Test40-generated MW2 fastfile with the exact retail x64 loader.
//
// The private mapped image runs DB_LoadXFile (0xdb7f0) unmodified: header
// and version checks, DB_InitStreams, Load_ScriptStringList, Load_XAsset and
// every XModel / XModelSurfs / XSurface / Material / technique set / GfxImage
// loader under it, with the real stream allocator. Only the edges are
// replaced: file reads come from memory, zone memory comes from guarded
// buffers, DB_AddXAsset copies headers into a registry (resolving ','
// references like the engine), SL_GetString interns into a table, and the
// texture upload records the loadDef it was given.
//
// Afterwards every loaded field is compared with the source model, block
// guards are checked, and the stream must be consumed exactly.
#include "script_call.hpp"
#include "bo1_model.hpp"
#include "../tests/bo1_model_synthetic.hpp"
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

using namespace mod::bo1model;
namespace {
constexpr std::uintptr_t kBase=0x140000000;
constexpr std::size_t kSize=0x5000000;
constexpr std::uintptr_t kLoadXFile=0xdb7f0;
constexpr std::uintptr_t kReaderInit=0xdbb40,kRawRead=0xdbe50,kHeaderError=0xdba70,kInflateInit=0xdbae0,kRead=0xdbbc0;
constexpr std::uintptr_t kVersionError=0x1fb5f0,kComError=0x2906b0,kAllocZone=0xee1b0,kFinishZone=0xee450,kInflateEnd=0xdbb30;
constexpr std::uintptr_t kAfterLoad1=0xdbaa0,kAfterLoad2=0xf0920;
constexpr std::uintptr_t kAddXModel=0xf3810,kAddXModelSurfs=0xf38e0,kAddMaterial=0xf2af0,kAddTechniqueSet=0xf2c70,kAddImage=0xf25b0;
constexpr std::uintptr_t kLoadTexture=0x23db0,kSlGetString=0x21a670;
constexpr std::uintptr_t kProgressFlag=0x600e04,kStreamPositions=0xb697d0,kZoneHandle=0xb69434;

void Check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
std::uint8_t* image=nullptr;
void Jump(std::uintptr_t rva,const void* target) {
    auto* p=image+rva;p[0]=0x48;p[1]=0xb8;std::uint64_t t=reinterpret_cast<std::uint64_t>(target);std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;
}

std::vector<std::uint8_t> rawFile;std::size_t rawPos=0;          // the fastfile bytes before the zlib stream
std::vector<std::uint8_t> stream;std::size_t streamPos=0;        // the inflated stream
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

// Zone memory: page-aligned blocks with a guard page of 0xcd after each.
constexpr std::size_t kPage=4096;
struct Block {std::uint8_t* data=nullptr;std::size_t size=0,mapped=0;};
std::array<Block,8> blocks;
std::vector<std::uint8_t> lockedVertex,lockedIndex;
void MOD_NATIVE_ABI AllocZone(const std::uint32_t* sizes,const char*,std::uint8_t* zoneMem) {
    for(std::size_t n=0;n<8;++n) {
        if(!sizes[n]) continue;
        auto& b=blocks[n];b.size=sizes[n];b.mapped=((b.size+kPage-1)&~(kPage-1))+kPage;
        b.data=static_cast<std::uint8_t*>(mmap(nullptr,b.mapped,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        std::memset(b.data,0xcd,b.mapped);
        *reinterpret_cast<std::uint8_t**>(zoneMem+0x10*n)=b.data;*reinterpret_cast<std::uint32_t*>(zoneMem+0x10*n+8)=sizes[n];
    }
    if(sizes[6]){lockedVertex.assign(sizes[6],0);*reinterpret_cast<std::uint8_t**>(zoneMem+0x80)=lockedVertex.data();}
    if(sizes[7]){lockedIndex.assign(sizes[7],0);*reinterpret_cast<std::uint8_t**>(zoneMem+0x88)=lockedIndex.data();}
}

// Script strings.
std::vector<std::string> slStrings{""};
unsigned MOD_NATIVE_ABI SlGetString(const char* s,int) {
    for(std::size_t n=1;n<slStrings.size();++n) if(slStrings[n]==s) return static_cast<unsigned>(n);
    slStrings.push_back(s);return static_cast<unsigned>(slStrings.size()-1);
}

// Asset registry: DB_AddXAsset copies the header out of the temp block.
struct Asset {int type;std::string name;std::vector<std::uint8_t> header;bool placeholder=false;};
std::vector<std::unique_ptr<Asset>> assets;
std::map<std::pair<int,std::string>,Asset*> byName;
std::size_t references=0;
const std::map<int,std::size_t> kHeaderBytes={{4,mw2::kModelBytes},{3,mw2::kSurfsBytes},{5,mw2::kMaterialBytes},{9,mw2::kTechniqueSetBytes},{10,mw2::kImageBytes}};
const std::map<int,std::size_t> kNameOffset={{4,0},{3,0},{5,0},{9,0},{10,0x20}};
template<int Type> void MOD_NATIVE_ABI Add(std::uint8_t** var) {
    const auto* header=*var;
    const char* name=*reinterpret_cast<const char* const*>(header+kNameOffset.at(Type));
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

// Texture upload: record what the engine would create.
struct Upload {std::uint8_t levels;std::int32_t flags;std::uint32_t format,size;std::vector<std::uint8_t> data;std::uint16_t w,h,d;std::uint8_t mapType,semantic;};
std::map<std::string,Upload> uploads;
void MOD_NATIVE_ABI LoadTexture(std::uint8_t** texture,std::uint8_t* gfxImage) {
    const auto* l=*texture;
    const char* name=*reinterpret_cast<const char* const*>(gfxImage+0x20);
    Upload u{l[0],Get<std::int32_t>(l+4),Get<std::uint32_t>(l+8),Get<std::uint32_t>(l+12),{},Get<std::uint16_t>(gfxImage+0x18),
             Get<std::uint16_t>(gfxImage+0x1a),Get<std::uint16_t>(gfxImage+0x1c),gfxImage[8],gfxImage[9]};
    u.data.assign(l+16,l+16+u.size);
    uploads[name]=std::move(u);
    *reinterpret_cast<std::uint64_t*>(texture)=0x7e7e7e7e7e7e7e7eull;
}

template<class T> T At(const std::uint8_t* p,std::size_t o) {return Get<T>(p+o);}
const std::uint8_t* Ptr(const std::uint8_t* p,std::size_t o) {return reinterpret_cast<const std::uint8_t*>(Get<std::uint64_t>(p+o));}
bool Within(const void* p,std::size_t n,int block) {
    const auto& b=blocks[static_cast<std::size_t>(block)];const auto* q=static_cast<const std::uint8_t*>(p);
    return b.data && q>=b.data && q+n<=b.data+b.size;
}
bool Same(const std::uint8_t* p,const std::vector<std::uint8_t>& v) {return v.empty() || (p && !std::memcmp(p,v.data(),v.size()));}

struct Report {std::size_t models=0,surfaces=0,materials=0,images=0,vertices=0,triangles=0,rigid=0,collisionTrees=0;};

void VerifyModel(const ModelSpec& spec,const ZoneSpec& zone,Report& report) {
    const auto found=byName.find({mw2::kAssetXModel,spec.name});
    Check(found!=byName.end() && !found->second->placeholder,"model "+spec.name+" not registered");
    const auto* h=found->second->header.data();const auto& m=*spec.source;
    const std::string where="model "+spec.name+": ";
    Check(h[8]==m.numBones && h[9]==m.numRootBones && h[10]==m.numsurfs && h[11]==m.lodRampType && At<float>(h,0xc)==1.f,where+"counts");
    const auto* bones=Ptr(h,0x28);
    for(std::size_t n=0;n<m.numBones;++n) {
        const auto id=At<std::uint16_t>(bones,2*n);
        Check(id<slStrings.size() && slStrings[id]==m.bones[n],where+"bone name "+std::to_string(n));
    }
    Check(Same(Ptr(h,0x30),m.parentList) && Same(Ptr(h,0x38),m.quats) && Same(Ptr(h,0x40),m.trans),where+"skeleton");
    Check(Same(Ptr(h,0x48),m.partClassification) && Same(Ptr(h,0x50),m.baseMat),where+"base pose");
    const auto* handles=Ptr(h,0x58);
    for(std::size_t n=0;n<m.numsurfs;++n) {
        const auto* material=Ptr(handles,8*n);
        const auto& want=zone.materials[spec.surfaceMaterial[n]];
        const auto mat=byName.find({5,want.name});
        Check(mat!=byName.end() && material==mat->second->header.data(),where+"material slot "+std::to_string(n));
    }
    for(std::size_t lod=0;lod<4;++lod) {
        const auto* l=h+0x60+mw2::kLodBytes*lod;
        if(lod>=m.numLods) {Check(!Ptr(l,8) && !At<std::uint16_t>(l,4),where+"unused lod");continue;}
        Check(At<float>(l,0)==m.lods[lod].dist && At<std::uint16_t>(l,4)==m.lods[lod].numsurfs && At<std::uint16_t>(l,6)==m.lods[lod].surfIndex,where+"lod range");
        const auto surfsAsset=byName.find({3,spec.name+"_lod"+std::to_string(lod)});
        Check(surfsAsset!=byName.end() && Ptr(l,8)==surfsAsset->second->header.data(),where+"lod surfaces asset");
        const auto* ms=surfsAsset->second->header.data();
        Check(Ptr(l,0x28)==Ptr(ms,8) && !std::memcmp(l+0x10,ms+0x14,24) && At<std::uint16_t>(ms,0x10)==m.lods[lod].numsurfs,where+"SetModelSurfs");
        const auto* surfaces=Ptr(ms,8);
        for(std::size_t k=0;k<m.lods[lod].numsurfs;++k) {
            const auto& sf=m.surfs[m.lods[lod].surfIndex+k];const auto* s=surfaces+mw2::kSurfaceBytes*k;
            const std::string at=where+"surface "+std::to_string(m.lods[lod].surfIndex+k)+": ";
            Check(s[1]==(sf.skinned()?1:0) && At<std::uint16_t>(s,2)==sf.vertCount && At<std::uint16_t>(s,4)==sf.triCount,at+"header");
            Check(s[6]==image[kZoneHandle],at+"zone handle");
            for(std::size_t c=0;c<4;++c) Check(At<std::int16_t>(s,0x18+2*c)==sf.blendCounts[c],at+"blend counts");
            Check(Same(Ptr(s,0x20),sf.vertsBlend) && (!sf.vertsBlend.empty() || !Ptr(s,0x20)),at+"blend weights");
            Check(Within(Ptr(s,0x28),sf.verts.size(),mw2::Vertex) && Same(Ptr(s,0x28),sf.verts),at+"vertices (vertex block)");
            Check(Within(Ptr(s,0x10),sf.tris.size(),mw2::Index) && Same(Ptr(s,0x10),sf.tris),at+"triangles (index block)");
            const std::size_t vOff=static_cast<std::size_t>(Ptr(s,0x28)-blocks[mw2::Vertex].data),iOff=static_cast<std::size_t>(Ptr(s,0x10)-blocks[mw2::Index].data);
            Check(!std::memcmp(lockedVertex.data()+vOff,sf.verts.data(),sf.verts.size()) && !std::memcmp(lockedIndex.data()+iOff,sf.tris.data(),sf.tris.size()),at+"locked GPU copies");
            Check(At<std::uint32_t>(s,0x30)==sf.rigid.size(),at+"rigid count");
            const auto* rigid=Ptr(s,0x38);
            for(std::size_t r=0;r<sf.rigid.size();++r) {
                const auto* e=rigid+mw2::kRigidBytes*r;const auto& l2=sf.rigid[r];
                Check(At<std::uint16_t>(e,0)==l2.boneOffset && At<std::uint16_t>(e,2)==l2.vertCount && At<std::uint16_t>(e,6)==l2.triCount,at+"rigid list");
                const auto* tree=Ptr(e,8);
                Check(!!tree==l2.tree,at+"collision tree presence");
                if(tree) {
                    Check(!std::memcmp(tree,l2.transScale.data(),24) && At<std::uint32_t>(tree,0x18)==l2.nodeCount && At<std::uint32_t>(tree,0x28)==l2.leafCount,at+"collision tree");
                    Check(Same(Ptr(tree,0x20),l2.nodes) && Same(Ptr(tree,0x30),l2.leafs) && (reinterpret_cast<std::uintptr_t>(Ptr(tree,0x20))&15)==0,at+"collision tree data");
                    ++report.collisionTrees;
                }
                ++report.rigid;
            }
            for(std::size_t b=0;b<5;++b) Check(At<std::uint32_t>(s,0x40+4*b)==sf.partBits[b],at+"part bits");
            Check((reinterpret_cast<std::uintptr_t>(Ptr(s,0x28))&15)==0 && (reinterpret_cast<std::uintptr_t>(Ptr(s,0x10))&15)==0,at+"alignment");
            ++report.surfaces;report.vertices+=sf.vertCount;report.triangles+=sf.triCount;
        }
    }
    Check(At<std::int32_t>(h,0x150)==static_cast<std::int32_t>(m.collSurfs.size()),where+"collision surface count");
    const auto* coll=Ptr(h,0x148);
    for(std::size_t n=0;n<m.collSurfs.size();++n) {
        const auto* c=coll+mw2::kCollSurfBytes*n;const auto& cs=m.collSurfs[n];
        Check(Same(Ptr(c,0),cs.tris) && At<std::int32_t>(c,8)==static_cast<std::int32_t>(cs.tris.size()/48) && At<std::int32_t>(c,0x24)==cs.boneIdx
            && At<std::int32_t>(c,0x28)==cs.contents && At<std::int32_t>(c,0x2c)==cs.surfFlags,where+"collision surface");
    }
    const auto* info=Ptr(h,0x158);
    for(std::size_t n=0;n<m.numBones;++n) {
        const auto* t=m.boneInfo.data()+t5::kBoneInfoBytes*n;const auto* b=info+mw2::kBoneInfoBytes*n;
        for(std::size_t k=0;k<3;++k) {
            const float lo=Get<float>(t+4*k),hi=Get<float>(t+12+4*k);
            Check(At<float>(b,4*k)==(lo+hi)*.5f && At<float>(b,12+4*k)==(hi-lo)*.5f,where+"bone bounds");
        }
        Check(At<float>(b,24)==Get<float>(t+36),where+"bone radius");
    }
    Check(At<std::uint8_t>(h,0x141)==m.numLods && static_cast<std::int8_t>(h[0x142])==m.collLod && At<float>(h,0x160)==m.radius,where+"lod count/radius");
    Check(!Ptr(h,0x188) && !Ptr(h,0x190),where+"physics");
    ++report.models;
}
void VerifyMaterials(const ZoneSpec& zone,Report& report) {
    for(const auto& want:zone.materials) {
        const auto found=byName.find({5,want.name});
        Check(found!=byName.end() && !found->second->placeholder,"material "+want.name+" not registered");
        const auto* h=found->second->header.data();
        const std::string where="material "+want.name+": ";
        Check(h[8]==mw2::kGameFlags && h[9]==mw2::kSortOpaque && At<std::uint32_t>(h,0x18)==mw2::kSurfaceFlesh && !std::memcmp(h+0x20,mw2::kStateBitsEntry.data(),48),where+"info");
        Check(h[0x50]==3 && h[0x51]==2 && h[0x52]==mw2::kStateBits.size() && h[0x53]==mw2::kStateFlags,where+"counts");
        const auto technique=byName.find({9,mw2::kTechniqueSet});
        Check(technique!=byName.end() && Ptr(h,0x58)==technique->second->header.data(),where+"technique set reference");
        const auto* defs=Ptr(h,0x60);
        const std::uint32_t hashes[3]={kHashSpecularMap,kHashNormalMap,kHashColorMap};const std::uint8_t sem[3]={8,5,2};
        const std::string* images[3]={&want.spec,&want.normal,&want.color};
        for(std::size_t n=0;n<3;++n) {
            const auto* d=defs+16*n;
            Check(At<std::uint32_t>(d,0)==hashes[n] && d[6]==mw2::kSamplerDefault && d[7]==sem[n],where+"texture def");
            const std::string name=(*images[n])[0]==','?images[n]->substr(1):*images[n];
            const auto img=byName.find({10,name});
            Check(img!=byName.end() && Ptr(d,8)==img->second->header.data(),where+"image "+name);
            if((*images[n])[0]==',') {Check(img->second->placeholder,where+"MW2 image should stay a reference");continue;}
            const auto& tex=zone.images.at(name);const auto up=uploads.find(name);
            Check(up!=uploads.end(),where+"image "+name+" never uploaded");
            const auto& u=up->second;
            Check(u.levels==tex.levels && u.flags==(tex.levels>1?0:2) && u.format==tex.format && u.size==tex.data.size() && u.data==tex.data,where+"loadDef "+name);
            Check(u.w==tex.width && u.h==tex.height && u.d==1 && u.mapType==3 && u.semantic==sem[n],where+"image header "+name);
            const auto* ih=img->second->header.data();
            Check(Get<std::uint64_t>(ih)==0x7e7e7e7e7e7e7e7eull,where+"texture pointer not stored by Load_Texture");
        }
        const auto* c=Ptr(h,0x68);
        Check(At<std::uint32_t>(c,0)==mw2::kHashEnvMapParms && !std::memcmp(c+4,"envMapParms",11) && At<float>(c,16+4)==want.envMapParms[1],where+"envMapParms");
        Check(At<std::uint32_t>(c,32)==mw2::kHashColorTint && At<float>(c,48)==1.f && (reinterpret_cast<std::uintptr_t>(c)&15)==0,where+"colorTint");
        const auto* sb=Ptr(h,0x70);
        for(std::size_t n=0;n<mw2::kStateBits.size();++n) Check(At<std::uint64_t>(sb,8*n)==mw2::kStateBits[n],where+"state bits");
        ++report.materials;
    }
    for(const auto& [name,tex]:zone.images) {(void)tex;Check(uploads.count(name)==1,"image "+name+" not uploaded");++report.images;}
}

Report LoadAndVerify(const ZoneSpec& zone,const std::vector<std::uint8_t>& file) {
    // Fresh state for each zone.
    assets.clear();byName.clear();uploads.clear();slStrings.assign(1,"");references=0;failure.clear();
    for(auto& b:blocks){if(b.data)munmap(b.data,b.mapped);b=Block{};}
    rawFile.assign(file.begin(),file.begin()+21);rawPos=0;
    // Inflate the stored zlib stream exactly as zlib would (stored blocks only).
    stream.clear();streamPos=0;
    std::size_t at=21+2;
    for(;;) {
        const bool last=file.at(at)&1;const std::size_t n=file[at+1]|(file[at+2]<<8);
        Check(((file[at+3]|(file[at+4]<<8))^0xffff)==n,"bad stored block");
        stream.insert(stream.end(),file.begin()+static_cast<std::ptrdiff_t>(at+5),file.begin()+static_cast<std::ptrdiff_t>(at+5+n));at+=5+n;
        if(last) break;
    }
    std::uint8_t zoneStruct[0xf0]{};std::strcpy(reinterpret_cast<char*>(zoneStruct+8),"bo1mw2_probe");
    std::uint8_t zoneMem[0xa0]{};
    *reinterpret_cast<std::uint32_t*>(image+kProgressFlag)=0;
    using LoadFn=void(MOD_NATIVE_ABI*)(std::uint8_t*,std::uint8_t*);
    if(setjmp(escape)) Check(false,failure);
    reinterpret_cast<LoadFn>(image+kLoadXFile)(zoneMem,zoneStruct);
    Check(failure.empty(),failure);
    Check(rawPos==21,"header not read exactly");
    Check(streamPos==stream.size(),"stream not consumed exactly ("+std::to_string(streamPos)+" of "+std::to_string(stream.size())+")");
    // Every block position stayed inside its declared size, and no guard byte changed.
    for(std::size_t n=0;n<8;++n) {
        const auto& b=blocks[n];
        const auto pos=*reinterpret_cast<std::uint8_t**>(image+kStreamPositions+8*n);
        if(!b.data) continue;
        Check(pos>=b.data && pos<=b.data+b.size,"block "+std::to_string(n)+" position outside its size");
        for(std::size_t k=b.size;k<b.mapped;++k) Check(b.data[k]==0xcd,"block "+std::to_string(n)+" overran its size");
    }
    Report report;
    VerifyMaterials(zone,report);
    for(const auto& m:zone.models) VerifyModel(m,zone,report);
    return report;
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
        Jump(kAddXModel,reinterpret_cast<void*>(&Add<4>));Jump(kAddXModelSurfs,reinterpret_cast<void*>(&Add<3>));
        Jump(kAddMaterial,reinterpret_cast<void*>(&Add<5>));Jump(kAddTechniqueSet,reinterpret_cast<void*>(&Add<9>));
        Jump(kAddImage,reinterpret_cast<void*>(&Add<10>));Jump(kLoadTexture,reinterpret_cast<void*>(&LoadTexture));
        Jump(kSlGetString,reinterpret_cast<void*>(&SlGetString));

        std::string json="{";
        auto emit=[&](const char* tag,const Report& r,std::size_t fileBytes,const ZoneResult& z) {
            char buffer[640];
            std::snprintf(buffer,sizeof(buffer),"\"%s\":{\"models\":%zu,\"surfaces\":%zu,\"materials\":%zu,\"images\":%zu,\"vertices\":%zu,"
                "\"triangles\":%zu,\"rigid_lists\":%zu,\"collision_trees\":%zu,\"references_resolved\":%zu,\"script_strings\":%zu,"
                "\"fastfile_bytes\":%zu,\"block_sizes\":[%u,%u,%u,%u,%u,%u,%u,%u]}",tag,r.models,r.surfaces,r.materials,r.images,r.vertices,
                r.triangles,r.rigid,r.collisionTrees,references,slStrings.size()-1,fileBytes,z.blockSize[0],z.blockSize[1],z.blockSize[2],
                z.blockSize[3],z.blockSize[4],z.blockSize[5],z.blockSize[6],z.blockSize[7]);
            json+=(json.size()>1?",":"");json+=buffer;
        };
        // 1. The synthetic character (always).
        synthetic::Scene scene;synthetic::Build(scene);
        ZoneResult z;std::string error;
        Check(BuildZone(scene.spec,z,&error),"BuildZone: "+error);
        const auto file=FastFile(z);
        const auto r=LoadAndVerify(scene.spec,file);
        Check(r.models==2 && r.collisionTrees==2 && references>=4,"synthetic coverage");
        emit("synthetic",r,file.size(),z);
        // 2. Optionally, the real Kino character from a local decompressed zone.
        if(argc>2) {
            std::ifstream zin(argv[2],std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(zin)),{});
            const auto strings=mod::bo1xanim::ParseZoneStrings(raw.data(),raw.size());
            Check(!!strings,"local zone strings");
            const auto c=CarveCharacter(raw.data(),raw.size(),strings->strings,strings->assetsOffset,&error);
            Check(!!c,"carve: "+error);
            std::map<std::string,Texture> tex;
            std::uint8_t seed=1;
            for(const auto* n:{&c->headColor,&c->headNormal,&c->headSpec,&c->eyeColor,&c->bodyColor,&c->bodyNormal,&c->bodySpec})
                tex[*n]=synthetic::Dxt1(64,32,seed++);
            ZoneSpec spec;Check(CharacterSpec(*c,tex,spec,&error),"spec: "+error);
            ZoneResult zr;Check(BuildZone(spec,zr,&error),"BuildZone: "+error);
            const auto ff=FastFile(zr);
            const auto rr=LoadAndVerify(spec,ff);
            Check(rr.models==6,"character coverage");
            emit("kino_character",rr,ff.size(),zr);
        }
        json+="}";
        std::puts(json.c_str());
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"%s\n",e.what());return 1;
    }
}
