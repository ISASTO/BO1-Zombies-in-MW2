#pragma once
// Test46: Black Ops zombie effects (FX) for MW2.
//
// Reads a handful of effects from the owner's own common_zombie.ff (T5 PC,
// x86 FxEffectDef/FxElemDef) and turns them into MW2 x64 effects in the
// generated zombie zone (bo1_model.hpp writes them):
//
//   misc/fx_zombie_powerup_on            the powerup glow (powerup_wobble)
//   misc/fx_zombie_powerup_grab          the pickup flash (powerup_grab)
//   maps/zombie/fx_zombie_dog_lightning_spawn / _buildup   hellhound arrival
//   maps/zombie/fx_zombie_dog_eyes       hellhound eye glow (tag_eye)
//   maps/zombie/fx_zombie_dog_explosion  hellhound death burst
//   maps/zombie/fx_zombie_dog_fire_trail / _ash_trail   hellhound trails
//   maps/zombie/fx_mp_zombie_hand_dirt_burst / _body_dirt_billowing   zombie rise
//
// The elem data (spawn, life, velocity and visual-state curves, sort order,
// lighting) is copied field by field: both games keep IW3's layout up to the
// rotation, and the samples are byte-identical structures. What differs:
//
//  * T5's extra fields (rotationAxis, windInfluence, spawn sound, billboard
//    pivot...) are dropped; T5 elem types map onto MW2's (rotated sprites
//    become billboards, a tail or line without velocity becomes a
//    non-uniform billboard, a line much longer than wide is dropped, and an
//    elem with no visuals that only spawns another effect becomes MW2's
//    visual-less model elem); sounds, decals, models, trails and runners are
//    dropped (none of these effects needs them to look right);
//  * flag bits are kept only where MW2 means the same thing (relative spawn
//    and run, frustum cull, collision, velocity graphs, gravity, non-uniform
//    scale);
//  * each sprite material becomes an MW2 effect material: the same colour map
//    (from the Black Ops .iwd archives), its blend (add, blend or screen) on
//    MW2's own effect technique set (zfeather when the material feathers),
//    its featherParms/colorTint constants and MW2's effect state bits.
//
// A T5 zone refers to an asset loaded earlier with a stream offset (an
// alias), not a name. Those offsets were resolved once, offline, against the
// owner's common_zombie.ff and are kept below as names only (which material
// each aliased visual uses, which image each aliased texture uses); a
// visual without an entry is dropped, never guessed. Nothing from Black Ops
// is shipped: the effects are built on the owner's machine.
//
// Header-only and free of engine state so the native tests can exercise it.
#include "bo1_model.hpp"
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

