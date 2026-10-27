#pragma once
// Test40: the whole model build, independent of Windows file I/O: carve the
// honorguard zombie from an inflated zombie_theater zone, read its textures
// from the Black Ops .iwd archives (the last archive holding a name wins, as
// in the BO1 file system), and write the MW2 fastfile. bo1_models.cpp
// supplies the archives; the native tests supply in-memory ones.
#include "bo1_model.hpp"
#include "zip_reader.hpp"
#include <set>

namespace mod::bo1model {
struct Archive {
    std::string name;
    std::uint64_t size=0;
    zip::ReadAt read;
};
struct BuildReport {
    std::size_t models=0,images=0,imageBytes=0,scriptStrings=0;
    unsigned textures=0;                         // BO1 textures found
    std::vector<std::string> fallbacks,notes;
};

// images/<name>.iwi from `archives` (ordered oldest to newest).
inline std::map<std::string,Texture> ReadTextures(const std::vector<Archive>& archives,const std::set<std::string>& wanted,
                                                  std::vector<std::string>& notes) {
    std::map<std::string,Texture> found;
    for(auto it=archives.rbegin();it!=archives.rend() && found.size()<wanted.size();++it) {
        std::vector<zip::Entry> entries;std::string error;
        if(!zip::ReadDirectory(it->read,it->size,entries,&error)) {notes.push_back(it->name+": "+error);continue;}
        for(const auto& e:entries) {
            if(e.name.size()<12) continue;
            for(const auto& name:wanted) {
                if(found.count(name) || !zip::SameName(e.name,"images/"+name+".iwi")) continue;
                std::vector<std::uint8_t> bytes;Texture t;
                if(!zip::ReadEntry(it->read,it->size,e,bytes,&error)) {notes.push_back(name+": "+error);continue;}
                if(!ParseIwi13(bytes.data(),bytes.size(),t,&error)) {notes.push_back(name+": "+error);continue;}
                found.emplace(name,std::move(t));
            }
        }
    }
    return found;
}

inline bool BuildCharacterFastFile(const std::uint8_t* raw,std::size_t size,const std::vector<Archive>& archives,
                                   std::vector<std::uint8_t>& fastfile,BuildReport& report,std::string* error=nullptr) {
    using detail::Fail;
    report=BuildReport{};
    const auto strings=bo1xanim::ParseZoneStrings(raw,size);
    if(!strings) return Fail(error,"zombie_theater.ff string table unreadable");
    std::string text;
    const auto character=CarveCharacter(raw,size,strings->strings,strings->assetsOffset,&text);
    if(!character) return Fail(error,text);
    const auto& c=*character;
    std::set<std::string> wanted{c.headColor,c.headNormal,c.headSpec,c.eyeColor,c.bodyColor,c.bodyNormal,c.bodySpec};
    if(!c.headGloss.empty()) wanted.insert(c.headGloss);
    auto textures=ReadTextures(archives,wanted,report.notes);
    report.textures=static_cast<unsigned>(textures.size());
    // MW2 keeps gloss in the specular map's alpha.
    const auto spec=textures.find(c.headSpec);
    if(spec!=textures.end() && spec->second.format==kD3dDxt1) {
        Texture combined;const auto gloss=textures.find(c.headGloss);
        if(gloss!=textures.end() && CombineSpecGloss(spec->second,gloss->second,combined)) spec->second=std::move(combined);
        else if(WithGloss(spec->second,0x60,combined)) {spec->second=std::move(combined);report.notes.push_back("head gloss: constant");}
    }
    ZoneSpec zone;
    if(!CharacterSpec(c,textures,zone,&text,&report.fallbacks)) return Fail(error,text);
    ZoneResult built;
    if(!BuildZone(zone,built,&text)) return Fail(error,text);
    fastfile=FastFile(built);
    report.models=zone.models.size();report.images=zone.images.size();report.imageBytes=built.imageBytes;report.scriptStrings=built.scriptStrings;
    return true;
}
} // namespace mod::bo1model
