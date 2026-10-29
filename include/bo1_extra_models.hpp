#pragma once
// Test45: more Black Ops models for the zombie zone, from the owner's own
// fastfiles, next to the honorguard character (bo1_model.hpp):
//
//  * the powerup models of common_zombie.ff (zombie_ammocan, zombie_x2_icon,
//    zombie_bomb, zombie_skull) with their gold and ammo-box materials;
//  * the honorguard zombie's dismemberment models in zombie_theater.ff: the
//    upper bodies without an arm (or with the guts out), the legs without a
//    leg (or without both), the neck stump left when the head pops, and the
//    four limb pieces thrown off (they carry physics data MW2 does not get).
//
// The dismemberment models follow the character in the zone, with the same
// 73-bone skeleton. Each is recognised by which bones its vertices use (an
// arm torn off at the elbow uses no j_elbow_ri), never by position, and every
// material slot is classified: the character's body sheet and head skin, the
// gore cap of a torn limb ("gore_limbs"), or a blood decal ("gore_blend",
// "gore_punch"), which is dropped (MW2's opaque model material cannot blend
// it). Anything unexpected leaves the dismemberment out; the character and
// the powerups do not depend on it.
//
// Header-only and free of engine state so the native tests can exercise it.
#include "bo1_model.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mod::bo1model {
// The image named by a material's sampler, with a reference's leading ','
// removed (the pixels are in the .iwd archives either way).
inline std::string ImageByHash(const MaterialRef& m,std::uint32_t hash) {
    for(const auto& t:m.textures) if(t.nameHash==hash && !t.name.empty()) return t.name[0]==','?t.name.substr(1):t.name;
    return {};
}
// Bones that vertices of the first LOD are bound to.
inline std::set<std::string> UsedBones(const Model& m) {
    std::set<std::string> used;
    auto bone=[&](std::uint16_t offset){if(offset/64u<m.bones.size()) used.insert(m.bones[offset/64u]);};
    for(std::size_t s=0;s<m.surfs.size() && s<m.lods[0].numsurfs;++s) {
        const auto& sf=m.surfs[s];
        for(const auto& l:sf.rigid) bone(l.boneOffset);
        if(!sf.skinned()) continue;
        std::size_t i=0;const std::size_t shorts=sf.vertsBlend.size()/2;
        auto at=[&](std::size_t k){return Get<std::uint16_t>(sf.vertsBlend.data()+2*k);};
        for(int k=0;k<sf.blendCounts[0] && i+1<=shorts;++k){bone(at(i));i+=1;}
        for(int k=0;k<sf.blendCounts[1] && i+3<=shorts;++k){bone(at(i));bone(at(i+1));i+=3;}
        for(int k=0;k<sf.blendCounts[2] && i+5<=shorts;++k){bone(at(i));bone(at(i+1));bone(at(i+3));i+=5;}
        for(int k=0;k<sf.blendCounts[3] && i+7<=shorts;++k){bone(at(i));bone(at(i+1));bone(at(i+3));bone(at(i+5));i+=7;}
    }
    return used;
}
// A copy of `in` without the surfaces marked in `drop` (every LOD keeps at
// least one surface).
inline bool WithoutSurfaces(const Model& in,const std::vector<bool>& drop,Model& out,std::string* error=nullptr) {
    if(drop.size()!=in.surfs.size() || in.materials.size()!=in.surfs.size()) return detail::Fail(error,"surface list mismatch");
    out=in;out.surfs.clear();out.materials.clear();
    std::size_t next=0;
    for(std::size_t lod=0;lod<in.numLods;++lod) {
        const auto& range=in.lods[lod];
        std::size_t kept=0;
        for(std::size_t n=range.surfIndex;n<static_cast<std::size_t>(range.surfIndex)+range.numsurfs;++n) {
            if(drop[n]) continue;
            out.surfs.push_back(in.surfs[n]);out.materials.push_back(in.materials[n]);++kept;
        }
        if(!kept) return detail::Fail(error,"a LOD would have no surface");
        out.lods[lod].surfIndex=static_cast<std::uint16_t>(next);out.lods[lod].numsurfs=static_cast<std::uint16_t>(kept);
        next+=kept;
    }
    if(next>255) return detail::Fail(error,"too many surfaces");
    out.numsurfs=static_cast<std::uint8_t>(next);
    return true;
}

