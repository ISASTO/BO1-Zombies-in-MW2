// Execute the Test39 native pieces of the BO1 animation import against exact
// retail code, in a private Linux MS-ABI image mapped at the PE's preferred
// base (so the engine's absolute tables work): Scr_AddAnim -> Scr_GetAnim
// round trip with the anim-tree check, the script animtree index helpers,
// DB_FindXAssetEntry over a database containing one converted XAnimParts,
// and XAnimGetLength/XAnimIsLeafNode reading that converted header.
#include "script_call.hpp"
#include "native_hooks.hpp"
#include "zombie_bindings.hpp"
#include "bo1_xanim.hpp"
#include <sys/mman.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uintptr_t kBase=0x140000000;
constexpr std::size_t kSize=0x5000000,kScratch=0x4f00000;
constexpr std::uintptr_t kScrGetAnimRva=0x222840,kXAnimGetLengthRva=0x29c920,kXAnimIsLeafRva=0x29d110;
constexpr std::uintptr_t kScriptTreesRva=0x1443540,kScriptTreeCountRva=0x1443944;
constexpr std::uintptr_t kAssetHashRva=0xa78720,kAssetEntriesRva=0xa8e860;
constexpr unsigned kAssetHashSize=37000;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::uint8_t* image=nullptr;
template<class T> T& At(std::uintptr_t rva) {return *reinterpret_cast<T*>(image+rva);}
std::uintptr_t fakeTree=0;
std::uint64_t observedAnim=0;
using GetAnimFn=std::uint64_t(MOD_NATIVE_ABI*)(unsigned,std::uintptr_t);
void MOD_NATIVE_ABI ReadAnimBuiltin(unsigned) {observedAnim=reinterpret_cast<GetAnimFn>(image+kScrGetAnimRva)(0,fakeTree);}
unsigned AssetHash(int type,const char* name) {
    unsigned h=static_cast<unsigned>(type);
    for(const char* c=name;*c;++c) {
        int ch=static_cast<unsigned char>(*c);if(ch>='A' && ch<='Z')ch+=32;
        if(ch=='\\')ch='/';
        h=h*31u+static_cast<unsigned>(ch);
    }
    return h%kAssetHashSize;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        Check(input.is_open() && bytes.size()>0x4071280 && bytes.size()<kScratch,"Invalid private image");
        image=static_cast<std::uint8_t*>(mmap(reinterpret_cast<void*>(kBase),kSize,PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
        Check(image==reinterpret_cast<std::uint8_t*>(kBase),"Preferred image base unavailable");
        struct Release {void* memory;~Release(){munmap(memory,kSize);}} release{image};
        std::memcpy(image,bytes.data(),bytes.size());
        auto* scratch=image+kScratch;std::size_t used=0;
        auto alloc=[&](std::size_t n){auto* p=scratch+used;used=(used+n+15)&~std::size_t(15);return p;};

        // 1. A converted anim: two bones, no quats, no translations, 54 frames at 30 Hz.
        mod::bo1xanim::Anim a;a.name="ai_zombie_probe";a.numframes=54;a.framerate=30.f;a.frequency=30.f/54.f;
        a.boneCount={2,0,0,0,0,0,0,0,2,2};a.bones={"j_mainroot","j_spine4"};a.dataByteCount=2;a.dataByte={0,1};
        a.notify={{"fire",.4f}};a.loop=true;
        char* mw2Name=reinterpret_cast<char*>(alloc(32));std::strcpy(mw2Name,"exposed_flashbang_v4");
        mod::bo1xanim::Image converted;
        Check(mod::bo1xanim::Build(a,[](const std::string& s){return static_cast<std::uint16_t>(s.size()+100);},
            mod::bo1xanim::Address(mw2Name),converted),"Converted header not built");
        auto* parts=alloc(mod::bo1xanim::kMw2XAnimBytes);std::memcpy(parts,converted.header.data(),converted.header.size());

        // 2. The asset database finds it by name, case-insensitively.
        const auto slot=AssetHash(mod::kAssetTypeXAnimParts,mw2Name);
        At<std::uint16_t>(kAssetHashRva+2*slot)=7;
        auto* entry=image+kAssetEntriesRva+7*24;
        *reinterpret_cast<std::int32_t*>(entry)=mod::kAssetTypeXAnimParts;*reinterpret_cast<std::uint64_t*>(entry+8)=mod::bo1xanim::Address(parts);
        *reinterpret_cast<std::uint16_t*>(entry+0x12)=0;
        using FindFn=std::uintptr_t(MOD_NATIVE_ABI*)(int,const char*);
        const auto find=reinterpret_cast<FindFn>(image+mod::kDbFindXAssetEntryRva);
        Check(find(mod::kAssetTypeXAnimParts,"EXPOSED_FLASHBANG_V4")==reinterpret_cast<std::uintptr_t>(entry),"DB_FindXAssetEntry missed the converted asset");
        Check(find(mod::kAssetTypeXAnimParts,"exposed_flashbang_v5")==0 && find(4,"exposed_flashbang_v4")==0,"DB_FindXAssetEntry matched a wrong name/type");

        // 3. An XAnim_s with the converted parts at entry 17, and the engine's readers.
        auto* anims=alloc(8+20*16);std::memset(anims,0,8+20*16);
        *reinterpret_cast<std::uint32_t*>(anims)=20;
        *reinterpret_cast<std::uint16_t*>(anims+mod::kXAnimEntries+3*mod::kXAnimEntryStride)=2;      // a blend node
        *reinterpret_cast<std::uint64_t*>(anims+mod::kXAnimEntries+17*mod::kXAnimEntryStride+mod::kXAnimEntryParts)=mod::bo1xanim::Address(parts);
        using LengthFn=float(MOD_NATIVE_ABI*)(std::uintptr_t,unsigned);
        using LeafFn=bool(MOD_NATIVE_ABI*)(std::uintptr_t,unsigned);
        const float length=reinterpret_cast<LengthFn>(image+kXAnimGetLengthRva)(reinterpret_cast<std::uintptr_t>(anims),17);
        Check(std::fabs(length-54.f/30.f)<1e-5f,"XAnimGetLength did not read the converted numframes/framerate");
        Check(reinterpret_cast<LeafFn>(image+kXAnimIsLeafRva)(reinterpret_cast<std::uintptr_t>(anims),17)
            && !reinterpret_cast<LeafFn>(image+kXAnimIsLeafRva)(reinterpret_cast<std::uintptr_t>(anims),3),"XAnimIsLeafNode entry layout differs");
        // The DLL's lookup: leaf entry whose parts pointer is the replaced asset.
        int found=-1;
        for(unsigned i=0;i<20 && found<0;++i) {
            const auto* e=anims+mod::kXAnimEntries+i*mod::kXAnimEntryStride;
            if(!*reinterpret_cast<const std::uint16_t*>(e) && *reinterpret_cast<const std::uint64_t*>(e+mod::kXAnimEntryParts)==mod::bo1xanim::Address(parts))found=static_cast<int>(i);
        }
        Check(found==17,"Anim index lookup failed");

        // 4. Script animtree table: index <-> XAnim_s.
        At<std::uint64_t>(kScriptTreesRva+5*8)=reinterpret_cast<std::uint64_t>(anims);At<std::int32_t>(kScriptTreeCountRva)=6;
        using IndexFn=int(MOD_NATIVE_ABI*)(std::uintptr_t);using ByIndexFn=std::uintptr_t(MOD_NATIVE_ABI*)(int);
        Check(reinterpret_cast<IndexFn>(image+mod::kAnimTreeIndexRva)(reinterpret_cast<std::uintptr_t>(anims))==5
            && reinterpret_cast<ByIndexFn>(image+mod::kAnimTreeByIndexRva)(5)==reinterpret_cast<std::uintptr_t>(anims),"Script animtree index helpers differ");

        // 5. Scr_AddAnim -> exact Scr_GetAnim with the tree check, through CallScriptBuiltin.
        auto* tree=alloc(16);*reinterpret_cast<std::uint64_t*>(tree)=reinterpret_cast<std::uint64_t>(anims);fakeTree=reinterpret_cast<std::uintptr_t>(tree);
        auto& parameters=*reinterpret_cast<volatile mod::NativeScriptParameters*>(image+mod::kScriptParametersRva);
        auto* stack=reinterpret_cast<mod::ScriptValue*>(alloc(64*16));
        for(int n=0;n<64;++n)stack[n]={0xdeadbeef,0,0};
        stack[0].type=7;parameters.top=reinterpret_cast<std::uintptr_t>(stack);parameters.incoming=0;parameters.outgoing=0;
        mod::ScriptCallNatives natives;
        natives.addInt=reinterpret_cast<decltype(natives.addInt)>(image+mod::kScrAddIntRva);
        natives.addFloat=reinterpret_cast<decltype(natives.addFloat)>(image+mod::kScrAddFloatRva);
        natives.addString=reinterpret_cast<decltype(natives.addString)>(image+mod::kScrAddStringRva);
        natives.addVector=reinterpret_cast<decltype(natives.addVector)>(image+mod::kScrAddVectorRva);
        natives.addEntityNum=reinterpret_cast<decltype(natives.addEntityNum)>(image+mod::kScrAddEntityNumRva);
        natives.addUndefined=reinterpret_cast<decltype(natives.addUndefined)>(image+mod::kScrAddUndefinedRva);
        natives.notify=reinterpret_cast<decltype(natives.notify)>(image+mod::kScriptNotifyRva);
        natives.addAnim=reinterpret_cast<decltype(natives.addAnim)>(image+mod::kScrAddAnimRva);
        const std::uint32_t value=(5u<<16)|17u;
        const auto result=mod::CallScriptBuiltin(parameters,natives,&ReadAnimBuiltin,0,{mod::ScriptArg::Anim(value)});
        Check(result==mod::ScriptCallResult::Ok && parameters.top==reinterpret_cast<std::uintptr_t>(stack) && !parameters.incoming && !parameters.outgoing,
            "Anim argument left the parameters unbalanced");
        Check(static_cast<std::uint32_t>(observedAnim)==value,"Scr_GetAnim did not accept the pushed anim for its tree");
        mod::ScriptCallNatives noAnim=natives;noAnim.addAnim=nullptr;
        Check(mod::CallScriptBuiltin(parameters,noAnim,&ReadAnimBuiltin,0,{mod::ScriptArg::Anim(value)})==mod::ScriptCallResult::Invalid,"Anim argument accepted without Scr_AddAnim");

        std::cout<<"{\"probe\":\"exact-native level preparation\",\"converted_header_found_by_db\":true,\"db_lookup_case_insensitive\":true,"
            "\"xanim_get_length_reads_converted_header\":true,\"xanim_entry_layout\":true,\"script_animtree_index\":true,"
            "\"scr_add_anim_scr_get_anim_round_trip\":true,\"anim_value\":\"0x"<<std::hex<<value<<std::dec<<"\","
            "\"scope\":\"DB_FindXAssetEntry, XAnimGetLength, XAnimIsLeafNode, animtree index helpers, Scr_AddAnim/Scr_GetAnim with the tree check. "
            "SL_GetString, Scr_GetFunctionHandle, animscripted and XAnim sampling need the live game and are exercised only in game.\"}\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
