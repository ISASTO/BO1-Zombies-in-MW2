#pragma once
// Test40: Black Ops zombie character models for MW2.
//
// Reads the Kino der Toten honorguard zombie (two bodies, four heads) from the
// owner's own zombie_theater.ff (T5 PC fastfile v473), and writes a retail
// x64 MW2 fastfile (IWffu100, version 276) holding them as MW2 XModels with
// their own materials and textures. MW2's loader then does the rest: asset
// registration, script strings for the bones, vertex/index buffers and
// textures. Nothing from Black Ops is shipped: the fastfile is generated on
// the owner's machine from the owner's install.
//
// The MW2 side follows the retail x64 loader exactly (Load_XModelPtr 0xe7120,
// Load_XModel 0xe6c80, Load_XModelSurfs 0xe72e0, Load_XSurfaceArray 0xe75d0,
// Load_Material 0xe0910, Load_MaterialTechniqueSet 0xe11a0, Load_GfxImage
// 0xde1c0, Load_GfxTexture 0xde790): every allocation's block and alignment
// is replayed so the XFile block sizes are exact.
//
// Header-only and free of engine state so the native tests can exercise it.
#include "bo1_xanim.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mod::bo1model {
using bo1xanim::Reader;
using bo1xanim::Put;
using bo1xanim::Get;

// ---------------------------------------------------------------- T5 input
namespace t5 {
inline constexpr std::uint32_t kInline=0xffffffffu,kInsert=0xfffffffeu;
inline bool IsInline(std::uint32_t p) {return p==kInline || p==kInsert;}
inline constexpr std::size_t kModelBytes=252,kSurfaceBytes=68,kMaterialBytes=192,kTextureDefBytes=16,kImageBytes=52,
    kLoadDefBytes=12,kRigidBytes=12,kTreeBytes=40,kTreeNodeBytes=16,kCollSurfBytes=44,kCollTriBytes=48,kBoneInfoBytes=44,
    kHighMipBytes=16,kConstantBytes=32,kStateBitsBytes=8,kTechniqueSetBytes=0;   // technique sets are never inline here
inline constexpr std::uint8_t kWaterSemantic=11;
} // namespace t5

struct ImageRef {
    std::string name;
    std::uint32_t nameHash=0;            // sampler name hash in the material
    std::uint8_t semantic=0;
    std::uint16_t width=0,height=0;
    std::uint32_t format=0;              // D3DFORMAT of the loadDef
};
struct MaterialRef {
    enum class Kind {Null,Inline,Alias};
    Kind kind=Kind::Null;
    std::string name;                    // inline (may start with ',' for a reference)
    std::uint32_t alias=0;               // T5 stream alias of the materialHandles slot
    std::vector<ImageRef> textures;      // inline, non-reference materials only
};
struct RigidList {
    std::uint16_t boneOffset=0,vertCount=0,triOffset=0,triCount=0;
    bool tree=false;
    std::array<std::uint8_t,24> transScale{};
    std::uint32_t nodeCount=0,leafCount=0;
    std::vector<std::uint8_t> nodes,leafs;
};
struct Surface {
    std::uint8_t tileMode=0;
    std::uint16_t flags=0,vertCount=0,triCount=0;
    std::array<std::int16_t,4> blendCounts{};
    std::vector<std::uint8_t> vertsBlend,verts,tris;
    std::vector<RigidList> rigid;
    std::array<std::uint32_t,5> partBits{};
    bool skinned() const {return blendCounts[0] || blendCounts[1] || blendCounts[2] || blendCounts[3];}
    std::size_t blendShorts() const {
        return static_cast<std::size_t>(blendCounts[0])+3u*blendCounts[1]+5u*blendCounts[2]+7u*blendCounts[3];
    }
};
struct CollSurf {
    std::vector<std::uint8_t> tris;      // 48 bytes each, same layout in T5 and MW2
    std::array<float,3> mins{},maxs{};
    std::int32_t boneIdx=0,contents=0,surfFlags=0;
};
struct Lod {float dist=0.f;std::uint16_t numsurfs=0,surfIndex=0;};
struct Model {
    std::size_t at=0,end=0;
    std::string name;                    // empty when the zone aliased it
    std::uint8_t numBones=0,numRootBones=0,numsurfs=0,lodRampType=0;
    std::vector<std::string> bones;
    std::vector<std::uint8_t> parentList,quats,trans /*vec3 per non-root bone, packed*/,partClassification,baseMat,boneInfo /*T5, 44 each*/;
    std::vector<Surface> surfs;
    std::vector<MaterialRef> materials;
    std::vector<CollSurf> collSurfs;
    std::array<Lod,4> lods{};
    std::uint16_t numLods=0;
    std::int16_t collLod=-1;
    std::int32_t contents=0;
    float radius=0.f;
    std::array<float,3> mins{},maxs{};
    bool hasBone(const std::string& bone) const {return std::find(bones.begin(),bones.end(),bone)!=bones.end();}
};

namespace detail {
inline bool Fail(std::string* error,const std::string& text) {if(error)*error=text;return false;}

// T5 GfxImage: name, then the texture's loadDef (12 bytes + data).
inline bool ParseImage(Reader& r,ImageRef& out) {
    const auto h=r.take(t5::kImageBytes);
    if(!r.ok()) return false;
    const auto texture=Get<std::uint32_t>(h.data());
    out.width=Get<std::uint16_t>(h.data()+20);out.height=Get<std::uint16_t>(h.data()+22);
    const auto name=Get<std::uint32_t>(h.data()+44);
    if(t5::IsInline(name)) out.name=r.cstring(256);
    if(t5::IsInline(texture)) {
        r.skip(4);out.format=r.get<std::uint32_t>();
        const auto size=r.get<std::uint32_t>();
        r.skip(size);
    } else if(texture!=0) return false;
    return r.ok();
}
// T5 Material: name, technique set (must be an alias), textures, constants, state bits.
inline bool ParseMaterial(Reader& r,MaterialRef& out) {
    const auto h=r.take(t5::kMaterialBytes);
    if(!r.ok()) return false;
    const auto name=Get<std::uint32_t>(h.data());
    const auto textureCount=h[170],constantCount=h[171],stateBitsCount=h[172];
    const auto techniqueSet=Get<std::uint32_t>(h.data()+176),textureTable=Get<std::uint32_t>(h.data()+180);
    const auto constantTable=Get<std::uint32_t>(h.data()+184),stateBitsTable=Get<std::uint32_t>(h.data()+188);
    out.kind=MaterialRef::Kind::Inline;
    if(t5::IsInline(name)) out.name=r.cstring(256);
    if(t5::IsInline(techniqueSet)) return false;
    if(t5::IsInline(textureTable)) {
        struct Def {std::uint32_t hash;std::uint8_t semantic;std::uint32_t image;};
        std::vector<Def> defs;
        for(unsigned n=0;n<textureCount;++n) {
            const auto d=r.take(t5::kTextureDefBytes);
            if(!r.ok()) return false;
            defs.push_back({Get<std::uint32_t>(d.data()),d[7],Get<std::uint32_t>(d.data()+12)});
        }
        for(const auto& d:defs) {
            if(d.semantic==t5::kWaterSemantic) return false;
            ImageRef image;image.nameHash=d.hash;image.semantic=d.semantic;
            if(t5::IsInline(d.image) && !ParseImage(r,image)) return false;
            out.textures.push_back(image);
        }
    } else if(textureTable!=0 && textureCount) return false;
    if(t5::IsInline(constantTable)) r.skip(t5::kConstantBytes*constantCount);
    if(t5::IsInline(stateBitsTable)) r.skip(t5::kStateBitsBytes*stateBitsCount);
    return r.ok();
}
} // namespace detail