// ------------------------------------------------------------- powerups
inline constexpr std::array<const char*,4> kPowerupModels={"zombie_ammocan","zombie_x2_icon","zombie_bomb","zombie_skull"};
inline constexpr const char* kGoldMaterial="mc/mtl_x2icon_gold";
inline constexpr const char* kAmmoMaterial="mc/mtl_zombie_ammocan";
struct PowerupSet {
    std::array<Model,kPowerupModels.size()> models;   // in kPowerupModels order
    MaterialRef gold,ammo;
    std::uint32_t goldAlias=0;
    // per model, per surface: 0 gold, 1 ammo box
    std::array<std::vector<std::size_t>,kPowerupModels.size()> surfaceMaterial;
};
// The four models are found by their inline names (the 252-byte header
// precedes the name).
inline std::optional<PowerupSet> CarvePowerups(const std::uint8_t* raw,std::size_t size,const std::vector<std::string>& strings,
                                               std::string* error=nullptr) {
    auto fail=[&](const std::string& text){if(error)*error=text;return std::optional<PowerupSet>{};};
    PowerupSet p;std::array<bool,kPowerupModels.size()> found{};
    ParseOptions options;options.unnamedRootBone=true;
    for(std::size_t k=0;k<kPowerupModels.size();++k) {
        const std::string needle=std::string(kPowerupModels[k])+'\0';
        for(const auto* hit=std::search(raw,raw+size,needle.begin(),needle.end());hit!=raw+size && !found[k];
            hit=std::search(hit+1,raw+size,needle.begin(),needle.end())) {
            const auto at=static_cast<std::size_t>(hit-raw);
            if(at<t5::kModelBytes) continue;
            const auto* h=raw+at-t5::kModelBytes;
            if(Get<std::uint32_t>(h)!=t5::kInline || !detail::LooksLikeModel(h)) continue;
            Model m;
            if(!ParseModel(raw,size,at-t5::kModelBytes,strings,m,nullptr,options) || m.name!=kPowerupModels[k]) continue;
            p.models[k]=std::move(m);found[k]=true;
        }
        if(!found[k]) return fail(std::string("powerup model ")+kPowerupModels[k]+" not found");
    }
    // zombie_bomb defines the gold material; zombie_ammocan its ammo box and
    // the gold again (by alias); the x2 icon and the skull alias the gold.
    const auto& bomb=p.models[2];const auto& ammo=p.models[0];
    if(bomb.materials.size()!=1 || bomb.materials[0].kind!=MaterialRef::Kind::Inline || bomb.materials[0].name!=kGoldMaterial)
        return fail("powerup gold material not found");
    if(ammo.materials.size()!=2 || ammo.materials[0].kind!=MaterialRef::Kind::Inline || ammo.materials[0].name!=kAmmoMaterial
       || ammo.materials[1].kind!=MaterialRef::Kind::Alias) return fail("powerup ammo materials not recognised");
    p.gold=bomb.materials[0];p.ammo=ammo.materials[0];p.goldAlias=ammo.materials[1].alias;
    for(std::size_t k=0;k<p.models.size();++k) {
        const auto& m=p.models[k];
        if(m.numBones!=1 || m.numRootBones!=1) return fail(std::string(kPowerupModels[k])+": unexpected skeleton");
        for(const auto& ref:m.materials) {
            if(ref.kind==MaterialRef::Kind::Inline && ref.name==kGoldMaterial) p.surfaceMaterial[k].push_back(0);
            else if(ref.kind==MaterialRef::Kind::Inline && ref.name==kAmmoMaterial) p.surfaceMaterial[k].push_back(1);
            else if(ref.kind==MaterialRef::Kind::Alias && ref.alias==p.goldAlias) p.surfaceMaterial[k].push_back(0);
            else return fail(std::string(kPowerupModels[k])+": unexpected material");
        }
    }
    return p;
}

