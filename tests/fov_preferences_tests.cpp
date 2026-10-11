#include "fov_preferences.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <array>
#include <cstring>

void Check(bool value,const char* message) {
    if(!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    using namespace mod;
    Check(ParseFovPreference("version=1\nfov=90\n")==90, "load saved FOV for a fresh process");
    Check(ParseFovPreference("version=1\r\nfov=65\r\n")==65, "Windows text line endings");
    Check(SerializeFovPreference(90)=="version=1\nfov=90\n", "stable minimal own configuration");
    for(const char* s:{"","version=2\nfov=90\n","version=1\nfov=nan\n","version=1\nfov=9999999999999999\n",
        "version=1\nfov=55\n","version=1\nfov=64\n","version=1\nfov=90\nfov=100\n","version=1\nfov=90evil\n"})
        Check(!ParseFovPreference(s),"invalid or ambiguous configuration is ignored");
    Check(SerializeFovPreference(130).empty(),"out-of-menu-range preference is never serialized");
    FovPreference p;
    Check(!p.desired && !p.saved,"default game state is not overridden on a clean install");
    p.load(90);Check(p.selected==90 && p.saved==90 && p.desired==90,"restart restores saved desire");
    p.step(1);Check(p.selected==95 && p.saved==90 && p.desired==95,"session edit does not silently rewrite saved value");
    p.savedSuccessfully();Check(p.saved==95,"only successful save commits the preference");
    for(int i=0;i<40;++i)p.step(1);
    Check(p.selected==120 && !p.step(1),"upper selector bound clamps");
    for(int i=0;i<40;++i)p.step(-1);
    Check(p.selected==60 && !p.step(-1),"lower selector bound clamps");
    p.defaultSuccessfullyCleared();Check(!p.saved && p.selected==65 && p.desired==65 && p.releaseAfterDefault,"clear queues native default once");
    p.applied();Check(!p.desired && !p.releaseAfterDefault,"default restore releases recurring override");
    p.load(90);p.defaultSuccessfullyCleared();p.step(1);p.applied();
    Check(p.desired==70 && !p.releaseAfterDefault,"new edit supersedes pending reset");
    Check(FovWriteAllowed(true,true,true,65.f,90),"known live field can be edited");
    Check(!FovWriteAllowed(false,true,true,65.f,90) && !FovWriteAllowed(true,false,true,65.f,90)
          && !FovWriteAllowed(true,true,false,65.f,90),"hash/resolution/live-player gates remain mandatory");
    Check(!FovWriteAllowed(true,true,true,std::numeric_limits<float>::quiet_NaN(),90)
          && !FovWriteAllowed(true,true,true,0.f,90) && !FovWriteAllowed(true,true,true,170.f,90)
          && !FovWriteAllowed(true,true,true,65.f,125),"invalid native/requested floats fail closed");
    constexpr std::uintptr_t dvar=0x1000;
    std::array<unsigned char,48> bytes{};bytes[0xc]=1;
    const float actual=87.5f;std::memcpy(bytes.data()+0x10,&actual,4);bytes[0x14]=0x7b;
    auto reader=[&](std::uintptr_t at,void* output,std::size_t size) {
        if(at<dvar || size>bytes.size() || at-dvar>bytes.size()-size)return false;
        std::memcpy(output,bytes.data()+at-dvar,size);return true;
    };
    auto field=ReadNativeFovValue(dvar,reader);
    Check(field.valid && field.current==87.5f,"native single float field ignores adjacent bytes");
    bytes[0xc]=7;Check(!ReadNativeFovValue(dvar,reader).valid,"string dvar is never treated as a float");
    Check(!ReadNativeFovValue(0,reader).valid && !ReadNativeFovValue(0x2000,reader).valid,"missing or unreadable dvar fails closed");
    std::cout << "FOV persistence, reset, bounds and write gates passed\n";
}