// Parse one T5 XModel whose 252-byte header starts at `at`. Skeleton arrays
// must be inline; material slots may be inline, null or aliases. `end`
// receives the first byte after the model (the next asset).
inline bool ParseModel(const std::uint8_t* raw,std::size_t size,std::size_t at,const std::vector<std::string>& strings,
                       Model& m,std::string* error=nullptr) {
    using detail::Fail;
    Reader r(raw,size,at);
    const auto h=r.take(t5::kModelBytes);
    if(!r.ok()) return Fail(error,"truncated header");
    m=Model{};m.at=at;
    const auto name=Get<std::uint32_t>(h.data());
    m.numBones=h[4];m.numRootBones=h[5];m.numsurfs=h[6];m.lodRampType=h[7];
    std::array<std::uint32_t,8> p{};for(std::size_t n=0;n<8;++n)p[n]=Get<std::uint32_t>(h.data()+8+4*n);
    enum {BoneNames,ParentList,Quats,Trans,PartClassification,BaseMat,Surfs,MaterialHandles};
    for(std::size_t n=0;n<4;++n) {
        const auto* lod=h.data()+40+32*n;
        m.lods[n].dist=Get<float>(lod);m.lods[n].numsurfs=Get<std::uint16_t>(lod+4);m.lods[n].surfIndex=Get<std::uint16_t>(lod+6);
    }
    const auto collSurfs=Get<std::uint32_t>(h.data()+172);const auto numCollSurfs=Get<std::int32_t>(h.data()+176);
    m.contents=Get<std::int32_t>(h.data()+180);
    const auto boneInfo=Get<std::uint32_t>(h.data()+184);
    m.radius=Get<float>(h.data()+188);
    for(std::size_t n=0;n<3;++n){m.mins[n]=Get<float>(h.data()+192+4*n);m.maxs[n]=Get<float>(h.data()+204+4*n);}
    m.numLods=Get<std::uint16_t>(h.data()+216);m.collLod=Get<std::int16_t>(h.data()+218);
    const auto highMip=Get<std::uint32_t>(h.data()+220);
    const auto physPreset=Get<std::uint32_t>(h.data()+236);const auto numCollmaps=h[240];
    const auto collmaps=Get<std::uint32_t>(h.data()+244);const auto physConstraints=Get<std::uint32_t>(h.data()+248);
    if(!m.numBones || m.numRootBones>m.numBones || !m.numsurfs || m.numLods<1 || m.numLods>4 || numCollSurfs<0 || numCollSurfs>4096)
        return Fail(error,"implausible header");
    if(!t5::IsInline(p[BoneNames]) || !t5::IsInline(p[PartClassification]) || !t5::IsInline(p[BaseMat])
        || !t5::IsInline(p[Surfs]) || !t5::IsInline(p[MaterialHandles]))
        return Fail(error,"aliased model arrays");
    const std::size_t nb=m.numBones,nc=static_cast<std::size_t>(m.numBones-m.numRootBones);
    for(int n:{ParentList,Quats,Trans}) if(nc ? !t5::IsInline(p[static_cast<std::size_t>(n)]) : p[static_cast<std::size_t>(n)]!=0)
        return Fail(error,"aliased model arrays");
    if(physPreset || (numCollmaps && collmaps) || physConstraints) return Fail(error,"physics data is not supported");
    if(t5::IsInline(name)) m.name=r.cstring(256);
    for(std::size_t n=0;n<nb;++n) {
        const auto id=r.get<std::uint16_t>();
        if(!r.ok() || id>=strings.size() || strings[id].empty()) return Fail(error,"bad bone name");
        m.bones.push_back(strings[id]);
    }
    if(nc) {
        m.parentList=r.take(nc);m.quats=r.take(8*nc);
        // T5 allocates 16 bytes per bone for the translations but stores them
        // packed, 12 bytes (vec3) per bone, followed by 4*nc zero bytes. Test40
        // read 16-byte records and scrambled every bone after the first (the
        // folded, stretched zombies in the Test42 screenshot).
        const auto t=r.take(16*nc);
        if(t.size()!=16*nc) return Fail(error,"truncated skeleton");
        if(std::any_of(t.begin()+12*nc,t.end(),[](std::uint8_t v){return v!=0;})) return Fail(error,"unexpected bone translation layout");
        m.trans.assign(t.begin(),t.begin()+12*nc);
    }
    m.partClassification=r.take(nb);m.baseMat=r.take(32*nb);
    if(!r.ok()) return Fail(error,"truncated skeleton");
    std::vector<std::vector<std::uint8_t>> heads(m.numsurfs);
    for(auto& s:heads) s=r.take(t5::kSurfaceBytes);
    if(!r.ok()) return Fail(error,"truncated surfaces");
    for(const auto& b:heads) {
        Surface s;s.tileMode=b[0];const std::size_t rigidCount=b[1];s.flags=Get<std::uint16_t>(b.data()+2);
        s.vertCount=Get<std::uint16_t>(b.data()+4);s.triCount=Get<std::uint16_t>(b.data()+6);
        const auto tris=Get<std::uint32_t>(b.data()+12);
        for(std::size_t n=0;n<4;++n) s.blendCounts[n]=Get<std::int16_t>(b.data()+16+2*n);
        const auto blend=Get<std::uint32_t>(b.data()+24),tension=Get<std::uint32_t>(b.data()+28),verts=Get<std::uint32_t>(b.data()+32);
        const auto vertList=Get<std::uint32_t>(b.data()+40);
        for(std::size_t n=0;n<5;++n) s.partBits[n]=Get<std::uint32_t>(b.data()+48+4*n);
        for(auto c:s.blendCounts) if(c<0) return Fail(error,"bad blend counts");
        const bool skinned=s.skinned();
        if(skinned!=t5::IsInline(blend) || (blend && !t5::IsInline(blend))) return Fail(error,"blend data mismatch");
        if(skinned) s.vertsBlend=r.take(2*s.blendShorts());
        if(t5::IsInline(tension)) r.skip(4u*12u*(static_cast<std::size_t>(s.blendCounts[0])+s.blendCounts[1]+s.blendCounts[2]+s.blendCounts[3]));
        else if(tension) return Fail(error,"aliased tension data");
        if(!t5::IsInline(verts) || !t5::IsInline(tris)) return Fail(error,"aliased vertex data");
        s.verts=r.take(32u*s.vertCount);
        if(rigidCount) {
            if(!t5::IsInline(vertList)) return Fail(error,"aliased rigid lists");
            std::vector<std::uint32_t> trees;
            for(std::size_t n=0;n<rigidCount;++n) {
                const auto e=r.take(t5::kRigidBytes);
                if(!r.ok()) return Fail(error,"truncated rigid list");
                RigidList l;l.boneOffset=Get<std::uint16_t>(e.data());l.vertCount=Get<std::uint16_t>(e.data()+2);
                l.triOffset=Get<std::uint16_t>(e.data()+4);l.triCount=Get<std::uint16_t>(e.data()+6);
                s.rigid.push_back(l);trees.push_back(Get<std::uint32_t>(e.data()+8));
            }
            for(std::size_t n=0;n<rigidCount;++n) {
                if(!trees[n]) continue;
                if(!t5::IsInline(trees[n])) return Fail(error,"aliased collision tree");
                const auto t=r.take(t5::kTreeBytes);
                if(!r.ok()) return Fail(error,"truncated collision tree");
                auto& l=s.rigid[n];l.tree=true;std::memcpy(l.transScale.data(),t.data(),24);
                l.nodeCount=Get<std::uint32_t>(t.data()+24);l.leafCount=Get<std::uint32_t>(t.data()+32);
                const auto nodes=Get<std::uint32_t>(t.data()+28),leafs=Get<std::uint32_t>(t.data()+36);
                if((nodes && !t5::IsInline(nodes)) || (leafs && !t5::IsInline(leafs)) || l.nodeCount>65536 || l.leafCount>65536)
                    return Fail(error,"bad collision tree");
                if(nodes) l.nodes=r.take(t5::kTreeNodeBytes*l.nodeCount); else l.nodeCount=0;
                if(leafs) l.leafs=r.take(2u*l.leafCount); else l.leafCount=0;
            }
        } else if(vertList) return Fail(error,"unexpected rigid list");
        s.tris=r.take(6u*s.triCount);
        if(!r.ok()) return Fail(error,"truncated surface data");
        m.surfs.push_back(std::move(s));
    }
    std::vector<std::uint32_t> slots(m.numsurfs);
    for(auto& v:slots) v=r.get<std::uint32_t>();
    for(auto v:slots) {
        MaterialRef ref;
        if(t5::IsInline(v)) {if(!detail::ParseMaterial(r,ref)) return Fail(error,"unsupported material");}
        else if(v) {ref.kind=MaterialRef::Kind::Alias;ref.alias=v;}
        m.materials.push_back(std::move(ref));
    }
    if(numCollSurfs) {
        if(!t5::IsInline(collSurfs)) return Fail(error,"aliased collision surfaces");
        std::vector<std::uint32_t> triPointers;
        for(int n=0;n<numCollSurfs;++n) {
            const auto c=r.take(t5::kCollSurfBytes);
            if(!r.ok()) return Fail(error,"truncated collision surfaces");
            CollSurf cs;triPointers.push_back(Get<std::uint32_t>(c.data()));
            cs.tris.resize(t5::kCollTriBytes*static_cast<std::size_t>(std::max(0,Get<std::int32_t>(c.data()+4))));
            for(std::size_t k=0;k<3;++k){cs.mins[k]=Get<float>(c.data()+8+4*k);cs.maxs[k]=Get<float>(c.data()+20+4*k);}
            cs.boneIdx=Get<std::int32_t>(c.data()+32);cs.contents=Get<std::int32_t>(c.data()+36);cs.surfFlags=Get<std::int32_t>(c.data()+40);
            m.collSurfs.push_back(std::move(cs));
        }
        for(std::size_t n=0;n<m.collSurfs.size();++n) {
            auto& cs=m.collSurfs[n];
            if(t5::IsInline(triPointers[n])) cs.tris=r.take(cs.tris.size());
            else if(triPointers[n]) return Fail(error,"aliased collision triangles");
            else cs.tris.clear();
        }
    }
    if(t5::IsInline(boneInfo)) m.boneInfo=r.take(t5::kBoneInfoBytes*nb);
    else if(boneInfo) return Fail(error,"aliased bone info");
    if(t5::IsInline(highMip)) r.skip(t5::kHighMipBytes*m.numsurfs);
    else if(highMip) return Fail(error,"aliased high mip bounds");
    if(!r.ok()) return Fail(error,"truncated model");
    // LOD surface ranges must tile the surface list.
    std::size_t next=0;
    for(std::size_t n=0;n<m.numLods;++n) {
        if(m.lods[n].surfIndex!=next || !m.lods[n].numsurfs) return Fail(error,"bad lod ranges");
        next+=m.lods[n].numsurfs;
    }
    if(next!=m.numsurfs) return Fail(error,"bad lod ranges");
    for(const auto& s:m.surfs) {
        if(s.skinned() && static_cast<std::size_t>(s.blendCounts[0])+s.blendCounts[1]+s.blendCounts[2]+s.blendCounts[3]!=s.vertCount)
            return Fail(error,"blend counts do not cover the vertices");
        for(std::size_t n=0;n<s.tris.size();n+=2) if(Get<std::uint16_t>(s.tris.data()+n)>=s.vertCount) return Fail(error,"triangle index out of range");
    }
    m.end=r.pos();
    return true;
}

