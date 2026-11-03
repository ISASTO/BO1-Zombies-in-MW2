#pragma once
// Test46: what a zombies level keeps quiet. MW2's dialogue (Spec Ops radio
// lines, battle chatter, face/pain lines, scripted level dialogue) plays at
// volume 0 on a zombies level; everything else (the map's ambience, weapons,
// BO1's own sounds) is untouched.
#include <cctype>
#include <cstddef>
#include <string_view>

namespace mod::quiet {
inline char Lower(char c) {return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
inline bool StartsWith(std::string_view text,std::string_view prefix) {
    if(text.size()<prefix.size()) return false;
    for(std::size_t i=0;i<prefix.size();++i) if(Lower(text[i])!=Lower(prefix[i])) return false;
    return true;
}
inline bool Contains(std::string_view text,std::string_view part) {
    if(part.empty() || text.size()<part.size()) return false;
    for(std::size_t i=0;i+part.size()<=text.size();++i) if(StartsWith(text.substr(i),part)) return true;
    return false;
}
// MW2 keeps every dialogue file under "voiceovers/".
inline bool DialogueFile(std::string_view path) {return Contains(path,"voiceovers/") || StartsWith(path,"voiceovers");}
// Alias names MW2 builds for dialogue: battle chatter "<CC>_<n>_..." with the
// battlechatter.gsc country IDs, flavor bursts "FB_<CC>_...", face.gsc's
// "generic_<type>_<voice>_<n>", the Spec Ops task force lines "so_..." and
// its arcade-mode cue sounds.
inline bool DialogueAlias(std::string_view name) {
    static constexpr std::string_view kCountries[]={"uk","us","ns","tf","ss","ru","ab","ge","sp","it","pg","sc"};
    for(const auto c:kCountries)
        if(name.size()>c.size()+2 && StartsWith(name,c) && name[c.size()]=='_' && std::isdigit(static_cast<unsigned char>(name[c.size()+1]))) return true;
    return StartsWith(name,"fb_") || StartsWith(name,"generic_") || StartsWith(name,"so_") || StartsWith(name,"arcademode_");
}
inline bool Dialogue(std::string_view aliasName,std::string_view filePath) {return DialogueAlias(aliasName) || DialogueFile(filePath);}
} // namespace mod::quiet
