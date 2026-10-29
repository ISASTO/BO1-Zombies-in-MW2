#pragma once
// Test43: the whole sound build, independent of Windows file I/O: read the
// zombie sound banks from the inflated common_zombie and zombie_theater zones,
// decode the chosen alias lists (loaded sounds from the zones, streamed music
// from the .iwd archives), and write the MW2 fastfile. bo1_sounds.cpp
// supplies the zones and archives; the native tests supply in-memory ones.
#include "bo1_model_build.hpp"
#include "bo1_sound.hpp"
#include "zip_reader.hpp"
#include <map>
#include <set>

namespace mod::bo1sound {
// How a list is played in MW2. Positional BO1 aliases take `channel`;
// BO1's non-positional ones (stingers, music) play in 2D.
struct WantedList {
    const char* name;
    std::uint32_t channel;
    bool prefix=false;                   // every list whose name starts with `name`
    // Test45 (ambience): the MW2 list name (default: BO1's), a looping BO1
    // alias written as a one-shot (the game replays it), a volume scale, and a
    // smaller copy of a long sound (halved sample rate above maxRate, at most
    // maxSeconds with fades at both ends, mono).
    const char* rename=nullptr;
    bool loopAsOneShot=false;
    float volume=1.f;
    int maxRate=0,maxSeconds=0;
    bool mono=false;
    const char* mw2Name() const {return rename?rename:name;}
};
inline constexpr WantedList kCommonLists[]={
    {"zmb_vocals_zombie_ambience",mw2::kChannelAuto},
    {"zmb_vocals_zombie_sprint",mw2::kChannelAuto},
    {"zmb_vocals_zombie_crawler",mw2::kChannelAuto},
    {"zmb_vocals_zombie_attack",mw2::kChannelAuto},
    {"zmb_vocals_zombie_behind",mw2::kChannelAuto},
    {"zmb_vocals_zombie_death",mw2::kChannelAuto},
    {"zmb_vocals_zombie_taunt",mw2::kChannelAuto},
    {"zmb_zombie_spawn",mw2::kChannelEffects1},
    {"zmb_attack_whoosh",mw2::kChannelEffects1},
    {"fly_step_zombie_",mw2::kChannelBody,true},
    {"zmb_hellhound_prespawn",mw2::kChannelEffects1},
    {"zmb_hellhound_bolt",mw2::kChannelEffects1},
    {"zmb_hellhound_spawn",mw2::kChannelEffects1},
    {"zmb_hellhound_explode",mw2::kChannelEffects1},
    {"zmb_hellhound_vocals_amb",mw2::kChannelAuto},
    {"zmb_hellhound_vocals_close",mw2::kChannelAuto},
    {"zmb_spawn_powerup",mw2::kChannelEffects1},
    {"zmb_powerup_grabbed",mw2::kChannelEffects1},
    {"zmb_full_ammo",mw2::kChannelEffects1},
    {"zmb_insta_kill",mw2::kChannelEffects1},
    {"evt_nuke_flash",mw2::kChannelEffects1},
    {"evt_nuked",mw2::kChannelEffects1},
    {"zmb_dog_round_start",mw2::kChannelEffects1},
    // Test45: dismemberment: a head popping (zombie_head_gib) and a limb
    // torn off (the gib sound of BO1's death gibs).
    {"zmb_zombie_head_gib",mw2::kChannelEffects1},
    {"zmb_death_gibs",mw2::kChannelEffects1},
};
inline constexpr WantedList kTheaterLists[]={
    {"mus_zombie_round_start",mw2::kChannelAuto2d},
    {"mus_zombie_round_over",mw2::kChannelAuto2d},
    {"mus_zombie_dog_start",mw2::kChannelAuto2d},
    {"mus_zombie_dog_end",mw2::kChannelAuto2d},
    {"mus_zombie_game_over",mw2::kChannelAuto2d},
    // Test45: zombie ambience instead of the mission music. Kino's underscore
    // (a looping streamed track; the WaW wave loop if it is missing) as a
    // quiet bed the game replays, and Kino's eerie one-shots played now and
    // then. The bed: mono, at most 24 kHz and 150 s (about 7 MB of PCM).
    {"mus_theatre_underscore",mw2::kChannelAuto2d,false,"bo1_zombie_ambience_bed",true,.55f,24000,150,true},
    {"mus_zombie_wave_loop",mw2::kChannelAuto2d,false,"bo1_zombie_ambience_bed",true,.45f,24000,150,true},
    {"amb_wind_howl",mw2::kChannelAuto2d,false,"bo1_amb_wind_howl",false,.55f},
    {"amb_wood_groan",mw2::kChannelAuto2d,false,"bo1_amb_wood_groan",false,.55f},
    {"amb_whispers",mw2::kChannelAuto2d,false,"bo1_amb_whispers",false,.5f},
    {"amb_metal_clangs",mw2::kChannelAuto2d,false,"bo1_amb_metal_clangs",false,.5f},
};
// Test45: the announcer ("Max Ammo!", "Insta-Kill!"...): streamed from the
// localized .iwd archives, named by the localized bank zmb_theater.<language>
// in zone\<Language>\<xx>_zombie_theater.ff.
inline constexpr WantedList kAnnouncerLists[]={
    {"zmb_vox_ann_maxammo",mw2::kChannelAuto2d},
    {"zmb_vox_ann_instakill",mw2::kChannelAuto2d},
    {"zmb_vox_ann_doublepoints",mw2::kChannelAuto2d},
    {"zmb_vox_ann_nuke",mw2::kChannelAuto2d},
    {"zmb_vox_ann_carpenter",mw2::kChannelAuto2d},
    {"zmb_vox_ann_dogstart",mw2::kChannelAuto2d},
};
inline constexpr const char* kCommonBank="zmb_common.all";
inline constexpr const char* kAmbienceBedList="bo1_zombie_ambience_bed";
inline constexpr const char* kTheaterBank="zmb_theater.all";

struct SoundBuildReport {
    std::size_t lists=0,aliases=0,sounds=0,pcmBytes=0,references=0,seconds10=0;   // seconds10: audio length x10
    unsigned missingLists=0,skippedAliases=0,streamedFound=0,streamedMissing=0,decodeFailures=0;
    unsigned announcer=0,ambience=0;     // Test45: announcer and ambience lists written
    int bedMilliseconds=0;               // Test45: length of the ambience bed (0: none)
    std::string localizedBank;           // the localized bank read ("" when none)
    std::set<std::string> listNames;
    std::vector<std::string> notes;
};

namespace detail {
inline std::string MwSoundName(const std::string& file,bool downmixed) {
    std::string n="bo1/";
    for(char c:file) n+=c=='\\'?'/':static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(downmixed) n+="#mono";
    return n;
}
inline std::string IwdPath(const std::string& file) {
    std::string n;for(char c:file) n+=c=='\\'?'/':c;
    return n;
}
inline bool Matches(const WantedList& w,const std::string& name) {
    return w.prefix?name.rfind(w.name,0)==0:name==w.name;
}

// Test45: a smaller copy of a long sound (the ambience bed).
struct Shrink {bool mono=false;int maxRate=0,maxSeconds=0;
    bool any() const {return mono || maxRate>0 || maxSeconds>0;}};
inline Pcm Smaller(Pcm pcm,const Shrink& k) {
    if(k.mono) pcm=Downmix(pcm);
    while(k.maxRate>0 && pcm.rate>k.maxRate && pcm.rate>=16000) {   // halve: average pairs of frames
        const std::size_t c=static_cast<std::size_t>(pcm.channels),frames=pcm.frames()/2;
        std::vector<std::int16_t> out(frames*c);
        for(std::size_t f=0;f<frames;++f) for(std::size_t ch=0;ch<c;++ch)
            out[f*c+ch]=static_cast<std::int16_t>((static_cast<int>(pcm.samples[2*f*c+ch])+pcm.samples[(2*f+1)*c+ch])/2);
        pcm.samples=std::move(out);pcm.rate/=2;
    }
    if(k.maxSeconds>0) {
        const std::size_t c=static_cast<std::size_t>(pcm.channels),limit=static_cast<std::size_t>(k.maxSeconds)*static_cast<std::size_t>(pcm.rate);
        if(pcm.frames()>limit) {
            pcm.samples.resize(limit*c);
            // Cut: two-second fades at both ends so the replay does not click.
            const std::size_t fade=std::min<std::size_t>(limit/4,static_cast<std::size_t>(pcm.rate)*2);
            for(std::size_t f=0;f<fade;++f) for(std::size_t ch=0;ch<c;++ch) {
                const float g=static_cast<float>(f)/static_cast<float>(fade);
                pcm.samples[f*c+ch]=static_cast<std::int16_t>(pcm.samples[f*c+ch]*g);
                pcm.samples[(limit-1-f)*c+ch]=static_cast<std::int16_t>(pcm.samples[(limit-1-f)*c+ch]*g);
            }
        }
    }
    return pcm;
}

class SpecBuilder {
public:
    SpecBuilder(SoundZoneSpec& spec,SoundBuildReport& report):spec_(spec),report_(report) {}
    // Decoded PCM of a sound, shared by every alias that plays it.
    std::optional<std::size_t> sound(const SoundRef& ref,const std::uint8_t* data,const SndAsset& asset,bool positional,const Shrink& shrink={}) {
        const bool downmix=positional && asset.channels==2;
        auto name=MwSoundName(ref.file,downmix);
        if(shrink.any()) name+="#small";
        const auto found=index_.find(name);
        if(found!=index_.end()) return found->second;
        if(failed_.count(name)) return std::nullopt;
        Pcm pcm;std::string error;
        if(!Decode(asset,data,pcm,&error)) {
            failed_.insert(name);++report_.decodeFailures;
            if(report_.notes.size()<24) report_.notes.push_back(ref.file+": "+error);
            return std::nullopt;
        }
        if(downmix) pcm=Downmix(pcm);
        if(shrink.any()) pcm=Smaller(std::move(pcm),shrink);
        report_.seconds10+=pcm.frames()*10/static_cast<std::size_t>(pcm.rate);
        spec_.sounds.push_back({name,std::move(pcm)});
        index_.emplace(name,spec_.sounds.size()-1);
        return spec_.sounds.size()-1;
    }
private:
    SoundZoneSpec& spec_;
    SoundBuildReport& report_;
    std::map<std::string,std::size_t> index_;
    std::set<std::string> failed_;
};

// Adds the wanted lists of `bank` (read from `raw`) to the spec; returns how
// many MW2 lists were written.
template<std::size_t N>
inline unsigned AddBank(const Bank& bank,const std::uint8_t* raw,const WantedList (&wanted)[N],
                    const std::map<std::string,std::vector<std::uint8_t>>& streamed,SpecBuilder& sounds,
                    SoundZoneSpec& spec,SoundBuildReport& report) {
    unsigned written=0;
    for(const auto& w:wanted) {
        // A fallback (same MW2 name) is not needed once the first list was written.
        if(w.rename && report.listNames.count(w.rename)) continue;
        bool any=false;
        for(const auto& list:bank.lists) {
            if(list.name.empty() || !Matches(w,list.name)) continue;
            const std::string mwName=w.rename?std::string(w.rename):list.name;
            if(report.listNames.count(mwName)) continue;
            any=true;
            ZoneAliasList out;out.name=mwName;
            const Shrink shrink{w.mono,w.maxRate,w.maxSeconds};
            for(const auto& a:list.aliases) {
                if(a.looping() && !w.loopAsOneShot) {++report.skippedAliases;continue;}
                std::optional<std::size_t> index;
                const bool positional=a.positional() && w.channel!=mw2::kChannelAuto2d;
                if(a.sound.kind==SoundRef::Kind::Loaded && a.sound.asset.dataInline) {
                    index=sounds.sound(a.sound,raw+a.sound.asset.data,a.sound.asset,positional,shrink);
                } else if(a.sound.kind==SoundRef::Kind::Streamed) {
                    const auto file=streamed.find(IwdPath(a.sound.file));
                    if(file==streamed.end()) {++report.streamedMissing;}
                    else {
                        SndAsset asset;std::string error;
                        if(ParseStreamedFile(file->second.data(),file->second.size(),asset,&error)) {
                            ++report.streamedFound;
                            index=sounds.sound(a.sound,file->second.data()+asset.data,asset,positional,shrink);
                        } else if(report.notes.size()<24) report.notes.push_back(a.sound.file+": "+error);
                    }
                }
                if(!index) {++report.skippedAliases;continue;}
                ZoneAlias z;z.sound=*index;
                z.volMin=a.volMin()*w.volume;z.volMax=std::max(a.volMin(),a.volMax())*w.volume;
                z.pitchMin=a.pitchMin();z.pitchMax=std::max(a.pitchMin(),a.pitchMax());
                if(!(z.pitchMin>0.25f && z.pitchMax<4.f)) z.pitchMin=z.pitchMax=1.f;
                z.distMin=a.distMin();z.distMax=std::max(a.distMin()+1.f,a.distMax());
                z.startDelay=a.startDelay();
                z.flags=mw2::Flags(positional?w.channel:mw2::kChannelAuto2d);
                out.aliases.push_back(z);
            }
            if(out.aliases.empty()) {++report.missingLists;continue;}
            report.aliases+=out.aliases.size();
            report.listNames.insert(out.name);
            spec.lists.push_back(std::move(out));
            ++written;
        }
        if(!any) {++report.missingLists;if(report.notes.size()<24) report.notes.push_back(std::string("list ")+w.name+" not in "+bank.name);}
    }
    return written;
}

// The streamed files the wanted lists of `bank` play.
template<std::size_t N>
inline void StreamedFiles(const Bank& bank,const WantedList (&wanted)[N],std::set<std::string>& out) {
    for(const auto& w:wanted) for(const auto& list:bank.lists)
        if(!list.name.empty() && Matches(w,list.name))
            for(const auto& a:list.aliases) if(a.sound.kind==SoundRef::Kind::Streamed && !a.sound.file.empty()) out.insert(IwdPath(a.sound.file));
}
} // namespace detail

// Reads `wanted` entries from the archives (newest wins, as in the BO1 file system).
inline std::map<std::string,std::vector<std::uint8_t>> ReadStreamedFiles(const std::vector<bo1model::Archive>& archives,
    const std::set<std::string>& wanted,std::vector<std::string>& notes) {
    std::map<std::string,std::vector<std::uint8_t>> found;
    for(auto it=archives.rbegin();it!=archives.rend() && found.size()<wanted.size();++it) {
        std::vector<zip::Entry> entries;std::string error;
        if(!zip::ReadDirectory(it->read,it->size,entries,&error)) {if(notes.size()<24) notes.push_back(it->name+": "+error);continue;}
        for(const auto& e:entries) {
            if(e.name.size()<10 || e.size>(64u<<20)) continue;
            for(const auto& name:wanted) {
                if(found.count(name) || !zip::SameName(e.name,name)) continue;
                std::vector<std::uint8_t> bytes;
                if(!zip::ReadEntry(it->read,it->size,e,bytes,&error)) {if(notes.size()<24) notes.push_back(name+": "+error);continue;}
                found.emplace(name,std::move(bytes));
            }
        }
    }
    return found;
}

// Test45: the localized bank (zone\\<Language>\\<xx>_zombie_theater.ff,
// bank "zmb_theater.<language>") holding the announcer's alias lists.
struct LocalizedZone {const std::uint8_t* raw=nullptr;std::size_t size=0;std::string bank;};

// The spec for the wanted lists. common: inflated common_zombie zone
// (required); theater: inflated zombie_theater zone (music and ambience,
// optional); localized: the announcer (optional).
inline bool BuildSoundSpec(const std::uint8_t* common,std::size_t commonSize,const std::uint8_t* theater,std::size_t theaterSize,
                           const std::vector<bo1model::Archive>& archives,SoundZoneSpec& spec,SoundBuildReport& report,
                           std::string* error=nullptr,const LocalizedZone& localized={}) {
    using detail::Fail;
    report=SoundBuildReport{};spec=SoundZoneSpec{};
    detail::SpecBuilder sounds(spec,report);
    std::string text;
    const auto at=FindBank(common,commonSize,kCommonBank);
    if(!at) return Fail(error,std::string(kCommonBank)+" not found in common_zombie.ff");
    Bank bank;
    if(!ParseBank(common,commonSize,*at,bank,&text)) return Fail(error,std::string(kCommonBank)+": "+text);
    detail::AddBank(bank,common,kCommonLists,{},sounds,spec,report);
    Bank music,announcer;bool haveMusic=false,haveAnnouncer=false;
    if(theater && theaterSize) {
        const auto t=FindBank(theater,theaterSize,kTheaterBank);
        if(!t) report.notes.push_back(std::string(kTheaterBank)+" not found");
        else if(!ParseBank(theater,theaterSize,*t,music,&text)) report.notes.push_back(std::string(kTheaterBank)+": "+text);
        else haveMusic=true;
    }
    if(localized.raw && localized.size && !localized.bank.empty()) {
        const auto l=FindBank(localized.raw,localized.size,localized.bank);
        if(!l) report.notes.push_back(localized.bank+" not found");
        else if(!ParseBank(localized.raw,localized.size,*l,announcer,&text)) report.notes.push_back(localized.bank+": "+text);
        else {haveAnnouncer=true;report.localizedBank=localized.bank;}
    }
    // One pass over the archives for every streamed file wanted.
    std::set<std::string> wanted;
    if(haveMusic) detail::StreamedFiles(music,kTheaterLists,wanted);
    if(haveAnnouncer) detail::StreamedFiles(announcer,kAnnouncerLists,wanted);
    const auto files=wanted.empty()?std::map<std::string,std::vector<std::uint8_t>>{}:ReadStreamedFiles(archives,wanted,report.notes);
    if(haveMusic) {
        const auto before=spec.lists.size();
        detail::AddBank(music,theater,kTheaterLists,files,sounds,spec,report);
        for(std::size_t n=before;n<spec.lists.size();++n) if(spec.lists[n].name.rfind("bo1_",0)==0) ++report.ambience;
    }
    if(haveAnnouncer) report.announcer=detail::AddBank(announcer,localized.raw,kAnnouncerLists,files,sounds,spec,report);
    for(const auto& l:spec.lists) if(l.name==kAmbienceBedList && !l.aliases.empty()) {
        const auto& pcm=spec.sounds[l.aliases.front().sound].pcm;
        report.bedMilliseconds=static_cast<int>(pcm.frames()*1000/static_cast<std::size_t>(pcm.rate));
    }
    report.lists=spec.lists.size();report.sounds=spec.sounds.size();
    if(spec.lists.empty()) return Fail(error,"none of the zombie sound lists could be read");
    return true;
}

inline bool BuildSoundFastFile(const std::uint8_t* common,std::size_t commonSize,const std::uint8_t* theater,std::size_t theaterSize,
                               const std::vector<bo1model::Archive>& archives,std::vector<std::uint8_t>& fastfile,
                               SoundBuildReport& report,std::string* error=nullptr,const LocalizedZone& localized={}) {
    SoundZoneSpec spec;
    if(!BuildSoundSpec(common,commonSize,theater,theaterSize,archives,spec,report,error,localized)) return false;
    SoundZoneResult built;
    if(!BuildSoundZone(spec,built,error)) return false;
    fastfile=bo1model::FastFile(built.zone);
    report.pcmBytes=built.pcmBytes;report.references=built.references;
    return true;
}
} // namespace mod::bo1sound