// The skeleton's own consistency check: composing every bone's local
// rotation and translation from its parent (parentList holds the distance
// back to the parent, as in MW2) must land on the model-space rest pose that
// baseMat stores (quat xyzw, trans xyz, transWeight). Returns the largest
// distance in units; MW2's own defaultactor and the corrected BO1 models give
// 0.0, Test40's misread translations gave 54.8.
inline float SkeletonError(const Model& m) {
    using Quat=std::array<float,4>;using Vec=std::array<float,3>;
    const std::size_t nb=m.numBones,nr=m.numRootBones,nc=nb-nr;
    if(!nb || m.baseMat.size()!=32*nb || m.parentList.size()!=nc || m.quats.size()!=8*nc || m.trans.size()!=12*nc) return 1e9f;
    auto mul=[](const Quat& a,const Quat& b)->Quat {
        return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
                a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    };
    auto normal=[](Quat q)->Quat {const float l=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);if(l>0)for(auto& v:q)v/=l;return q;};
    auto rotate=[&](const Quat& q,const Vec& v)->Vec {
        const auto r=mul(mul(q,{v[0],v[1],v[2],0.f}),Quat{-q[0],-q[1],-q[2],q[3]});return {r[0],r[1],r[2]};
    };
    auto base=[&](std::size_t i,Quat& q,Vec& tr) {
        for(std::size_t k=0;k<4;++k) q[k]=Get<float>(m.baseMat.data()+32*i+4*k);
        for(std::size_t k=0;k<3;++k) tr[k]=Get<float>(m.baseMat.data()+32*i+16+4*k);
        q=normal(q);
    };
    std::vector<Quat> wq(nb);std::vector<Vec> wt(nb);
    for(std::size_t i=0;i<nr;++i) base(i,wq[i],wt[i]);
    float worst=0.f;
    for(std::size_t i=nr;i<nb;++i) {
        const std::size_t c=i-nr,back=m.parentList[c];
        if(!back || back>i) return 1e9f;
        const std::size_t parent=i-back;
        Quat local;for(std::size_t k=0;k<4;++k) local[k]=Get<std::int16_t>(m.quats.data()+8*c+2*k)/32767.f;
        Vec offset;for(std::size_t k=0;k<3;++k) offset[k]=Get<float>(m.trans.data()+12*c+4*k);
        wq[i]=normal(mul(wq[parent],local));
        const auto moved=rotate(wq[parent],offset);
        for(std::size_t k=0;k<3;++k) wt[i][k]=wt[parent][k]+moved[k];
        Quat bq;Vec bt;base(i,bq,bt);
        const float d=std::sqrt((wt[i][0]-bt[0])*(wt[i][0]-bt[0])+(wt[i][1]-bt[1])*(wt[i][1]-bt[1])+(wt[i][2]-bt[2])*(wt[i][2]-bt[2]));
        if(!(d<=worst)) worst=d;
    }
    return worst;
}
inline constexpr float kSkeletonTolerance=0.05f;

// ------------------------------------------------- the Kino honorguard zombie
inline constexpr const char* kHead1Material="mc/mtl_c_ger_zombie_head1";
inline constexpr const char* kEyesMaterial="mc/mtl_c_ger_zombie_eyes";
inline constexpr const char* kBodyFirstMaterial=",mc/mtl_c_ger_honorguard_zombie_linen";
// The honorguard body texture sheet; the zombie body materials live in another
// zone, but every body surface maps into this sheet (LOD1-3 use one material).
inline constexpr const char* kBodySheetMaterial="mc/mtl_char_ger_honorguard_lthr1_1";
inline constexpr std::uint32_t kHashColorMap=0xa0ab1041u,kHashNormalMap=0x59d30d0fu,kHashSpecularMap=0x34ecccb3u;

enum class Skin : std::uint8_t {Body,HeadSkin,Eyes};
struct Character {
    Model body1,body2;
    std::array<Model,4> heads;
    std::uint32_t headSkinAlias=0,eyesAlias=0;
    std::string headColor,headNormal,headSpec,headGloss,eyeColor,bodyColor,bodyNormal,bodySpec;
    // Material class of every surface of a model of this character.
    Skin skinOf(const Model& m,std::size_t surface) const {
        const auto& ref=m.materials[surface];
        if(ref.kind==MaterialRef::Kind::Alias) {
            if(ref.alias==headSkinAlias) return Skin::HeadSkin;
            if(ref.alias==eyesAlias) return Skin::Eyes;
            return Skin::Body;
        }
        if(ref.name==kHead1Material) return Skin::HeadSkin;
        if(ref.name==kEyesMaterial) return Skin::Eyes;
        return Skin::Body;
    }
};

namespace detail {
inline bool LooksLikeModel(const std::uint8_t* h) {
    const auto nb=h[4],nr=h[5],ns=h[6];
    if(!nb || !nr || nr>nb || !ns || ns>64) return false;
    const auto lods=Get<std::uint16_t>(h+216);
    if(lods<1 || lods>4) return false;
    return Get<std::uint32_t>(h+8)==t5::kInline && Get<std::uint32_t>(h+32)==t5::kInline && Get<std::uint32_t>(h+36)==t5::kInline;
}
inline std::string TextureBySuffix(const MaterialRef& m,const char* suffix) {
    const std::size_t n=std::strlen(suffix);
    for(const auto& t:m.textures) if(t.name.size()>n && t.name.compare(t.name.size()-n,n,suffix)==0 && t.name[0]!=',') return t.name;
    return {};
}
inline std::string TextureByHash(const MaterialRef& m,std::uint32_t hash) {
    for(const auto& t:m.textures) if(t.nameHash==hash && !t.name.empty() && t.name[0]!=',') return t.name;
    return {};
}
} // namespace detail