// ------------------------------------------------------- dismemberment
// do_gib's limb refs (animscripts\zombie_death.gsc get_limb_data).
enum class GibPiece : int {RightArm,LeftArm,RightLeg,LeftLeg,Count};
enum class GibSkin : std::uint8_t {Body,HeadSkin,Eyes,Gore,Drop};
struct GibModel {Model model;std::vector<GibSkin> skins;};   // skins: per surface after drops (no Drop left)
struct GibSet {
    // torsoDmg1 (upper body), torsoDmg2 (right arm off), torsoDmg3 (left arm
    // off), torsoDmg4 (guts), torsoDmg5 (neck stump), legDmg1 (legs),
    // legDmg2/3 (a leg off), legDmg4 (no legs), gibSpawn1-4 (thrown pieces).
    GibModel upper,rightArmOff,leftArmOff,guts,neck,legs,rightLegOff,leftLegOff,noLegs;
    std::array<GibModel,static_cast<std::size_t>(GibPiece::Count)> pieces;
    bool piecesFound=false;
    MaterialRef gore;                    // the inline gore_limbs material
};
inline constexpr std::array<const char*,9> kGibModelNames={"bo1_honorguard_g_upclean","bo1_honorguard_g_rarmoff","bo1_honorguard_g_larmoff",
    "bo1_honorguard_g_guts","bo1_honorguard_g_behead","bo1_honorguard_g_lowclean","bo1_honorguard_g_rlegoff","bo1_honorguard_g_llegoff",
    "bo1_honorguard_g_legsoff"};
inline constexpr std::array<const char*,4> kGibPieceNames={"bo1_honorguard_g_rarmspawn","bo1_honorguard_g_larmspawn",
    "bo1_honorguard_g_rlegspawn","bo1_honorguard_g_llegspawn"};
// The tag each piece is thrown from (gibSpawnTag1-4).
inline constexpr std::array<const char*,4> kGibPieceTags={"j_elbow_ri","j_elbow_le","j_knee_ri","j_knee_le"};

namespace detail {
inline bool Contains(const std::string& text,const char* part) {return text.find(part)!=std::string::npos;}
inline std::optional<GibSkin> InlineSkin(const std::string& name) {
    if(Contains(name,"gore_limbs")) return GibSkin::Gore;
    if(Contains(name,"gore_blend") || Contains(name,"gore_punch")) return GibSkin::Drop;
    if(Contains(name,"honorguard")) return GibSkin::Body;
    if(Contains(name,"head")) return GibSkin::HeadSkin;
    if(Contains(name,"eye")) return GibSkin::Eyes;
    return std::nullopt;
}
} // namespace detail

