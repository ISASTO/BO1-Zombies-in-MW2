#pragma once
// A small synthetic BO1-style character used by the Test40 model tests and the
// exact-native loader probe: four bones, a skinned and a rigid surface (with a
// collision tree) in LOD0, a skinned surface in LOD1, one collision surface,
// and two materials, one of them reused (so the zone carries references).
#include "bo1_model.hpp"

namespace mod::bo1model::synthetic {
inline std::vector<std::uint8_t> Bytes(std::size_t n,std::uint8_t seed) {
    std::vector<std::uint8_t> v(n);for(std::size_t i=0;i<n;++i)v[i]=static_cast<std::uint8_t>(seed+i*7);return v;
}
inline std::vector<std::uint8_t> Tris(std::initializer_list<std::uint16_t> idx) {
    std::vector<std::uint8_t> v;for(auto i:idx){v.push_back(static_cast<std::uint8_t>(i));v.push_back(static_cast<std::uint8_t>(i>>8));}return v;
}
inline Surface Skinned(std::array<std::int16_t,4> counts,std::uint8_t seed,std::initializer_list<std::uint16_t> tris,std::uint32_t bits) {
    Surface s;s.blendCounts=counts;s.vertCount=static_cast<std::uint16_t>(counts[0]+counts[1]+counts[2]+counts[3]);
    s.flags=0x82;s.tris=Tris(tris);s.triCount=static_cast<std::uint16_t>(s.tris.size()/6);s.verts=Bytes(32u*s.vertCount,seed);
    std::vector<std::uint16_t> blend;
    for(int n=0;n<counts[0];++n) blend.push_back(64);
    for(int n=0;n<counts[1];++n){blend.push_back(128);blend.push_back(192);blend.push_back(30000);}
    for(int n=0;n<counts[2];++n){blend.push_back(64);blend.push_back(128);blend.push_back(20000);blend.push_back(192);blend.push_back(10000);}
    for(int n=0;n<counts[3];++n){blend.push_back(0);blend.push_back(64);blend.push_back(9000);blend.push_back(128);blend.push_back(8000);blend.push_back(192);blend.push_back(7000);}
    for(auto b:blend){s.vertsBlend.push_back(static_cast<std::uint8_t>(b));s.vertsBlend.push_back(static_cast<std::uint8_t>(b>>8));}
    s.partBits={bits,0,0,0,0};
    return s;
}
inline Model Character(const std::vector<std::string>& bones,std::uint8_t seed) {
    Model m;m.numBones=static_cast<std::uint8_t>(bones.size());m.numRootBones=1;m.lodRampType=1;m.bones=bones;
    const std::size_t nc=bones.size()-1;
    m.parentList.assign(nc,1);m.quats=Bytes(8*nc,seed);m.trans=Bytes(12*nc,static_cast<std::uint8_t>(seed+1));
    m.partClassification=Bytes(bones.size(),2);m.baseMat=Bytes(32*bones.size(),static_cast<std::uint8_t>(seed+2));
    m.surfs.push_back(Skinned({2,1,1,0},seed,{0,1,2,2,3,0},0xf0000000u));
    Surface rigid;rigid.vertCount=3;rigid.triCount=1;rigid.tris=Tris({0,1,2});rigid.verts=Bytes(96,static_cast<std::uint8_t>(seed+3));
    rigid.partBits={0x20000000u,0,0,0,0};
    RigidList l;l.boneOffset=128;l.vertCount=3;l.triOffset=0;l.triCount=1;l.tree=true;l.nodeCount=2;l.leafCount=3;
    l.transScale[0]=1;l.nodes=Bytes(32,9);l.leafs=Bytes(6,4);rigid.rigid.push_back(l);
    m.surfs.push_back(rigid);
    m.surfs.push_back(Skinned({3,0,0,0},static_cast<std::uint8_t>(seed+4),{0,1,2},0x40000000u));
    m.numsurfs=3;m.numLods=2;m.lods[0]={300.f,2,0};m.lods[1]={900.f,1,2};
    m.materials.resize(3);
    for(auto& r:m.materials) r.kind=MaterialRef::Kind::Alias;
    CollSurf cs;cs.tris=Bytes(96,11);cs.mins={-1,-2,-3};cs.maxs={1,2,3};cs.boneIdx=2;cs.contents=0x2000000;cs.surfFlags=4;
    m.collSurfs.push_back(cs);
    m.boneInfo.assign(t5::kBoneInfoBytes*bones.size(),0);
    for(std::size_t n=0;n<bones.size();++n) {
        auto* b=m.boneInfo.data()+t5::kBoneInfoBytes*n;
        const float lo[3]={-1.f,-2.f,-3.f},hi[3]={3.f,2.f,1.f+static_cast<float>(n)};
        for(int k=0;k<3;++k){Put<float>(b+4*k,lo[k]);Put<float>(b+12+4*k,hi[k]);Put<float>(b+24+4*k,(lo[k]+hi[k])/2);}
        Put<float>(b+36,9.f+static_cast<float>(n));
    }
    m.radius=20.f;m.mins={-5,-6,-7};m.maxs={5,6,7};
    return m;
}
inline Texture Dxt1(std::uint16_t w,std::uint16_t h,std::uint8_t seed) {
    Texture t;t.width=w;t.height=h;t.format=kD3dDxt1;t.levels=static_cast<std::uint8_t>(MipCount(w,h));
    for(unsigned level=0;level<t.levels;++level){const auto n=MipBytes(kD3dDxt1,w>>level,h>>level);const auto b=Bytes(n,static_cast<std::uint8_t>(seed+level));t.data.insert(t.data.end(),b.begin(),b.end());}
    return t;
}
// Two models (sharing materials) with three materials and five images.
struct Scene {Model a,b;ZoneSpec spec;};
inline void Build(Scene& s) {
    s.a=Character({"tag_origin","j_mainroot","j_spine4","j_head"},10);
    s.b=Character({"tag_origin","j_mainroot","j_spine4","j_neck","j_head"},50);
    s.spec=ZoneSpec{};
    s.spec.images["bo1_test_c"]=Dxt1(16,8,1);
    s.spec.images["bo1_test_n"]=SolidDxt5(8,0x7bef,0x80);
    s.spec.images["bo1_test_s"]=SolidDxt5(4,0x4208,0x40);
    s.spec.images["bo1_test_skin_c"]=Dxt1(8,8,3);
    MaterialSpec body{"mc/bo1_test_body","bo1_test_c","bo1_test_n","bo1_test_s",{0.f,1.f,0.1f,2.f}};
    MaterialSpec skin{"mc/bo1_test_skin","bo1_test_skin_c",mw2::kIdentityNormal,"bo1_test_s",{0.f,0.5f,0.1f,2.f}};
    s.spec.materials={body,skin};
    s.spec.models.push_back({"bo1_test_body_a",&s.a,{0,1,0}});
    s.spec.models.push_back({"bo1_test_body_b",&s.b,{1,1,0}});
}
} // namespace mod::bo1model::synthetic