// Find and parse the honorguard zombie in a decompressed zombie_theater zone.
inline std::optional<Character> CarveCharacter(const std::uint8_t* raw,std::size_t size,const std::vector<std::string>& strings,
                                               std::size_t start,std::string* error=nullptr) {
    auto fail=[&](const char* text){if(error)*error=text;return std::optional<Character>{};};
    std::vector<Model> models;
    for(std::size_t at=start;at+t5::kModelBytes<size;++at) {
        if(raw[at+8]!=0xff || raw[at+32]!=0xff || !detail::LooksLikeModel(raw+at)) continue;
        Model m;
        if(!ParseModel(raw,size,at,strings,m)) continue;
        if(!m.bones.empty() && (m.bones[0]=="j_spine4" || m.bones[0]=="tag_origin")) {
            const auto end=m.end;
            models.push_back(std::move(m));
            at=end-1;
        }
    }
    Character c;
    const Model* head1=nullptr;const Model* body1=nullptr;
    for(const auto& m:models) {
        if(!head1 && m.bones[0]=="j_spine4" && m.hasBone("tag_eye") && m.materials.size()>=2
            && m.materials[0].kind==MaterialRef::Kind::Inline && m.materials[0].name==kHead1Material
            && m.materials[1].kind==MaterialRef::Kind::Inline && m.materials[1].name==kEyesMaterial) head1=&m;
        if(!body1 && m.bones.size()>=3 && m.bones[1]=="j_mainroot" && m.bones[2]=="tag_sync" && !m.materials.empty()
            && m.materials[0].kind==MaterialRef::Kind::Inline && m.materials[0].name==kBodyFirstMaterial) body1=&m;
    }
    if(!head1) return fail("zombie head (c_ger_zombie_head1) not found");
    if(!body1) return fail("zombie body (c_ger_honorguard_body1) not found");
    // head1 aliases its own material slots 0 (skin) and 1 (eyes) for LOD1-3.
    std::uint32_t low=0,high=0;
    for(const auto& ref:head1->materials) if(ref.kind==MaterialRef::Kind::Alias) {
        if(!low || ref.alias<low) low=ref.alias;
        if(ref.alias>high) high=ref.alias;
    }
    if(!low || high!=low+4) return fail("zombie head material slots not recognised");
    c.headSkinAlias=low;c.eyesAlias=high;
    std::vector<const Model*> otherHeads;
    const Model* body2=nullptr;
    for(const auto& m:models) {
        if(&m==head1 || &m==body1) continue;
        if(m.bones[0]=="j_spine4" && m.hasBone("tag_eye") && m.numsurfs==head1->numsurfs) {
            bool skin=false,eyes=false,other=false;
            for(const auto& ref:m.materials) {
                if(ref.kind!=MaterialRef::Kind::Alias) {other=true;continue;}
                if(ref.alias==c.headSkinAlias) skin=true; else if(ref.alias==c.eyesAlias) eyes=true; else other=true;
            }
            if(skin && eyes && !other) otherHeads.push_back(&m);
        }
        if(!body2 && m.at>body1->at && m.bones==body1->bones && m.numsurfs==body1->numsurfs
            && m.lods[0].numsurfs==body1->lods[0].numsurfs && m.lods[0].numsurfs>=2) {
            bool ok=true;
            const std::size_t last=m.lods[0].numsurfs-1u;
            for(std::size_t n=0;n<=last;++n) {
                const auto& ref=m.materials[n];
                const bool skinSlot=ref.kind==MaterialRef::Kind::Alias && ref.alias==c.headSkinAlias;
                if(ref.kind!=MaterialRef::Kind::Alias || (n==last)!=skinSlot) ok=false;
            }
            if(ok) body2=&m;
        }
    }
    if(otherHeads.size()!=3) return fail("expected three more zombie heads");
    if(!body2) return fail("zombie body (c_ger_honorguard_body2) not found");
    c.heads[0]=*head1;for(std::size_t n=0;n<3;++n) c.heads[n+1]=*otherHeads[n];
    c.body1=*body1;c.body2=*body2;
    // Never ship a skeleton that does not reproduce its own rest pose.
    for(const auto* m:{&c.body1,&c.body2,&c.heads[0],&c.heads[1],&c.heads[2],&c.heads[3]})
        if(!(SkeletonError(*m)<=kSkeletonTolerance)) return fail("zombie skeleton does not match its base pose");
    c.headColor=detail::TextureBySuffix(head1->materials[0],"_c");
    c.headNormal=detail::TextureBySuffix(head1->materials[0],"_n");
    c.headSpec=detail::TextureBySuffix(head1->materials[0],"_s");
    c.headGloss=detail::TextureBySuffix(head1->materials[0],"_g");
    c.eyeColor=detail::TextureByHash(head1->materials[1],kHashColorMap);
    if(c.headColor.empty() || c.headNormal.empty() || c.headSpec.empty() || c.eyeColor.empty()) return fail("zombie head textures not named");
    // The body sheet material: header 192 bytes before its inline name.
    const std::string sheet=std::string(kBodySheetMaterial)+'\0';
    const auto* hit=std::search(raw+start,raw+size,sheet.begin(),sheet.end());
    if(hit==raw+size || hit-raw<static_cast<std::ptrdiff_t>(t5::kMaterialBytes)) return fail("honorguard body textures not found");
    Reader r(raw,size,static_cast<std::size_t>(hit-raw)-t5::kMaterialBytes);
    MaterialRef bodySheet;
    if(Get<std::uint32_t>(raw+(hit-raw)-t5::kMaterialBytes)!=t5::kInline || !detail::ParseMaterial(r,bodySheet) || bodySheet.name!=kBodySheetMaterial)
        return fail("honorguard body material unreadable");
    c.bodyColor=detail::TextureByHash(bodySheet,kHashColorMap);
    c.bodyNormal=detail::TextureByHash(bodySheet,kHashNormalMap);
    c.bodySpec=detail::TextureByHash(bodySheet,kHashSpecularMap);
    if(c.bodyColor.empty() || c.bodyNormal.empty() || c.bodySpec.empty()) return fail("honorguard body textures not named");
    return c;
}

// --------------------------------------------------------- textures (IWI 13)
inline constexpr std::uint32_t kD3dDxt1=0x31545844u,kD3dDxt3=0x33545844u,kD3dDxt5=0x35545844u;
struct Texture {
    std::uint16_t width=0,height=0;
    std::uint32_t format=0;              // D3DFORMAT
    std::uint8_t levels=0;
    std::vector<std::uint8_t> data;      // largest mip first, as MW2's loadDef path uploads
};
inline std::size_t BlockBytes(std::uint32_t format) {return format==kD3dDxt1?8:16;}
inline std::size_t MipBytes(std::uint32_t format,unsigned w,unsigned h) {
    return static_cast<std::size_t>((std::max(1u,w)+3)/4)*((std::max(1u,h)+3)/4)*BlockBytes(format);
}
inline unsigned MipCount(unsigned w,unsigned h) {unsigned n=1;while(w>1 || h>1){w=std::max(1u,w/2);h=std::max(1u,h/2);++n;}return n;}

// Black Ops IWI v13: "IWi" 13, format, flags, w, h, d, gamma, fileSizeForPicmip[8],
// then the mips smallest first. Only DXT1/3/5 2D textures are accepted.
inline bool ParseIwi13(const std::uint8_t* p,std::size_t n,Texture& out,std::string* error=nullptr) {
    using detail::Fail;
    if(n<48 || std::memcmp(p,"IWi",3) || p[3]!=13) return Fail(error,"not an IWI v13 image");
    const auto format=p[4],flags=p[5];
    const auto w=Get<std::uint16_t>(p+6),h=Get<std::uint16_t>(p+8),d=Get<std::uint16_t>(p+10);
    if(flags&(4|8)) return Fail(error,"cube or volume image");
    if(d>1 || !w || !h || w>4096 || h>4096) return Fail(error,"bad image size");
    switch(format) {case 0xb:out.format=kD3dDxt1;break;case 0xc:out.format=kD3dDxt3;break;case 0xd:out.format=kD3dDxt5;break;
        default:return Fail(error,"unsupported image format "+std::to_string(format));}
    out.width=w;out.height=h;
    const unsigned levels=(flags&2)?1u:MipCount(w,h);
    out.levels=static_cast<std::uint8_t>(levels);
    std::vector<std::size_t> offsets(levels);std::size_t at=48;
    for(int level=static_cast<int>(levels)-1;level>=0;--level) {
        offsets[static_cast<std::size_t>(level)]=at;
        at+=MipBytes(out.format,w>>level,h>>level);
    }
    if(at>n) return Fail(error,"image is shorter than its mips");
    out.data.clear();out.data.reserve(at-48);
    for(unsigned level=0;level<levels;++level)
        out.data.insert(out.data.end(),p+offsets[level],p+offsets[level]+MipBytes(out.format,w>>level,h>>level));
    return true;
}