// `c` is the carved character; the dismemberment models follow it.
inline std::optional<GibSet> CarveGibs(const std::uint8_t* raw,std::size_t size,const std::vector<std::string>& strings,const Character& c,
                                       std::string* error=nullptr) {
    auto fail=[&](const std::string& text){if(error)*error=text;return std::optional<GibSet>{};};
    // The character's own material aliases: the body sheet slots of body1 and
    // body2, the head skin and the eyes.
    std::set<std::uint32_t> body;
    for(const auto* m:{&c.body1,&c.body2}) for(const auto& ref:m->materials)
        if(ref.kind==MaterialRef::Kind::Alias && ref.alias!=c.headSkinAlias && ref.alias!=c.eyesAlias) body.insert(ref.alias);
    // Every model of the character's skeleton (or a part of it) and every
    // two-bone physics piece after body2, in zone order.
    std::vector<Model> parts,pieces;
    ParseOptions options;options.allowPhysics=true;
    const std::size_t from=std::max(c.body1.end,c.body2.end),to=std::min(size,from+(48u<<20));
    for(std::size_t at=from;at+t5::kModelBytes<to;++at) {
        if(raw[at+8]!=0xff || raw[at+32]!=0xff || !detail::LooksLikeModel(raw+at)) continue;
        Model m;
        if(!ParseModel(raw,size,at,strings,m,nullptr,options)) continue;
        const auto end=m.end;
        if(m.bones.size()==2 && m.bones[0]=="tag_origin" && m.bones[1]=="tag_fx" && m.physics) {if(pieces.size()<8) pieces.push_back(std::move(m));}
        else if(m.bones.size()>=6 && m.bones[0]=="tag_origin" && m.bones[1]=="j_mainroot" && !m.physics && m.bones.size()<=c.body1.bones.size()
                && std::equal(m.bones.begin(),m.bones.begin()+2,c.body1.bones.begin())) parts.push_back(std::move(m));
        at=std::max(at,end)-1;
        if(parts.size()>=24 && pieces.size()>=4) break;
    }
    // Gore material aliases: the first part defining the three gore materials
    // inline (gore_limbs, gore_blend, gore_punch in consecutive slots) gave
    // them consecutive alias slots in that order.
    GibSet g;
    std::map<std::uint32_t,GibSkin> goreAlias;
    for(const auto& m:parts) {
        for(std::size_t n=0;n+2<m.materials.size();++n) {
            const auto& a=m.materials[n];const auto& b=m.materials[n+1];const auto& d=m.materials[n+2];
            if(a.kind!=MaterialRef::Kind::Inline || b.kind!=MaterialRef::Kind::Inline || d.kind!=MaterialRef::Kind::Inline
               || !detail::Contains(a.name,"gore_limbs") || !detail::Contains(b.name,"gore_blend") || !detail::Contains(d.name,"gore_punch")) continue;
            // The model's own later LODs alias them: the smallest unknown alias is gore_limbs.
            std::set<std::uint32_t> unknown;
            for(const auto& ref:m.materials) if(ref.kind==MaterialRef::Kind::Alias && !body.count(ref.alias) && ref.alias!=c.headSkinAlias && ref.alias!=c.eyesAlias)
                unknown.insert(ref.alias);
            if(unknown.empty()) return fail("gore material aliases not found");
            const auto base=*unknown.begin();
            for(const auto u:unknown) if(u!=base && u!=base+4 && u!=base+8) return fail("unexpected gore material aliases");
            goreAlias={{base,GibSkin::Gore},{base+4,GibSkin::Drop},{base+8,GibSkin::Drop}};
            g.gore=a;
            break;
        }
        if(!goreAlias.empty()) break;
    }
    if(goreAlias.empty()) return fail("gore materials not found");
    auto classify=[&](const Model& m,GibModel& out,std::optional<GibSkin> unknownAs,std::string* why)->bool {
        std::vector<GibSkin> skins;std::vector<bool> drop;
        for(const auto& ref:m.materials) {
            std::optional<GibSkin> s;
            if(ref.kind==MaterialRef::Kind::Inline) s=detail::InlineSkin(ref.name);
            else if(ref.kind==MaterialRef::Kind::Alias) {
                if(ref.alias==c.headSkinAlias) s=GibSkin::HeadSkin;
                else if(ref.alias==c.eyesAlias) s=GibSkin::Eyes;
                else if(body.count(ref.alias)) s=GibSkin::Body;
                else if(goreAlias.count(ref.alias)) s=goreAlias[ref.alias];
                else s=unknownAs;
            }
            if(!s) {
                if(why) {
                    char text[96];
                    std::snprintf(text,sizeof(text),"unclassified material (model at 0x%zx: %s 0x%x)",m.at,
                        ref.kind==MaterialRef::Kind::Inline?ref.name.c_str():ref.kind==MaterialRef::Kind::Alias?"alias":"null",ref.alias);
                    *why=text;
                }
                return false;
            }
            skins.push_back(*s);drop.push_back(*s==GibSkin::Drop);
        }
        if(!WithoutSurfaces(m,drop,out.model,why)) return false;
        out.skins.clear();
        for(const auto s:skins) if(s!=GibSkin::Drop) out.skins.push_back(s);
        if(!(SkeletonError(out.model)<=kSkeletonTolerance)) {if(why)*why="skeleton does not match its base pose";return false;}
        return true;
    };
    // Parts by the bones their vertices use.
    struct Part {const Model* m;std::set<std::string> used;};
    std::vector<Part> torsos,legs;const Model* neck=nullptr;
    for(const auto& m:parts) {
        // Only this character's parts: they use its body sheet or head skin
        // (the zone holds other characters' legs too).
        if(std::none_of(m.materials.begin(),m.materials.end(),[&](const MaterialRef& ref){
            return ref.kind==MaterialRef::Kind::Alias && (body.count(ref.alias) || ref.alias==c.headSkinAlias);})) continue;
        auto used=UsedBones(m);
        const bool knees=used.count("j_knee_le") || used.count("j_knee_ri") || used.count("j_ankle_le") || used.count("j_ankle_ri");
        if(m.bones==c.body1.bones && !knees && used.count("j_spine4")) torsos.push_back({&m,std::move(used)});
        else if(!m.hasBone("j_spine4") && m.hasBone("j_hip_le") && m.hasBone("j_hip_ri")) legs.push_back({&m,std::move(used)});
        else if(!neck && m.bones.size()<=8 && m.bones.back()=="j_neck" && m.hasBone("j_spine4") && !m.hasBone("j_head")) neck=&m;
    }
    auto first=[&](const std::vector<Part>& list,auto test)->const Model* {for(const auto& p:list) if(test(p.used)) return p.m;return nullptr;};
    auto last=[&](const std::vector<Part>& list,auto test)->const Model* {const Model* r=nullptr;for(const auto& p:list) if(test(p.used)) r=p.m;return r;};
    auto has=[](const std::set<std::string>& u,const char* b){return u.count(b)!=0;};
    const Model* upper=first(torsos,[&](const auto& u){return has(u,"j_wrist_le") && has(u,"j_wrist_ri") && has(u,"j_elbow_le") && has(u,"j_elbow_ri");});
    const Model* rightArm=first(torsos,[&](const auto& u){return !has(u,"j_elbow_ri") && !has(u,"j_wrist_ri") && has(u,"j_wrist_le");});
    const Model* leftArm=first(torsos,[&](const auto& u){return !has(u,"j_wrist_le") && has(u,"j_wrist_ri");});
    const Model* guts=last(torsos,[&](const auto& u){return !has(u,"j_wrist_le") && has(u,"j_wrist_ri");});
    const Model* lower=first(legs,[&](const auto& u){return has(u,"j_knee_le") && has(u,"j_knee_ri") && has(u,"j_ankle_le") && has(u,"j_ankle_ri");});
    const Model* rightLeg=first(legs,[&](const auto& u){return !has(u,"j_knee_ri") && !has(u,"j_ankle_ri") && has(u,"j_ankle_le");});
    if(!rightLeg) rightLeg=first(legs,[&](const auto& u){return !has(u,"j_ankle_ri") && has(u,"j_ankle_le");});
    const Model* leftLeg=first(legs,[&](const auto& u){return !has(u,"j_knee_le") && !has(u,"j_ankle_le") && has(u,"j_ankle_ri");});
    if(!leftLeg) leftLeg=first(legs,[&](const auto& u){return !has(u,"j_ankle_le") && has(u,"j_ankle_ri");});
    const Model* noLegs=first(legs,[&](const auto& u){return !has(u,"j_knee_le") && !has(u,"j_knee_ri") && !has(u,"j_ankle_le") && !has(u,"j_ankle_ri");});
    if(!upper || !rightArm || !leftArm || !guts || !lower || !rightLeg || !leftLeg || !noLegs || !neck)
        return fail("dismemberment models not recognised (torsos="+std::to_string(torsos.size())+" legs="+std::to_string(legs.size())+")");
    std::string why;
    const std::pair<const Model*,GibModel*> wanted[]={{upper,&g.upper},{rightArm,&g.rightArmOff},{leftArm,&g.leftArmOff},{guts,&g.guts},
        {neck,&g.neck},{lower,&g.legs},{rightLeg,&g.rightLegOff},{leftLeg,&g.leftLegOff},{noLegs,&g.noLegs}};
    for(const auto& [m,out]:wanted) if(!classify(*m,*out,std::nullopt,&why)) return fail("dismemberment model: "+why);
    // The thrown pieces, in the character script's order: right arm, left
    // arm, right leg, left leg. A piece's own material aliases are skin (arms)
    // or the body sheet (legs).
    if(pieces.size()>=4) {
        bool ok=true;
        for(std::size_t k=0;k<4 && ok;++k) {
            const bool arm=k<2;
            ok=classify(pieces[k],g.pieces[k],arm?GibSkin::HeadSkin:GibSkin::Body,&why);
        }
        g.piecesFound=ok;
        if(!ok && error) *error="pieces: "+why;
    }
    return g;
}