namespace mod::bo1fx {
using bo1xanim::Reader;
using bo1xanim::Get;
using bo1xanim::Put;
using bo1model::Texture;

// ---------------------------------------------------------------- T5 input
namespace t5 {
inline constexpr std::uint32_t kInline=0xffffffffu,kInsert=0xfffffffeu;
inline bool IsInline(std::uint32_t p) {return p==kInline || p==kInsert;}
inline constexpr std::size_t kEffectBytes=60,kElemBytes=0x124,kMaterialBytes=192,kImageBytes=52,kTextureDefBytes=16,kConstantBytes=32,
    kStateBitsBytes=8,kTrailBytes=28,kTrailVertexBytes=20,kVelSampleBytes=96,kVisSampleBytes=48;
inline constexpr std::size_t kElemGravity=156,kElemReflection=164,kElemAtlas=172,kElemType=184,kElemVisualCount=185,kElemVelCount=186,
    kElemVisCount=187,kElemVel=188,kElemVis=192,kElemVisuals=196,kElemCollMins=200,kElemCollMaxs=212,kElemImpact=224,kElemDeath=228,
    kElemEmitted=232,kElemEmitDist=236,kElemEmitDistVariance=244,kElemAttached=252,kElemTrail=256,kElemSortOrder=260,
    kElemLightingFrac=261,kElemSpawnSound=280;
enum Type : std::uint8_t {Billboard=0,Oriented=1,Rotated=2,Tail=3,Line=4,Trail=5,Cloud=6,Model=7,Omni=8,Spot=9,Sound=10,Decal=11,Runner=12};
} // namespace t5

namespace mw2 {
enum Type : std::uint8_t {Billboard=0,Oriented=1,Tail=2,Trail=3,Cloud=4,Model=7,Omni=8,Spot=9,Runner=12};
inline constexpr std::size_t kElemGravity=152,kElemReflection=160,kElemAtlas=168,kElemCollBounds=208,kElemEmitDist=256,
    kElemEmitDistVariance=264,kElemSortOrder=280,kElemLightingFrac=281;
// FxElemDef flags both games give the same meaning: spawn relative to the
// effect, frustum cull, runner random rotation, spawn offset shape, run
// relative, use collision, die on touch, draw past fog, local and world
// velocity graphs, gravity, non-uniform scale.
inline constexpr std::uint32_t kKeptElemFlags=0x2u|0x4u|0x8u|0x30u|0xc0u|0x100u|0x200u|0x400u|0x1000000u|0x2000000u|0x4000000u|0x10000000u;
inline constexpr std::uint32_t kNonUniformScale=0x10000000u;
} // namespace mw2

// Material constants (name hash as stored).
inline constexpr std::uint32_t kHashFeatherParms=0x4d7ea234u,kHashColorTint=0xb60c3b3au,kHashDistortionScale=0xf37b6913u;
// GfxStateBits loadBits[0] blend factors (same encoding in both games).
inline constexpr std::uint32_t kBlendOne=2,kBlendSrcAlpha=5,kBlendInvSrcAlpha=6,kBlendInvDestColor=0xa;
enum class Blend {None,Add,Blend,Screen};
inline Blend BlendOf(std::uint32_t loadBits0) {
    const auto src=loadBits0&0xfu,dst=(loadBits0>>4)&0xfu;
    if(src==kBlendOne && dst==kBlendOne) return Blend::Add;
    if(src==kBlendSrcAlpha && dst==kBlendInvSrcAlpha) return Blend::Blend;
    if(src==kBlendInvDestColor && dst==kBlendOne) return Blend::Screen;
    return Blend::None;
}
// MW2's own effect materials (common.ff): the technique set and the first
// state bits for each blend, with and without depth feathering.
struct EffectTechnique {const char* techniqueSet;std::uint32_t loadBits0;};
inline EffectTechnique TechniqueFor(Blend blend,bool feather) {
    switch(blend) {
        case Blend::Add: return feather?EffectTechnique{"effect_zfeather_add",0x09218922u}:EffectTechnique{"effect_add",0x19218922u};
        case Blend::Blend: return feather?EffectTechnique{"effect_zfeather_blend",0x19289165u}:EffectTechnique{"effect_blend",0x19289165u};
        case Blend::Screen: return feather?EffectTechnique{"effect_zfeather_screen",0x0921892au}:EffectTechnique{"effect_screen",0x1921892au};
        case Blend::None: break;
    }
    return {nullptr,0};
}
// loadBits[1]: depth test less-equal without depth writes (MW2's 0x0000000c),
// or no depth test when the Black Ops material had none (lens flares).
inline std::uint32_t DepthBits(std::uint32_t t5LoadBits1) {return (t5LoadBits1&0x2u)?0x2u:0xcu;}

// -------------------------------------------------- the converted effects
inline constexpr std::array<const char*,10> kEffects={
    "misc/fx_zombie_powerup_on","misc/fx_zombie_powerup_grab",
    "maps/zombie/fx_zombie_dog_lightning_spawn","maps/zombie/fx_zombie_dog_lightning_buildup",
    "maps/zombie/fx_zombie_dog_eyes","maps/zombie/fx_zombie_dog_explosion",
    "maps/zombie/fx_zombie_dog_fire_trail","maps/zombie/fx_zombie_dog_ash_trail",
    "maps/zombie/fx_mp_zombie_hand_dirt_burst","maps/zombie/fx_mp_zombie_body_dirt_billowing"};
enum class Effect : std::size_t {PowerupOn,PowerupGrab,DogLightningSpawn,DogLightningBuildup,DogEyes,DogExplosion,DogFireTrail,DogAshTrail,
    RiseHandDirt,RiseBodyDirt,Count};
inline const char* EffectName(Effect e) {return kEffects[static_cast<std::size_t>(e)];}

// Aliased visuals: (effect, elem, visual) -> the material loaded earlier.
struct VisualAlias {const char* effect;std::uint8_t elem,visual;const char* material;};
inline constexpr VisualAlias kVisualAliases[]={
    {"maps/zombie/fx_zombie_dog_lightning_spawn",1,0,"gfx_fxt_lensflare_stripe_white"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",3,0,"gfx_fxt_light_flare_phosphorous_eo20"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",5,0,"gfx_fxt_lensflare_stripe_white"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",6,0,"gfx_fxt_env_lightning_bolt_air_add"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",8,0,"gfx_fxt_env_cloud_exp_add"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",10,0,"gfx_fxt_light_flare_phosphorous"},
    {"maps/zombie/fx_zombie_dog_lightning_spawn",12,0,"gfx_fxt_env_lightning_bolt_air_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",0,0,"gfx_fxt_lensflare_stripe_white"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",1,0,"gfx_fxt_env_electric_arc1_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",1,1,"gfx_fxt_env_electric_arc2_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",2,0,"gfx_fxt_env_electric_arc1_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",2,1,"gfx_fxt_env_electric_arc2_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",3,0,"gfx_fxt_light_flare_phosphorous_eo20"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",4,0,"gfx_fxt_env_electric_arc1_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",4,1,"gfx_fxt_env_electric_arc2_add"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",5,0,"gfx_fxt_light_flare_phosphorous"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",6,0,"gfx_fxt_light_flare_phosphorous"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",6,1,"gfx_fxt_light_flare2"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",7,0,"gfx_fxt_light_flare_phosphorous"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",7,1,"gfx_fxt_light_flare2"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",8,0,"gfx_fxt_lensflare_stripe_white"},
    {"maps/zombie/fx_zombie_dog_lightning_buildup",10,0,"gfx_fxt_env_lightning_bolt_air_add"},
    {"maps/zombie/fx_zombie_dog_eyes",1,0,"gfx_fxt_light_flare2_eyeglow"},
    {"maps/zombie/fx_zombie_dog_eyes",3,0,"gfx_fxt_light_flare2_eyeglow2"},
    {"maps/zombie/fx_zombie_dog_explosion",1,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_zombie_dog_explosion",4,0,"gfx_fxt_light_flare_phosphorous"},
    {"maps/zombie/fx_zombie_dog_explosion",5,0,"gfx_fxt_fire_flame_lick_add"},
    {"maps/zombie/fx_zombie_dog_explosion",6,0,"gfx_fxt_exp_ember_omni"},
    {"maps/zombie/fx_zombie_dog_explosion",9,0,"gfx_fxt_bio_bloodburst_b"},
    {"maps/zombie/fx_zombie_dog_explosion",13,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_zombie_dog_explosion",14,0,"gfx_fxt_smk_gen"},
    {"maps/zombie/fx_zombie_dog_explosion",15,0,"gfx_fxt_bio_bloodburst_b"},
    {"maps/zombie/fx_zombie_dog_explosion",16,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_zombie_dog_explosion",18,0,"gfx_fxt_spark_anim_blink"},
    {"maps/zombie/fx_zombie_dog_fire_trail",0,0,"gfx_fxt_spark_anim_blink"},
    {"maps/zombie/fx_zombie_dog_fire_trail",1,0,"gfx_fxt_fire_flame_base_01_z3"},
    {"maps/zombie/fx_zombie_dog_fire_trail",3,0,"gfx_fxt_fire_flame_base_01_z3"},
    {"maps/zombie/fx_zombie_dog_fire_trail",4,0,"gfx_fxt_smk_gen_z3"},
    {"maps/zombie/fx_zombie_dog_fire_trail",5,0,"gfx_fxt_fire_flame_base_01_z3"},
    {"maps/zombie/fx_zombie_dog_ash_trail",0,0,"gfx_fxt_spark_anim_blink"},
    {"maps/zombie/fx_zombie_dog_ash_trail",1,0,"gfx_fxt_smk_gen_z3"},
    {"maps/zombie/fx_zombie_dog_ash_trail",3,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_zombie_dog_ash_trail",4,0,"gfx_fxt_exp_ember_omni2"},
    {"maps/zombie/fx_zombie_dog_ash_trail",4,1,"gfx_fxt_exp_ember_omni3"},
    {"maps/zombie/fx_zombie_dog_ash_trail",5,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_zombie_dog_ash_trail",6,0,"gfx_fxt_spark_anim_blink"},
    {"misc/fx_zombie_powerup_on",0,0,"gfx_fxt_muz_vert_gen"},
    {"misc/fx_zombie_powerup_on",1,0,"gfx_fxt_light_flare_halogen"},
    {"misc/fx_zombie_powerup_on",3,0,"gfx_fxt_light_flare_halogen"},
    {"misc/fx_zombie_powerup_grab",2,0,"gfx_fxt_smk_gen_add"},
    {"misc/fx_zombie_powerup_grab",3,0,"gfx_fxt_light_flare_phosphorous"},
    {"misc/fx_zombie_powerup_grab",4,0,"gfx_fxt_spark_anim_blink"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",0,0,"gfx_fxt_smk_gen_z20"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",1,0,"gfx_fxt_debris_gib_rock"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",2,0,"gfx_fxt_debris_plume_water_z5"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",3,0,"gfx_fxt_debris_plume_water_z5"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",4,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",6,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",7,0,"gfx_fxt_debris_plume_dirt"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",8,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_hand_dirt_burst",9,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",0,0,"gfx_fxt_smk_gen_z20"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",2,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",3,0,"gfx_fxt_debris_clump"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",4,0,"gfx_fxt_debris_gib_rock"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",5,0,"gfx_fxt_smk_gen_z20"},
    {"maps/zombie/fx_mp_zombie_body_dirt_billowing",6,0,"gfx_fxt_debris_plume_water"}};
// Aliased colour maps: material -> the image loaded earlier.
struct ImageAlias {const char* material;const char* image;};
inline constexpr ImageAlias kImageAliases[]={
    {"gfx_fxt_debris_clump_add","fxt_debris_clump"},{"gfx_fxt_debris_plume_dirt_z5","fxt_debris_plume_dirt"},
    {"gfx_fxt_debris_plume_water","fxt_debris_plume_water"},{"gfx_fxt_env_lightning_bolt_streak","fxt_env_lighting_bolt_ground"},
    {"gfx_fxt_fire_flame_base_01_z3","fxt_fire_flame_base_01"},{"gfx_fxt_light_flare2_eyeglow","fxt_light_flare2"},
    {"gfx_fxt_light_flare2_eyeglow2","fxt_light_flare2"},{"gfx_fxt_light_flare_phosphorous_eo20","fxt_light_phosphorous"},
    {"gfx_fxt_smk_gen","fxt_smk_gen"},{"gfx_fxt_smk_gen_add","fxt_smk_gen"},{"gfx_fxt_smk_gen_z20","fxt_smk_gen"},
    {"gfx_fxt_smk_gen_z3","fxt_smk_gen"},{"gfx_fxt_smk_gen_z40","fxt_smk_gen"}};
inline const char* AliasedMaterial(const std::string& effect,std::size_t elem,std::size_t visual) {
    for(const auto& a:kVisualAliases) if(effect==a.effect && a.elem==elem && a.visual==visual) return a.material;
    return nullptr;
}
inline const char* AliasedImage(const std::string& material) {
    for(const auto& a:kImageAliases) if(material==a.material) return a.image;
    return nullptr;
}

// ------------------------------------------------------------ T5 parsing
struct T5Texture {std::uint32_t hash=0;std::uint8_t nameStart=0,nameEnd=0,sampler=0,semantic=0;std::string image;bool aliased=false;};
struct T5Material {
    std::size_t at=0;
    std::string name;
    std::uint8_t sortKey=0,atlasRows=1,atlasColumns=1;
    std::vector<T5Texture> textures;
    std::vector<std::array<std::uint8_t,32>> constants;
    std::vector<std::uint64_t> stateBits;
    bool hasConstant(std::uint32_t hash) const {
        for(const auto& c:constants) if(Get<std::uint32_t>(c.data())==hash) return true;
        return false;
    }
};
struct T5Visual {enum class Kind {None,Material,Alias,Other} kind=Kind::None;std::string material;std::uint32_t alias=0;};
struct T5Elem {
    std::array<std::uint8_t,t5::kElemBytes> raw{};
    std::vector<std::uint8_t> vel,vis;
    std::vector<T5Visual> visuals;
    std::string impact,death,emitted;        // inline names (aliases are dropped)
    bool trail=false;
    std::uint8_t type() const {return raw[t5::kElemType];}
};
struct T5Effect {
    std::size_t at=0,end=0;
    std::string name;
    std::uint8_t flags=0;
    std::int32_t totalSize=0,loopingLife=0;
    std::array<std::int32_t,3> counts{};
    std::vector<T5Elem> elems;
    std::vector<T5Material> inlineMaterials;   // materials defined inside this effect
};

namespace detail {
using bo1model::detail::Fail;
inline bool ParseImageName(Reader& r,std::uint32_t pointer,T5Texture& t) {
    if(!t5::IsInline(pointer)) {t.aliased=pointer!=0;return true;}
    bo1model::ImageRef image;
    if(!bo1model::detail::ParseImage(r,image)) return false;
    t.image=image.name;
    return true;
}
// T5 Material (192-byte header in temp): name, technique set (an alias),
// texture table (+ inline images), constants, state bits.
inline bool ParseMaterial(Reader& r,T5Material& m) {
    m.at=r.pos();
    const auto h=r.take(t5::kMaterialBytes);
    if(!r.ok()) return false;
    const auto name=Get<std::uint32_t>(h.data());
    m.sortKey=h[9];m.atlasRows=h[10];m.atlasColumns=h[11];
    const auto textureCount=h[170],constantCount=h[171],stateBitsCount=h[172];
    const auto techniqueSet=Get<std::uint32_t>(h.data()+176),textureTable=Get<std::uint32_t>(h.data()+180);
    const auto constantTable=Get<std::uint32_t>(h.data()+184),stateBitsTable=Get<std::uint32_t>(h.data()+188);
    if(t5::IsInline(name)) {
        m.name=r.cstring(256);
        if(m.name.empty() || !std::all_of(m.name.begin(),m.name.end(),[](char c){return c>' ' && c<0x7f;})) return false;
    }
    if(t5::IsInline(techniqueSet) || textureCount>16) return false;
    if(t5::IsInline(textureTable)) {
        std::vector<std::uint32_t> images;
        for(unsigned n=0;n<textureCount;++n) {
            const auto d=r.take(t5::kTextureDefBytes);
            if(!r.ok()) return false;
            T5Texture t;t.hash=Get<std::uint32_t>(d.data());t.nameStart=d[4];t.nameEnd=d[5];t.sampler=d[6];t.semantic=d[7];
            if(t.semantic==bo1model::t5::kWaterSemantic) return false;
            images.push_back(Get<std::uint32_t>(d.data()+12));
            m.textures.push_back(t);
        }
        for(std::size_t n=0;n<m.textures.size();++n) if(!ParseImageName(r,images[n],m.textures[n])) return false;
    } else if(textureTable!=0 && textureCount) return false;
    if(t5::IsInline(constantTable)) for(unsigned n=0;n<constantCount;++n) {
        const auto c=r.take(t5::kConstantBytes);
        if(!r.ok()) return false;
        std::array<std::uint8_t,32> a{};std::memcpy(a.data(),c.data(),32);m.constants.push_back(a);
    }
    if(t5::IsInline(stateBitsTable)) for(unsigned n=0;n<stateBitsCount;++n) m.stateBits.push_back(r.get<std::uint64_t>());
    return r.ok();
}
inline bool ParseRef(Reader& r,std::uint32_t pointer,std::string& out) {
    if(t5::IsInline(pointer)) out=r.cstring(256);
    return r.ok();
}
} // namespace detail

// The effect whose 60-byte header precedes the inline name `name` (the
// header's own bytes decide; the byte before the name is a header byte).
inline std::optional<std::size_t> FindEffect(const std::uint8_t* raw,std::size_t size,const std::string& name) {
    const std::string needle=name+std::string(1,'\0');
    for(auto it=std::search(raw,raw+size,needle.begin(),needle.end());it!=raw+size;
        it=std::search(it+1,raw+size,needle.begin(),needle.end())) {
        const std::size_t nameAt=static_cast<std::size_t>(it-raw);
        if(nameAt<t5::kEffectBytes) continue;
        const auto* h=raw+nameAt-t5::kEffectBytes;
        const auto looping=Get<std::int32_t>(h+16),oneShot=Get<std::int32_t>(h+20),emission=Get<std::int32_t>(h+24);
        if(Get<std::uint32_t>(h)==t5::kInline && Get<std::uint32_t>(h+28)==t5::kInline && looping>=0 && oneShot>=0 && emission>=0
           && looping+oneShot+emission>0 && looping+oneShot+emission<=64)
            return nameAt-t5::kEffectBytes;
    }
    return std::nullopt;
}
// The inline material whose 192-byte header precedes the name `name`.
inline std::optional<T5Material> FindMaterial(const std::uint8_t* raw,std::size_t size,const std::string& name) {
    const std::string needle=name+std::string(1,'\0');
    for(auto it=std::search(raw,raw+size,needle.begin(),needle.end());it!=raw+size;
        it=std::search(it+1,raw+size,needle.begin(),needle.end())) {
        const std::size_t nameAt=static_cast<std::size_t>(it-raw);
        if(nameAt<t5::kMaterialBytes) continue;
        const auto* h=raw+nameAt-t5::kMaterialBytes;
        const auto techniqueSet=Get<std::uint32_t>(h+176),textureTable=Get<std::uint32_t>(h+180);
        if(Get<std::uint32_t>(h)!=t5::kInline || t5::IsInline(techniqueSet) || !techniqueSet || !t5::IsInline(textureTable) || !h[170] || h[170]>16)
            continue;
        Reader r(raw,size,nameAt-t5::kMaterialBytes);
        T5Material m;
        if(detail::ParseMaterial(r,m) && m.name==name) return m;
    }
    return std::nullopt;
}
// FxEffectDef (60-byte header) at `at`: name, elems, then per elem its
// samples, visuals (inline materials parsed), effect names, trail and spawn
// sound, exactly in T5's load order.
inline std::optional<T5Effect> ParseEffect(const std::uint8_t* raw,std::size_t size,std::size_t at,std::string* error=nullptr) {
    auto fail=[&](const std::string& text)->std::optional<T5Effect> {if(error)*error=text;return std::nullopt;};
    Reader r(raw,size,at);
    T5Effect fx;fx.at=at;
    const auto h=r.take(t5::kEffectBytes);
    if(!r.ok()) return fail("truncated effect header");
    fx.flags=h[4];fx.totalSize=Get<std::int32_t>(h.data()+8);fx.loopingLife=Get<std::int32_t>(h.data()+12);
    for(std::size_t k=0;k<3;++k) fx.counts[k]=Get<std::int32_t>(h.data()+16+4*k);
    if(!t5::IsInline(Get<std::uint32_t>(h.data()))) return fail("effect name not inline");
    fx.name=r.cstring(256);
    const auto count=static_cast<std::size_t>(fx.counts[0])+static_cast<std::size_t>(fx.counts[1])+static_cast<std::size_t>(fx.counts[2]);
    if(!t5::IsInline(Get<std::uint32_t>(h.data()+28)) || !count || count>64) return fail("effect elems not inline");
    for(std::size_t n=0;n<count;++n) {
        T5Elem e;const auto bytes=r.take(t5::kElemBytes);
        if(!r.ok()) return fail("truncated elems");
        std::memcpy(e.raw.data(),bytes.data(),t5::kElemBytes);
        fx.elems.push_back(e);
    }
    for(std::size_t n=0;n<count;++n) {
        auto& e=fx.elems[n];const auto* p=e.raw.data();
        const auto type=e.type(),visualCount=p[t5::kElemVisualCount];
        if(t5::IsInline(Get<std::uint32_t>(p+t5::kElemVel))) e.vel=r.take((p[t5::kElemVelCount]+1u)*t5::kVelSampleBytes);
        if(t5::IsInline(Get<std::uint32_t>(p+t5::kElemVis))) e.vis=r.take((p[t5::kElemVisCount]+1u)*t5::kVisSampleBytes);
        if(!r.ok()) return fail("truncated samples");
        const auto visuals=Get<std::uint32_t>(p+t5::kElemVisuals);
        auto visual=[&](std::uint32_t pointer)->bool {
            T5Visual v;
            if(!pointer) {e.visuals.push_back(v);return true;}
            switch(type) {
                case t5::Model:
                    if(t5::IsInline(pointer)) return false;      // an inline model: not handled
                    v.kind=T5Visual::Kind::Other;break;
                case t5::Runner: case t5::Sound: {
                    std::string name;if(!detail::ParseRef(r,pointer,name)) return false;
                    v.kind=T5Visual::Kind::Other;v.material=name;break;
                }
                case t5::Omni: case t5::Spot: v.kind=T5Visual::Kind::None;break;
                default:
                    if(t5::IsInline(pointer)) {
                        T5Material m;if(!detail::ParseMaterial(r,m)) return false;
                        v.kind=T5Visual::Kind::Material;v.material=m.name;fx.inlineMaterials.push_back(std::move(m));
                    } else {v.kind=T5Visual::Kind::Alias;v.alias=pointer;}
            }
            e.visuals.push_back(v);
            return r.ok();
        };
        if(type==t5::Decal) {
            if(t5::IsInline(visuals)) {
                std::vector<std::uint32_t> marks;
                for(unsigned k=0;k<2u*visualCount;++k) marks.push_back(r.get<std::uint32_t>());
                for(const auto m:marks) {
                    if(!t5::IsInline(m)) continue;
                    T5Material mat;if(!detail::ParseMaterial(r,mat)) return fail("decal material");
                    fx.inlineMaterials.push_back(std::move(mat));
                }
            }
        } else if(visualCount>1) {
            if(t5::IsInline(visuals)) {
                std::vector<std::uint32_t> pointers;
                for(unsigned k=0;k<visualCount;++k) pointers.push_back(r.get<std::uint32_t>());
                for(const auto pointer:pointers) if(!visual(pointer)) return fail("visual of elem "+std::to_string(n));
            }
        } else if(!visual(visuals)) return fail("visual of elem "+std::to_string(n));
        if(!detail::ParseRef(r,Get<std::uint32_t>(p+t5::kElemImpact),e.impact) || !detail::ParseRef(r,Get<std::uint32_t>(p+t5::kElemDeath),e.death)
           || !detail::ParseRef(r,Get<std::uint32_t>(p+t5::kElemEmitted),e.emitted)) return fail("effect names");
        std::string attached;
        if(!detail::ParseRef(r,Get<std::uint32_t>(p+t5::kElemAttached),attached)) return fail("attached effect");
        if(t5::IsInline(Get<std::uint32_t>(p+t5::kElemTrail))) {
            const auto t=r.take(t5::kTrailBytes);
            if(!r.ok()) return fail("trail");
            e.trail=true;
            const auto verts=Get<std::int32_t>(t.data()+12),inds=Get<std::int32_t>(t.data()+20);
            if(verts<0 || inds<0 || verts>4096 || inds>8192) return fail("trail sizes");
            if(t5::IsInline(Get<std::uint32_t>(t.data()+16))) r.skip(static_cast<std::size_t>(verts)*t5::kTrailVertexBytes);
            if(t5::IsInline(Get<std::uint32_t>(t.data()+24))) r.skip(static_cast<std::size_t>(inds)*2);
        }
        std::string sound;
        if(!detail::ParseRef(r,Get<std::uint32_t>(p+t5::kElemSpawnSound),sound)) return fail("spawn sound");
    }
    if(!r.ok()) return fail("truncated effect");
    fx.end=r.pos();
    return fx;
}

// --------------------------------------------------------- conversion
struct Plan {
    std::vector<T5Effect> effects;                 // kEffects order (missing ones left out)
    std::map<std::string,T5Material> materials;    // every material a kept visual uses
    std::map<std::string,std::string> imageOf;     // material -> Black Ops image name
    std::vector<std::string> notes;
    std::set<std::string> images() const {
        std::set<std::string> out;
        for(const auto& [m,i]:imageOf) {(void)m;out.insert(i);}
        return out;
    }
};
inline std::string ColorMapOf(const T5Material& m) {
    for(const auto& t:m.textures) if(t.hash==bo1model::kHashColorMap && !t.image.empty()) return t.image[0]==','?t.image.substr(1):t.image;
    for(const auto& t:m.textures) if(t.hash==bo1model::kHashColorMap && t.aliased) if(const auto* a=AliasedImage(m.name)) return a;
    return {};
}
// Phase 1: parse the effects and find every material their visuals use.
inline Plan PlanEffects(const std::uint8_t* raw,std::size_t size) {
    Plan plan;
    std::set<std::string> wanted;
    for(const auto* name:kEffects) {
        const auto at=FindEffect(raw,size,name);
        if(!at) {plan.notes.push_back(std::string("effect ")+name+" not found");continue;}
        std::string error;
        auto fx=ParseEffect(raw,size,*at,&error);
        if(!fx) {plan.notes.push_back(std::string("effect ")+name+": "+error);continue;}
        for(auto& m:fx->inlineMaterials) if(!m.name.empty() && m.name[0]!=',') plan.materials.emplace(m.name,m);
        for(std::size_t n=0;n<fx->elems.size();++n) {
            auto& e=fx->elems[n];
            for(std::size_t v=0;v<e.visuals.size();++v) {
                auto& vis=e.visuals[v];
                if(vis.kind==T5Visual::Kind::Alias) {
                    if(const auto* m=AliasedMaterial(fx->name,n,v)) {vis.kind=T5Visual::Kind::Material;vis.material=m;}
                    else plan.notes.push_back(fx->name+" elem "+std::to_string(n)+" visual "+std::to_string(v)+" alias unresolved");
                }
                if(vis.kind==T5Visual::Kind::Material) {
                    if(!vis.material.empty() && vis.material[0]==',') vis.material=vis.material.substr(1);
                    wanted.insert(vis.material);
                }
            }
        }
        plan.effects.push_back(std::move(*fx));
    }
    for(const auto& name:wanted) {
        if(!plan.materials.count(name)) {
            if(auto m=FindMaterial(raw,size,name)) plan.materials.emplace(name,std::move(*m));
            else {plan.notes.push_back("material "+name+" not found");continue;}
        }
        const auto image=ColorMapOf(plan.materials.at(name));
        if(image.empty()) plan.notes.push_back("material "+name+" has no colour map");
        else plan.imageOf[name]=image;
    }
    return plan;
}

inline std::string ZoneImageName(const std::string& image) {
    std::string n="bo1fx_";
    for(const char ch:image) n+=static_cast<char>(ch>='A' && ch<='Z'?ch-'A'+'a':(ch=='~' || ch=='&' || ch=='-' || ch=='$')?'_':ch);
    return n;
}
inline std::string ZoneMaterialName(const std::string& material) {return "bo1fx/"+material;}

struct Report {
    std::size_t effects=0,elems=0,droppedElems=0,materials=0,images=0;
    std::vector<std::string> notes;
};
namespace detail {
inline bool NonZero(const std::vector<std::uint8_t>& samples,std::size_t from,std::size_t to) {
    for(std::size_t s=0;s+96<=samples.size();s+=96)
        for(std::size_t k=from;k<to;k+=4) if(std::fabs(Get<float>(samples.data()+s+k))>1e-6f) return true;
    return false;
}
// The largest size[0] and size[1] of the visual-state samples (base + amplitude).
inline std::pair<float,float> SampleSizes(const std::vector<std::uint8_t>& vis) {
    float a=0.f,b=0.f;
    for(std::size_t s=0;s+48<=vis.size();s+=48) for(std::size_t o:{std::size_t(0),std::size_t(24)}) {
        a=std::max(a,std::fabs(Get<float>(vis.data()+s+o+12)));b=std::max(b,std::fabs(Get<float>(vis.data()+s+o+16)));
    }
    return {a,b};
}
} // namespace detail

// The MW2 elem type for a T5 elem, or nullopt to drop it.
inline std::optional<std::uint8_t> MapElemType(const T5Elem& e,bool& nonUniform) {
    nonUniform=false;
    const bool moving=detail::NonZero(e.vel,0,96);
    switch(e.type()) {
        case t5::Billboard: case t5::Rotated: return mw2::Billboard;
        case t5::Oriented: return mw2::Oriented;
        case t5::Cloud: return mw2::Cloud;
        case t5::Omni: return mw2::Omni;
        case t5::Spot: return mw2::Spot;
        case t5::Tail:
            if(moving) return mw2::Tail;
            nonUniform=true;return mw2::Billboard;
        case t5::Line: {
            if(moving) return mw2::Tail;
            const auto [w,l]=detail::SampleSizes(e.vis);
            if(w<=0.f || l>8.f*w) return std::nullopt;
            nonUniform=true;return mw2::Billboard;
        }
        default: return std::nullopt;     // trail, model, sound, decal, runner
    }
}

// Phase 2: append the effects (with their materials and images) to `spec`.
// `textures` holds the Black Ops images found in the archives, by name.
inline bool AppendEffects(const Plan& plan,const std::map<std::string,Texture>& textures,bo1model::ZoneSpec& spec,Report& report,
                          std::string* error=nullptr) {
    report=Report{};report.notes=plan.notes;
    std::map<std::string,std::size_t> materialIndex;
    // One MW2 effect material per Black Ops material that has a supported
    // blend, no distortion and its colour map.
    auto material=[&](const std::string& name)->std::optional<std::size_t> {
        if(const auto found=materialIndex.find(name);found!=materialIndex.end()) return found->second;
        const auto m=plan.materials.find(name);const auto image=plan.imageOf.find(name);
        if(m==plan.materials.end() || image==plan.imageOf.end()) return std::nullopt;
        const auto& t=m->second;
        if(t.stateBits.empty() || t.hasConstant(kHashDistortionScale)) {report.notes.push_back("material "+name+" not an effect blend");return std::nullopt;}
        const auto lb0=static_cast<std::uint32_t>(t.stateBits[0]),lb1=static_cast<std::uint32_t>(t.stateBits[0]>>32);
        const bool feather=t.hasConstant(kHashFeatherParms);
        const auto technique=TechniqueFor(BlendOf(lb0),feather);
        if(!technique.techniqueSet) {report.notes.push_back("material "+name+" blend unsupported");return std::nullopt;}
        const auto texture=textures.find(image->second);
        if(texture==textures.end()) {report.notes.push_back("image "+image->second+" missing");return std::nullopt;}
        bo1model::EffectMaterialSpec ms;
        ms.name=ZoneMaterialName(name);ms.techniqueSet=technique.techniqueSet;
        ms.image=ZoneImageName(image->second);
        if(!spec.images.count(ms.image)) {spec.images[ms.image]=texture->second;++report.images;}
        ms.sortKey=t.sortKey?t.sortKey:0x30;ms.atlasRows=std::max<std::uint8_t>(1,t.atlasRows);ms.atlasColumns=std::max<std::uint8_t>(1,t.atlasColumns);
        for(const auto& tex:t.textures) if(tex.hash==bo1model::kHashColorMap) {ms.sampler=tex.sampler;break;}
        // featherParms then colorTint (MW2 keeps constants sorted by hash).
        std::array<std::uint8_t,32> tint{};Put<std::uint32_t>(tint.data(),kHashColorTint);std::memcpy(tint.data()+4,"colorTint",9);
        for(std::size_t k=0;k<4;++k) Put<float>(tint.data()+16+4*k,1.f);
        for(const auto& c:t.constants) {
            const auto hash=Get<std::uint32_t>(c.data());
            if(hash==kHashColorTint) tint=c;
            else if(hash==kHashFeatherParms) ms.constants.push_back(c);
        }
        ms.constants.push_back(tint);
        ms.stateBits={static_cast<std::uint64_t>(technique.loadBits0)|(static_cast<std::uint64_t>(DepthBits(lb1))<<32),bo1model::mw2::kEffectSecondStateBits};
        spec.effectMaterials.push_back(std::move(ms));
        materialIndex.emplace(name,spec.effectMaterials.size()-1);
        ++report.materials;
        return spec.effectMaterials.size()-1;
    };
    for(const auto& fx:plan.effects) {
        bo1model::EffectSpec out;
        out.name=fx.name;out.flags=static_cast<std::uint8_t>(fx.flags&1u);out.loopingLife=fx.loopingLife;
        std::size_t total=bo1model::mw2::kEffectBytes+fx.name.size()+1;
        std::size_t first=0;
        for(std::size_t group=0;group<3;++group) {
            const auto count=static_cast<std::size_t>(std::max(0,fx.counts[group]));
            for(std::size_t n=first;n<first+count && n<fx.elems.size();++n) {
                const auto& e=fx.elems[n];
                bool nonUniform=false;
                auto type=MapElemType(e,nonUniform);
                bo1model::EffectElemSpec o;
                const bool spawner=e.visuals.empty() || std::all_of(e.visuals.begin(),e.visuals.end(),[](const T5Visual& v){return v.kind==T5Visual::Kind::None;});
                // Visuals: the materials that converted (lights have none).
                if(type && *type!=mw2::Omni && *type!=mw2::Spot) {
                    for(const auto& v:e.visuals) if(v.kind==T5Visual::Kind::Material) if(const auto i=material(v.material)) o.materials.push_back(*i);
                    if(o.materials.empty()) type.reset();
                }
                // An elem whose only job is spawning another (converted) effect.
                const bool refs=!e.impact.empty() || !e.death.empty() || !e.emitted.empty();
                if(!type && spawner && e.type()!=t5::Omni && e.type()!=t5::Spot && refs) {type=mw2::Model;o.materials.clear();}
                if(!type) {++report.droppedElems;--out.counts[group];continue;}
                const std::uint8_t mapped=type.value_or(mw2::Billboard);
                auto keepRef=[&](const std::string& name)->std::string {
                    if(name.empty()) return {};
                    for(const auto& done:spec.effects) if(done.name==name) return name;
                    return {};
                };
                o.impact=keepRef(e.impact);o.death=keepRef(e.death);o.emitted=keepRef(e.emitted);
                if(mapped==mw2::Model && o.impact.empty() && o.death.empty() && o.emitted.empty()) {++report.droppedElems;--out.counts[group];continue;}
                const auto* p=e.raw.data();auto* q=o.raw.data();
                std::memcpy(q,p,152);                                   // flags .. initialRotation
                auto flags=Get<std::uint32_t>(p)&mw2::kKeptElemFlags;
                if(nonUniform) flags|=mw2::kNonUniformScale;
                Put<std::uint32_t>(q,flags);
                std::memcpy(q+mw2::kElemGravity,p+t5::kElemGravity,8);
                std::memcpy(q+mw2::kElemReflection,p+t5::kElemReflection,8);
                std::memcpy(q+mw2::kElemAtlas,p+t5::kElemAtlas,8);
                Put<std::uint16_t>(q+mw2::kElemAtlas+6,static_cast<std::uint16_t>(std::max(1,Get<std::uint16_t>(p+t5::kElemAtlas+6)&0xff)));
                q[bo1model::mw2::kElemType]=mapped;
                q[bo1model::mw2::kElemVisualCount]=mapped==mw2::Model?0:(mapped==mw2::Omni || mapped==mw2::Spot)?1:static_cast<std::uint8_t>(o.materials.size());
                q[bo1model::mw2::kElemVelCount]=p[t5::kElemVelCount];q[bo1model::mw2::kElemVisCount]=p[t5::kElemVisCount];
                for(std::size_t k=0;k<3;++k) {
                    const float lo=Get<float>(p+t5::kElemCollMins+4*k),hi=Get<float>(p+t5::kElemCollMaxs+4*k);
                    Put<float>(q+mw2::kElemCollBounds+4*k,(lo+hi)*.5f);Put<float>(q+mw2::kElemCollBounds+12+4*k,(hi-lo)*.5f);
                }
                std::memcpy(q+mw2::kElemEmitDist,p+t5::kElemEmitDist,8);
                std::memcpy(q+mw2::kElemEmitDistVariance,p+t5::kElemEmitDistVariance,8);
                q[mw2::kElemSortOrder]=p[t5::kElemSortOrder];q[mw2::kElemLightingFrac]=p[t5::kElemLightingFrac];
                o.vel=e.vel;o.vis=e.vis;
                total+=bo1model::mw2::kElemBytes+o.vel.size()+o.vis.size()+(o.materials.size()>1?8*o.materials.size():0)
                    +o.impact.size()+o.death.size()+o.emitted.size();
                out.elems.push_back(std::move(o));
                ++report.elems;
            }
            out.counts[group]+=static_cast<std::int32_t>(count);
            first+=count;
        }
        if(out.elems.empty()) {report.notes.push_back("effect "+fx.name+" has no elems left");continue;}
        out.totalSize=static_cast<std::int32_t>(total);
        spec.effects.push_back(std::move(out));
        ++report.effects;
    }
    (void)error;
    return true;
}
} // namespace mod::bo1fx