namespace detail {
inline std::array<std::uint8_t,3> Rgb565(std::uint16_t c) {
    return {static_cast<std::uint8_t>(((c>>11)&31)*255/31),static_cast<std::uint8_t>(((c>>5)&63)*255/63),static_cast<std::uint8_t>((c&31)*255/31)};
}
// Green channel of the 16 texels of one DXT1 block.
inline std::array<std::uint8_t,16> Dxt1Green(const std::uint8_t* b) {
    const auto c0=Get<std::uint16_t>(b),c1=Get<std::uint16_t>(b+2);
    const auto a=Rgb565(c0)[1],z=Rgb565(c1)[1];
    std::array<std::uint8_t,4> g{a,z,0,0};
    if(c0>c1){g[2]=static_cast<std::uint8_t>((2*a+z)/3);g[3]=static_cast<std::uint8_t>((a+2*z)/3);}
    else {g[2]=static_cast<std::uint8_t>((a+z)/2);g[3]=0;}
    std::array<std::uint8_t,16> out{};const auto bits=Get<std::uint32_t>(b+4);
    for(unsigned n=0;n<16;++n) out[n]=g[(bits>>(2*n))&3];
    return out;
}
// A DXT5 alpha block (8-value mode) for 16 alpha values.
inline std::array<std::uint8_t,8> AlphaBlock(const std::array<std::uint8_t,16>& a) {
    std::uint8_t hi=0,lo=255;for(auto v:a){hi=std::max(hi,v);lo=std::min(lo,v);}
    std::array<std::uint8_t,8> out{};out[0]=hi;out[1]=lo;
    if(hi==lo) return out;
    std::array<int,8> pal{hi,lo,0,0,0,0,0,0};
    for(int n=1;n<7;++n) pal[static_cast<std::size_t>(n+1)]=((7-n)*hi+n*lo)/7;
    std::uint64_t bits=0;
    for(unsigned n=0;n<16;++n) {
        unsigned best=0;int err=1<<30;
        for(unsigned k=0;k<8;++k){const int e=std::abs(pal[k]-a[n]);if(e<err){err=e;best=k;}}
        bits|=static_cast<std::uint64_t>(best)<<(3*n);
    }
    for(unsigned n=0;n<6;++n) out[2+n]=static_cast<std::uint8_t>(bits>>(8*n));
    return out;
}
} // namespace detail

// MW2's specular map carries the gloss ("cosine power") in alpha: a DXT5 with
// the colour blocks of the BO1 specular map and alpha from its gloss map.
inline bool CombineSpecGloss(const Texture& spec,const Texture& gloss,Texture& out,std::string* error=nullptr) {
    using detail::Fail;
    if(spec.format!=kD3dDxt1 || gloss.format!=kD3dDxt1 || spec.width!=gloss.width || spec.height!=gloss.height || spec.levels!=gloss.levels)
        return Fail(error,"specular and gloss maps differ");
    out.width=spec.width;out.height=spec.height;out.levels=spec.levels;out.format=kD3dDxt5;
    out.data.clear();out.data.reserve(spec.data.size()*2);
    for(std::size_t n=0;n+8<=spec.data.size();n+=8) {
        const auto alpha=detail::AlphaBlock(detail::Dxt1Green(gloss.data.data()+n));
        out.data.insert(out.data.end(),alpha.begin(),alpha.end());
        out.data.insert(out.data.end(),spec.data.begin()+static_cast<std::ptrdiff_t>(n),spec.data.begin()+static_cast<std::ptrdiff_t>(n+8));
    }
    return out.data.size()==spec.data.size()*2;
}
// A DXT1 specular map without a gloss map: the same colours, constant gloss.
inline bool WithGloss(const Texture& spec,std::uint8_t gloss,Texture& out) {
    if(spec.format!=kD3dDxt1) return false;
    out.width=spec.width;out.height=spec.height;out.levels=spec.levels;out.format=kD3dDxt5;
    out.data.clear();out.data.reserve(spec.data.size()*2);
    for(std::size_t n=0;n+8<=spec.data.size();n+=8) {
        const std::uint8_t alpha[8]={gloss,gloss,0,0,0,0,0,0};
        out.data.insert(out.data.end(),alpha,alpha+8);
        out.data.insert(out.data.end(),spec.data.begin()+static_cast<std::ptrdiff_t>(n),spec.data.begin()+static_cast<std::ptrdiff_t>(n+8));
    }
    return true;
}
// A small constant-colour DXT5 texture (all mips), for maps BO1 has no file for.
inline Texture SolidDxt5(std::uint16_t size,std::uint16_t rgb565,std::uint8_t alpha) {
    Texture t;t.width=size;t.height=size;t.format=kD3dDxt5;t.levels=static_cast<std::uint8_t>(MipCount(size,size));
    for(unsigned level=0;level<t.levels;++level) {
        const auto blocks=MipBytes(kD3dDxt5,size>>level,size>>level)/16;
        for(std::size_t b=0;b<blocks;++b) {
            const std::uint8_t block[16]={alpha,alpha,0,0,0,0,0,0,static_cast<std::uint8_t>(rgb565),static_cast<std::uint8_t>(rgb565>>8),
                static_cast<std::uint8_t>(rgb565),static_cast<std::uint8_t>(rgb565>>8),0,0,0,0};
            t.data.insert(t.data.end(),block,block+16);
        }
    }
    return t;
}

// -------------------------------------------------------- MW2 x64 zone file
namespace mw2 {
inline constexpr std::uint32_t kVersion=0x114;
inline constexpr std::uint64_t kInline=0xffffffffffffffffull;
inline constexpr std::size_t kXFileBytes=40,kAssetListBytes=32,kAssetBytes=16,kModelBytes=0x198,kLodBytes=0x38,kSurfsBytes=0x30,
    kSurfaceBytes=0x58,kRigidBytes=0x10,kTreeBytes=0x38,kCollSurfBytes=0x30,kBoneInfoBytes=0x1c,kMaterialBytes=0x78,
    kTextureDefBytes=0x10,kConstantBytes=0x20,kStateBitsBytes=8,kTechniqueSetBytes=0x198,kImageBytes=0x28,kLoadDefBytes=0x10;
enum Block : int {Temp=0,Physical=1,Runtime=2,Virtual=3,Large=4,Callback=5,Vertex=6,Index=7,BlockCount=8};
inline constexpr int kAssetXModel=4;
// Lit, normal-mapped, specular model technique set present in MW2's common.ff.
inline constexpr const char* kTechniqueSet="mc_l_sm_r0c0n0s0";
inline constexpr const char* kIdentityNormal=",$identitynormalmap";
inline constexpr std::uint32_t kHashEnvMapParms=0x3d9994dcu,kHashColorTint=0xb60c3b3au;
// The opaque lit-model state of MW2's own materials (e.g. mc/mtl_weapon_m14).
inline constexpr std::array<std::uint8_t,48> kStateBitsEntry={
    0x00,0x01,0x02,0x01,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x04,0x04,0x04,0xff,0xff,0xff,0xff,0x05,0xff,0x03,0x03};
inline constexpr std::array<std::uint64_t,6> kStateBits={0xe49e490d00128812ull,0x0000000d18128812ull,0x0000003d00128812ull,
    0x0000000d58128812ull,0xe004004858128922ull,0xe49e492c88128812ull};
inline constexpr std::uint8_t kGameFlags=0x50,kSortOpaque=1,kStateFlags=0x79,kSamplerDefault=0x0b,kSamplerIdentity=0x01;
inline constexpr std::uint32_t kSurfaceFlesh=1u<<6;
} // namespace mw2

// Replays MW2's stream allocator while the bytes are written.
class ZoneStream {
public:
    std::vector<std::uint8_t> bytes;
    std::array<std::uint64_t,mw2::BlockCount> peak{};
    void push(int block) {stack_.push_back({current_,0});current_=block;stack_.back().pos=pos_[static_cast<std::size_t>(block)];}
    void pop() {
        const auto saved=stack_.back();stack_.pop_back();
        if(current_==mw2::Temp) pos_[mw2::Temp]=saved.pos;
        current_=saved.block;
    }
    void align(std::uint64_t mask) {auto& p=pos_[static_cast<std::size_t>(current_)];p=(p+mask)&~mask;}
    // Load_Stream(atStreamStart=true): the bytes land at the current position.
    void read(const void* data,std::size_t size) {
        const auto* b=static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(),b,b+size);
        auto& p=pos_[static_cast<std::size_t>(current_)];p+=size;
        peak[static_cast<std::size_t>(current_)]=std::max(peak[static_cast<std::size_t>(current_)],p);
    }
    void read(const std::vector<std::uint8_t>& data) {if(!data.empty())read(data.data(),data.size());}
    void string(const std::string& s) {align(0);read(s.c_str(),s.size()+1);}
    int block() const {return current_;}
    std::size_t depth() const {return stack_.size();}
private:
    struct Saved {int block;std::uint64_t pos;};
    std::vector<Saved> stack_;
    std::array<std::uint64_t,mw2::BlockCount> pos_{};
    int current_=mw2::Temp;
};