// ------------------------------------------------------- zone spec
// Image asset name for a BO1 texture: "bo1_" + the name with every
// character outside [a-z0-9_] replaced (BO1 combined images carry ~ & $ -).
inline std::string ExtraImageName(const std::string& source) {
    std::string n="bo1x_";
    for(const char ch:source) {
        const char c=static_cast<char>(ch>='A' && ch<='Z'?ch-'A'+'a':ch);
        n+=(c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'?c:'_';
    }
    return n;
}
inline constexpr const char* kFallbackGore="bo1_fallback_gore";
struct ExtraReport {
    bool powerups=false,gibs=false,pieces=false;
    std::size_t models=0;
    std::vector<std::string> fallbacks;  // textures the extra materials lack
};
// The textures the extra materials need from the archives.
inline std::set<std::string> ExtraTextures(const PowerupSet* p,const GibSet* g) {
    std::set<std::string> out;
    auto add=[&](const MaterialRef& m){for(const auto h:{kHashColorMap,kHashNormalMap,kHashSpecularMap}) {const auto n=ImageByHash(m,h);if(!n.empty()) out.insert(n);}};
    if(p) {add(p->gold);add(p->ammo);}
    if(g) add(g->gore);
    return out;
}
// Appends the powerup and dismemberment materials and models to a spec built
// by CharacterSpec (materials 0 body, 1 head skin, 2 eyes).
inline bool ExtendZoneSpec(ZoneSpec& spec,const PowerupSet* p,const GibSet* g,const std::map<std::string,Texture>& textures,ExtraReport& report,
                           std::string* error=nullptr) {
    if(spec.materials.size()!=3) return detail::Fail(error,"character materials missing");
    report=ExtraReport{};
    enum class Kind {Color,Normal,Specular};
    auto take=[&](const MaterialRef& m,std::uint32_t hash,Kind kind,const char* fallbackColor)->std::string {
        const auto source=ImageByHash(m,hash);
        const auto found=source.empty()?textures.end():textures.find(source);
        if(found==textures.end()) {
            report.fallbacks.push_back(m.name+":"+(source.empty()?std::string("-"):source));
            if(kind==Kind::Color) {
                if(!spec.images.count(fallbackColor)) spec.images[fallbackColor]=!std::strcmp(fallbackColor,kFallbackGore)?SolidDxt5(4,0x7041,0xff):SolidDxt5(4,0x8410,0xff);
                return fallbackColor;
            }
            if(kind==Kind::Normal) return mw2::kIdentityNormal;
            if(!spec.images.count(kFallbackSpecular)) spec.images[kFallbackSpecular]=SolidDxt5(4,0x2104,0x30);
            return kFallbackSpecular;
        }
        Texture t=found->second;
        if(kind==Kind::Specular && t.format==kD3dDxt1) {Texture glossy;if(WithGloss(t,0x60,glossy)) t=std::move(glossy);}
        const auto name=ExtraImageName(source);
        spec.images[name]=std::move(t);
        return name;
    };
    auto material=[&](const MaterialRef& m,const char* name,const char* fallbackColor,std::array<float,4> env)->std::size_t {
        MaterialSpec ms;ms.name=name;
        ms.color=take(m,kHashColorMap,Kind::Color,fallbackColor);
        ms.normal=take(m,kHashNormalMap,Kind::Normal,fallbackColor);
        ms.spec=take(m,kHashSpecularMap,Kind::Specular,fallbackColor);
        ms.envMapParms=env;
        spec.materials.push_back(std::move(ms));
        return spec.materials.size()-1;
    };
    if(p) {
        const auto gold=material(p->gold,"mc/bo1_mtl_x2icon_gold",kFallbackColor,{0.4f,1.2f,0.3f,2.f});
        const auto ammo=material(p->ammo,"mc/bo1_mtl_zombie_ammocan",kFallbackColor,{0.1f,0.8f,0.1f,2.f});
        for(std::size_t k=0;k<p->models.size();++k) {
            ModelSpec ms{kPowerupModels[k],&p->models[k],{}};
            for(const auto i:p->surfaceMaterial[k]) ms.surfaceMaterial.push_back(i==0?gold:ammo);
            spec.models.push_back(std::move(ms));++report.models;
        }
        report.powerups=true;
    }
    if(g) {
        const auto gore=material(g->gore,"mc/bo1_mtl_gore_limbs",kFallbackGore,{0.f,0.6f,0.05f,2.f});
        auto add=[&](const char* name,const GibModel& gm) {
            ModelSpec ms{name,&gm.model,{}};
            for(const auto s:gm.skins) switch(s) {
                case GibSkin::Body: ms.surfaceMaterial.push_back(0);break;
                case GibSkin::HeadSkin: ms.surfaceMaterial.push_back(1);break;
                case GibSkin::Eyes: ms.surfaceMaterial.push_back(2);break;
                case GibSkin::Gore: case GibSkin::Drop: ms.surfaceMaterial.push_back(gore);break;
            }
            spec.models.push_back(std::move(ms));++report.models;
        };
        const GibModel* parts[]={&g->upper,&g->rightArmOff,&g->leftArmOff,&g->guts,&g->neck,&g->legs,&g->rightLegOff,&g->leftLegOff,&g->noLegs};
        for(std::size_t k=0;k<kGibModelNames.size();++k) add(kGibModelNames[k],*parts[k]);
        report.gibs=true;
        if(g->piecesFound) {
            for(std::size_t k=0;k<kGibPieceNames.size();++k) add(kGibPieceNames[k],g->pieces[k]);
            report.pieces=true;
        }
    }
    return true;
}
} // namespace mod::bo1model