struct MaterialSpec {
    std::string name;
    std::string color,normal,spec;               // image asset names (',' = existing MW2 image)
    std::array<float,4> envMapParms{0.f,1.5f,0.05f,2.f};
};
struct ModelSpec {
    std::string name;
    const Model* source=nullptr;
    std::vector<std::size_t> surfaceMaterial;    // index into the zone's materials, per surface
};
struct ZoneSpec {
    std::vector<MaterialSpec> materials;
    std::map<std::string,Texture> images;        // name -> texels (written once, then referenced)
    std::vector<ModelSpec> models;
};
struct ZoneResult {
    std::vector<std::uint8_t> stream;            // decompressed: XFile header onward
    std::array<std::uint32_t,mw2::BlockCount> blockSize{};
    std::size_t scriptStrings=0,assets=0,imageBytes=0;
};

namespace detail {
inline void Zero(std::uint8_t* p,std::size_t n) {std::memset(p,0,n);}
inline std::uint32_t BitsOfBone(std::uint32_t bone) {return 0x80000000u>>(bone&31u);}

class ZoneBuilder {
public:
    ZoneBuilder(const ZoneSpec& spec,ZoneStream& s):spec_(spec),s_(s) {}
    std::vector<std::string> strings{std::string()};   // index 0 is the null entry
    std::uint16_t intern(const std::string& text) {
        const auto found=index_.find(text);
        if(found!=index_.end()) return found->second;
        const auto id=static_cast<std::uint16_t>(strings.size());
        strings.push_back(text);index_.emplace(text,id);return id;
    }
    void techniqueSet(const std::string& name) {     // ',' reference to a loaded MW2 technique set
        s_.push(mw2::Temp);s_.align(3);
        std::uint8_t h[mw2::kTechniqueSetBytes]{};Put<std::uint64_t>(h,mw2::kInline);
        s_.read(h,sizeof(h));
        s_.push(mw2::Virtual);s_.string(','+name);s_.pop();
        s_.pop();
    }
    void image(const std::string& name) {
        s_.push(mw2::Temp);s_.align(3);
        std::uint8_t h[mw2::kImageBytes]{};
        const bool reference=name[0]==',' || written_.count(name);
        const Texture* t=nullptr;
        if(!reference) {
            const auto found=spec_.images.find(name);
            t=found==spec_.images.end()?nullptr:&found->second;
        }
        Put<std::uint64_t>(h+0x20,mw2::kInline);
        if(t) {
            Put<std::uint64_t>(h,mw2::kInline);
            h[0x8]=3;                                 // MAPTYPE_2D
            h[0x9]=semantic_;h[0xa]=3;h[0xe]=1;      // category: load-from-file family; no picmip
            Put<std::uint32_t>(h+0x10,static_cast<std::uint32_t>(t->data.size()));
            Put<std::uint32_t>(h+0x14,static_cast<std::uint32_t>(t->data.size()));
            Put<std::uint16_t>(h+0x18,t->width);Put<std::uint16_t>(h+0x1a,t->height);Put<std::uint16_t>(h+0x1c,1);
        }
        s_.read(h,sizeof(h));
        s_.push(mw2::Virtual);
        s_.string(reference && name[0]!=','?','+name:name);
        s_.push(mw2::Temp);
        if(t) {
            s_.align(3);
            std::uint8_t l[mw2::kLoadDefBytes]{};
            l[0]=t->levels;Put<std::int32_t>(l+4,t->levels>1?0:2);
            Put<std::uint32_t>(l+8,t->format);Put<std::uint32_t>(l+12,static_cast<std::uint32_t>(t->data.size()));
            s_.read(l,sizeof(l));s_.read(t->data);
            imageBytes+=t->data.size();
            written_.insert(name);
        }
        s_.pop();
        s_.pop();
        s_.pop();
    }
    void material(std::size_t index) {
        const auto& m=spec_.materials[index];
        s_.push(mw2::Temp);s_.align(3);
        std::uint8_t h[mw2::kMaterialBytes]{};
        Put<std::uint64_t>(h,mw2::kInline);
        const bool reference=materialsWritten_[index];
        if(!reference) {
            h[0x8]=mw2::kGameFlags;h[0x9]=mw2::kSortOpaque;h[0xa]=1;h[0xb]=1;
            Put<std::uint32_t>(h+0x18,mw2::kSurfaceFlesh);
            std::memcpy(h+0x20,mw2::kStateBitsEntry.data(),48);
            h[0x50]=3;h[0x51]=2;h[0x52]=static_cast<std::uint8_t>(mw2::kStateBits.size());h[0x53]=mw2::kStateFlags;h[0x54]=0;
            for(std::size_t o:{0x58,0x60,0x68,0x70}) Put<std::uint64_t>(h+o,mw2::kInline);
        }
        s_.read(h,sizeof(h));
        s_.push(mw2::Virtual);
        s_.string(reference?','+m.name:m.name);
        if(!reference) {
            techniqueSet(mw2::kTechniqueSet);
            // Texture table, sorted by sampler name hash like MW2's materials.
            struct Def {std::uint32_t hash;std::uint8_t start,end,semantic;const std::string* image;};
            const Def defs[3]={{kHashSpecularMap,'s','p',8,&m.spec},{kHashNormalMap,'n','p',5,&m.normal},{kHashColorMap,'c','p',2,&m.color}};
            s_.align(3);
            for(const auto& d:defs) {
                std::uint8_t t[mw2::kTextureDefBytes]{};
                Put<std::uint32_t>(t,d.hash);t[4]=d.start;t[5]=d.end;t[7]=d.semantic;
                // MW2 samples its flat $identitynormalmap with state 0x01, its own maps with 0x0b.
                t[6]=*d.image==mw2::kIdentityNormal?mw2::kSamplerIdentity:mw2::kSamplerDefault;
                Put<std::uint64_t>(t+8,mw2::kInline);
                s_.read(t,sizeof(t));
            }
            for(const auto& d:defs) {semantic_=d.semantic;image(*d.image);}
            s_.align(15);
            std::uint8_t c[2*mw2::kConstantBytes]{};
            Put<std::uint32_t>(c,mw2::kHashEnvMapParms);std::memcpy(c+4,"envMapParms",11);
            for(std::size_t n=0;n<4;++n) Put<float>(c+16+4*n,m.envMapParms[n]);
            Put<std::uint32_t>(c+32,mw2::kHashColorTint);std::memcpy(c+36,"colorTint",9);
            for(std::size_t n=0;n<4;++n) Put<float>(c+48+4*n,1.f);
            s_.read(c,sizeof(c));
            s_.align(3);
            for(auto bits:mw2::kStateBits) s_.read(&bits,8);
            materialsWritten_[index]=true;
        }
        s_.pop();
        s_.pop();
    }
    void surfaces(const ModelSpec& spec,std::size_t lod) {
        const auto& src=*spec.source;const auto& range=src.lods[lod];
        s_.push(mw2::Temp);s_.align(3);
        std::uint8_t h[mw2::kSurfsBytes]{};
        Put<std::uint64_t>(h,mw2::kInline);Put<std::uint64_t>(h+8,mw2::kInline);
        Put<std::uint16_t>(h+0x10,range.numsurfs);
        std::array<std::uint32_t,6> lodBits{};
        for(std::size_t n=0;n<range.numsurfs;++n) for(std::size_t k=0;k<5;++k) lodBits[k]|=src.surfs[range.surfIndex+n].partBits[k];
        for(std::size_t k=0;k<6;++k) Put<std::uint32_t>(h+0x14+4*k,lodBits[k]);
        s_.read(h,sizeof(h));
        s_.push(mw2::Virtual);
        s_.string(spec.name+"_lod"+std::to_string(lod));
        s_.align(3);
        for(std::size_t n=0;n<range.numsurfs;++n) {
            const auto& sf=src.surfs[range.surfIndex+n];
            std::uint8_t b[mw2::kSurfaceBytes]{};
            b[0]=sf.tileMode;b[1]=sf.skinned()?1:0;
            Put<std::uint16_t>(b+2,sf.vertCount);Put<std::uint16_t>(b+4,sf.triCount);
            Put<std::uint64_t>(b+0x10,mw2::kInline);
            for(std::size_t k=0;k<4;++k) Put<std::int16_t>(b+0x18+2*k,sf.blendCounts[k]);
            if(sf.skinned()) Put<std::uint64_t>(b+0x20,mw2::kInline);
            Put<std::uint64_t>(b+0x28,mw2::kInline);
            Put<std::uint32_t>(b+0x30,static_cast<std::uint32_t>(sf.rigid.size()));
            if(!sf.rigid.empty()) Put<std::uint64_t>(b+0x38,mw2::kInline);
            for(std::size_t k=0;k<5;++k) Put<std::uint32_t>(b+0x40+4*k,sf.partBits[k]);
            s_.read(b,sizeof(b));
        }
        for(std::size_t n=0;n<range.numsurfs;++n) {
            const auto& sf=src.surfs[range.surfIndex+n];
            if(sf.skinned()) {s_.align(1);s_.read(sf.vertsBlend);}
            s_.push(mw2::Vertex);s_.align(15);s_.read(sf.verts);s_.pop();
            if(!sf.rigid.empty()) {
                s_.align(3);
                for(const auto& l:sf.rigid) {
                    std::uint8_t e[mw2::kRigidBytes]{};
                    Put<std::uint16_t>(e,l.boneOffset);Put<std::uint16_t>(e+2,l.vertCount);
                    Put<std::uint16_t>(e+4,l.triOffset);Put<std::uint16_t>(e+6,l.triCount);
                    if(l.tree) Put<std::uint64_t>(e+8,mw2::kInline);
                    s_.read(e,sizeof(e));
                }
                for(const auto& l:sf.rigid) {
                    if(!l.tree) continue;
                    s_.align(3);
                    std::uint8_t t[mw2::kTreeBytes]{};
                    std::memcpy(t,l.transScale.data(),24);
                    Put<std::uint32_t>(t+0x18,l.nodeCount);if(l.nodeCount) Put<std::uint64_t>(t+0x20,mw2::kInline);
                    Put<std::uint32_t>(t+0x28,l.leafCount);if(l.leafCount) Put<std::uint64_t>(t+0x30,mw2::kInline);
                    s_.read(t,sizeof(t));
                    if(l.nodeCount){s_.align(15);s_.read(l.nodes);}
                    if(l.leafCount){s_.align(1);s_.read(l.leafs);}
                }
            }
            s_.push(mw2::Index);s_.align(15);s_.read(sf.tris);s_.pop();
        }
        s_.pop();
        s_.pop();
    }
    void model(const ModelSpec& spec) {
        const auto& m=*spec.source;
        const std::size_t nb=m.numBones,nc=static_cast<std::size_t>(m.numBones-m.numRootBones);
        s_.push(mw2::Temp);s_.align(3);
        std::uint8_t h[mw2::kModelBytes]{};
        Put<std::uint64_t>(h,mw2::kInline);
        h[0x8]=m.numBones;h[0x9]=m.numRootBones;h[0xa]=m.numsurfs;h[0xb]=m.lodRampType;
        Put<float>(h+0xc,1.f);
        Put<std::uint64_t>(h+0x28,mw2::kInline);
        if(nc) {Put<std::uint64_t>(h+0x30,mw2::kInline);Put<std::uint64_t>(h+0x38,mw2::kInline);Put<std::uint64_t>(h+0x40,mw2::kInline);}
        Put<std::uint64_t>(h+0x48,mw2::kInline);Put<std::uint64_t>(h+0x50,mw2::kInline);Put<std::uint64_t>(h+0x58,mw2::kInline);
        for(std::size_t n=0;n<4;++n) {
            auto* l=h+0x60+mw2::kLodBytes*n;
            if(n<m.numLods) {
                Put<float>(l,m.lods[n].dist);Put<std::uint16_t>(l+4,m.lods[n].numsurfs);Put<std::uint16_t>(l+6,m.lods[n].surfIndex);
                Put<std::uint64_t>(l+8,mw2::kInline);
                l[0x30]=static_cast<std::uint8_t>(n);    // XModelLodInfo::lod (MW2 zones carry the index)
            } else Put<float>(l,1000000.f);
        }
        h[0x140]=0;h[0x141]=static_cast<std::uint8_t>(m.numLods);h[0x142]=static_cast<std::uint8_t>(m.collLod);h[0x143]=0;
        if(!m.collSurfs.empty()) Put<std::uint64_t>(h+0x148,mw2::kInline);
        Put<std::int32_t>(h+0x150,static_cast<std::int32_t>(m.collSurfs.size()));Put<std::int32_t>(h+0x154,m.contents);
        if(!m.boneInfo.empty()) Put<std::uint64_t>(h+0x158,mw2::kInline);
        Put<float>(h+0x160,m.radius);
        for(std::size_t k=0;k<3;++k) {
            Put<float>(h+0x164+4*k,(m.mins[k]+m.maxs[k])*.5f);
            Put<float>(h+0x170+4*k,(m.maxs[k]-m.mins[k])*.5f);
        }
        std::size_t memory=mw2::kModelBytes;
        for(const auto& sf:m.surfs) memory+=sf.verts.size()+sf.tris.size()+sf.vertsBlend.size()+mw2::kSurfaceBytes;
        Put<std::int32_t>(h+0x17c,static_cast<std::int32_t>(std::min<std::size_t>(memory,0x7fffffff)));
        s_.read(h,sizeof(h));
        s_.push(mw2::Virtual);
        s_.string(spec.name);
        s_.align(1);
        for(const auto& bone:m.bones) {const auto id=intern(bone);s_.read(&id,2);}
        if(nc) {
            s_.align(0);s_.read(m.parentList);
            s_.align(1);s_.read(m.quats);
            s_.align(3);s_.read(m.trans);
        }
        s_.align(0);s_.read(m.partClassification);
        s_.align(3);s_.read(m.baseMat);
        s_.align(3);
        for(std::size_t n=0;n<m.numsurfs;++n) {const auto p=mw2::kInline;s_.read(&p,8);}
        for(std::size_t n=0;n<m.numsurfs;++n) material(spec.surfaceMaterial[n]);
        for(std::size_t n=0;n<4;++n) {
            if(n<m.numLods) surfaces(spec,n);
            else {s_.push(mw2::Temp);s_.pop();}
        }
        if(!m.collSurfs.empty()) {
            s_.align(3);
            for(const auto& cs:m.collSurfs) {
                std::uint8_t c[mw2::kCollSurfBytes]{};
                if(!cs.tris.empty()) Put<std::uint64_t>(c,mw2::kInline);
                Put<std::int32_t>(c+8,static_cast<std::int32_t>(cs.tris.size()/t5::kCollTriBytes));
                for(std::size_t k=0;k<3;++k){Put<float>(c+12+4*k,(cs.mins[k]+cs.maxs[k])*.5f);Put<float>(c+24+4*k,(cs.maxs[k]-cs.mins[k])*.5f);}
                Put<std::int32_t>(c+36,cs.boneIdx);Put<std::int32_t>(c+40,cs.contents);Put<std::int32_t>(c+44,cs.surfFlags);
                s_.read(c,sizeof(c));
            }
            for(const auto& cs:m.collSurfs) if(!cs.tris.empty()) {s_.align(3);s_.read(cs.tris);}
        }
        if(!m.boneInfo.empty()) {
            s_.align(3);
            for(std::size_t n=0;n<nb;++n) {
                const auto* t=m.boneInfo.data()+t5::kBoneInfoBytes*n;
                std::uint8_t b[mw2::kBoneInfoBytes]{};
                for(std::size_t k=0;k<3;++k) {
                    const float lo=Get<float>(t+4*k),hi=Get<float>(t+12+4*k);
                    Put<float>(b+4*k,(lo+hi)*.5f);Put<float>(b+12+4*k,(hi-lo)*.5f);
                }
                Put<float>(b+24,Get<float>(t+36));
                s_.read(b,sizeof(b));
            }
        }
        s_.push(mw2::Temp);s_.pop();     // physPreset (null)
        s_.push(mw2::Temp);s_.pop();     // physCollmap (null)
        s_.pop();
        s_.pop();
    }
    std::size_t imageBytes=0;
private:
    const ZoneSpec& spec_;
    ZoneStream& s_;
    std::map<std::string,std::uint16_t> index_;
    std::set<std::string> written_;
    std::map<std::size_t,bool> materialsWritten_;
    std::uint8_t semantic_=0;
};
} // namespace detail

// The decompressed MW2 zone: XFile header, XAssetList, script strings, assets.
inline bool BuildZone(const ZoneSpec& spec,ZoneResult& out,std::string* error=nullptr) {
    using detail::Fail;
    for(const auto& m:spec.models) {
        if(!m.source || m.surfaceMaterial.size()!=m.source->numsurfs) return Fail(error,"model "+m.name+" is incomplete");
        for(auto i:m.surfaceMaterial) if(i>=spec.materials.size()) return Fail(error,"model "+m.name+" names a missing material");
        if(m.source->numBones>128) return Fail(error,"model "+m.name+" has too many bones");
        for(const auto& sf:m.source->surfs) for(const auto& l:sf.rigid) if(l.boneOffset%64 || l.boneOffset/64>=m.source->numBones)
            return Fail(error,"model "+m.name+" has a bad rigid bone");
    }
    for(const auto& mat:spec.materials) for(const auto* image:{&mat.color,&mat.normal,&mat.spec})
        if(image->empty() || ((*image)[0]!=',' && !spec.images.count(*image))) return Fail(error,"material "+mat.name+" lacks an image");
    // Pass 1 collects the script strings (bone names) so the list can precede the assets.
    std::vector<std::string> strings;
    {
        ZoneStream scratch;detail::ZoneBuilder b(spec,scratch);
        for(const auto& m:spec.models) b.model(m);
        strings=b.strings;
    }
    ZoneStream s;
    detail::ZoneBuilder b(spec,s);
    for(std::size_t n=1;n<strings.size();++n) b.intern(strings[n]);
    // XAssetList (read directly, outside the blocks).
    std::uint8_t list[mw2::kAssetListBytes]{};
    Put<std::uint32_t>(list,static_cast<std::uint32_t>(strings.size()));Put<std::uint64_t>(list+8,mw2::kInline);
    Put<std::uint32_t>(list+16,static_cast<std::uint32_t>(spec.models.size()));Put<std::uint64_t>(list+24,mw2::kInline);
    s.bytes.insert(s.bytes.end(),list,list+sizeof(list));
    s.push(mw2::Virtual);                          // Load_XAssetList
    s.push(mw2::Virtual);                          // Load_ScriptStringList
    s.align(3);
    for(std::size_t n=0;n<strings.size();++n) {const std::uint64_t p=n?mw2::kInline:0;s.read(&p,8);}
    for(std::size_t n=1;n<strings.size();++n) s.string(strings[n]);
    s.pop();
    s.pop();
    s.push(mw2::Virtual);
    s.align(3);
    for(std::size_t n=0;n<spec.models.size();++n) {
        std::uint8_t a[mw2::kAssetBytes]{};Put<std::int32_t>(a,mw2::kAssetXModel);Put<std::uint64_t>(a+8,mw2::kInline);
        s.read(a,sizeof(a));
    }
    for(const auto& m:spec.models) b.model(m);
    s.pop();
    if(s.depth()!=0 || b.strings.size()!=strings.size()) return Fail(error,"zone stream unbalanced");
    // XFile: size of what follows, external size, per-block sizes (+1 page of slack each).
    out.blockSize.fill(0);
    for(std::size_t n=0;n<mw2::BlockCount;++n) if(s.peak[n]) out.blockSize[n]=static_cast<std::uint32_t>(((s.peak[n]+15)&~15ull)+4096);
    out.stream.assign(mw2::kXFileBytes,0);
    Put<std::uint32_t>(out.stream.data(),static_cast<std::uint32_t>(s.bytes.size()));
    for(std::size_t n=0;n<mw2::BlockCount;++n) Put<std::uint32_t>(out.stream.data()+8+4*n,out.blockSize[n]);
    out.stream.insert(out.stream.end(),s.bytes.begin(),s.bytes.end());
    out.scriptStrings=strings.size();out.assets=spec.models.size();out.imageBytes=b.imageBytes;
    return true;
}

// zlib (stored blocks) with an adler32 trailer: what inflateInit_ (zlib 1.1.4) reads.
inline std::vector<std::uint8_t> ZlibStore(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out{0x78,0x01};
    std::size_t at=0;
    do {
        const std::size_t n=std::min<std::size_t>(65535,data.size()-at);
        const bool last=at+n==data.size();
        out.push_back(last?1:0);
        out.push_back(static_cast<std::uint8_t>(n));out.push_back(static_cast<std::uint8_t>(n>>8));
        out.push_back(static_cast<std::uint8_t>(~n));out.push_back(static_cast<std::uint8_t>((~n)>>8));
        out.insert(out.end(),data.begin()+static_cast<std::ptrdiff_t>(at),data.begin()+static_cast<std::ptrdiff_t>(at+n));
        at+=n;
    } while(at<data.size());
    std::uint32_t a=1,b=0;
    for(auto c:data){a=(a+c)%65521u;b=(b+a)%65521u;}
    const std::uint32_t adler=(b<<16)|a;
    for(int shift=24;shift>=0;shift-=8) out.push_back(static_cast<std::uint8_t>(adler>>shift));
    return out;
}
// The fastfile: "IWffu100", version 276, allowOnlineUpdate, file time, zlib stream.
inline std::vector<std::uint8_t> FastFile(const ZoneResult& zone) {
    std::vector<std::uint8_t> out={'I','W','f','f','u','1','0','0'};
    out.resize(21,0);Put<std::uint32_t>(out.data()+8,mw2::kVersion);
    const auto z=ZlibStore(zone.stream);
    out.insert(out.end(),z.begin(),z.end());
    return out;
}

// The full Test40 zone for one carved character and its textures.
// `textures` holds: headColor, headNormal, headSpec (+gloss), eyeColor,
// bodyColor, bodyNormal, bodySpec, keyed by the Character's image names.
inline constexpr std::array<const char*,2> kBodyModels={"c_ger_honorguard_body1","c_ger_honorguard_body2"};
inline constexpr std::array<const char*,4> kHeadModels={"c_ger_zombie_head1","c_ger_zombie_head2","c_ger_zombie_head3","c_ger_zombie_head4"};
inline constexpr const char* kImagePrefix="bo1_";
// Missing textures never stop the models: a colour map falls back to plain
// grey, a normal map to MW2's flat $identitynormalmap and a specular map to a
// dull constant one (each reported in `fallbacks`).
inline constexpr const char* kFallbackSpecular="bo1_fallback_specular";
inline constexpr const char* kFallbackColor="bo1_fallback_color";
inline bool CharacterSpec(const Character& c,const std::map<std::string,Texture>& textures,ZoneSpec& spec,std::string* error=nullptr,
                          std::vector<std::string>* fallbacks=nullptr) {
    spec=ZoneSpec{};
    if(error) error->clear();
    enum class Kind {Color,Normal,Specular};
    auto take=[&](const std::string& source,const char* role,Kind kind,std::string& name)->bool {
        const auto found=source.empty()?textures.end():textures.find(source);
        if(found==textures.end()) {
            if(fallbacks) fallbacks->push_back(std::string(role)+":"+source);
            if(kind==Kind::Color) {name=kFallbackColor;spec.images[name]=SolidDxt5(4,0x8410,0xff);}
            else if(kind==Kind::Normal) name=mw2::kIdentityNormal;
            else {name=kFallbackSpecular;spec.images[name]=SolidDxt5(4,0x2104,0x30);}
            return true;
        }
        name=kImagePrefix+source;
        for(auto& ch:name) if(ch=='~' || ch=='&' || ch=='-') ch='_';
        spec.images[name]=found->second;return true;
    };
    MaterialSpec body,head,eyes;
    body.name="mc/bo1_c_ger_honorguard_body1";head.name="mc/bo1_c_ger_zombie_head1";eyes.name="mc/bo1_c_ger_zombie_eyes";
    if(!take(c.bodyColor,"body colour",Kind::Color,body.color) || !take(c.bodyNormal,"body normal",Kind::Normal,body.normal)
        || !take(c.bodySpec,"body specular",Kind::Specular,body.spec) || !take(c.headColor,"head colour",Kind::Color,head.color)
        || !take(c.headNormal,"head normal",Kind::Normal,head.normal) || !take(c.headSpec,"head specular",Kind::Specular,head.spec)
        || !take(c.eyeColor,"eye colour",Kind::Color,eyes.color)) return false;
    eyes.normal=mw2::kIdentityNormal;
    eyes.spec="bo1_zombie_eye_specular";spec.images[eyes.spec]=SolidDxt5(4,0x4208,0x40);
    head.envMapParms={0.f,0.4f,0.05f,2.f};eyes.envMapParms={0.2f,1.f,0.2f,2.f};body.envMapParms={0.f,0.6f,0.05f,2.f};
    spec.materials={body,head,eyes};
    auto add=[&](const char* name,const Model& m) {
        ModelSpec ms{name,&m,{}};
        for(std::size_t n=0;n<m.numsurfs;++n) {
            switch(c.skinOf(m,n)) {case Skin::Body:ms.surfaceMaterial.push_back(0);break;
                case Skin::HeadSkin:ms.surfaceMaterial.push_back(1);break;case Skin::Eyes:ms.surfaceMaterial.push_back(2);break;}
        }
        spec.models.push_back(std::move(ms));
    };
    add(kBodyModels[0],c.body1);add(kBodyModels[1],c.body2);
    for(std::size_t n=0;n<4;++n) add(kHeadModels[n],c.heads[n]);
    return true;
}
} // namespace mod::bo1model
