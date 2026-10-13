// Execute the unchanged native held-shot gate in a private Linux MS-ABI fixture.
#include "control_extensions.hpp"
#include <sys/mman.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>void Put(void* p,std::size_t at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
using Gate=bool(__attribute__((ms_abi))*)(void*,void*,int);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream stream(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),{});
        Check(stream.is_open() && bytes.size()==0x73,"Unexpected native gate size");
        auto* code=mmap(nullptr,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        Check(code!=MAP_FAILED,"mmap failed");
        std::memcpy(code,bytes.data(),bytes.size());
        const auto gate=reinterpret_cast<Gate>(code);
        std::array<char,0x400> player{},definition{};
        mod::WeaponFields weapon;weapon.valid=true;weapon.index=12;weapon.complete=0x2000;
        weapon.definition=reinterpret_cast<std::uintptr_t>(definition.data());weapon.type=0;weapon.fireMode=1;weapon.fireTime=125;
        std::strcpy(weapon.name,"test_semi");
        mod::FireModeOwnership owned;
        unsigned nativeCases=0;
        for(int hand=0;hand<2;++hand) {
            Put(definition.data(),mod::kWeaponFireMode,1);
            Put(player.data(),0x200+hand*0x1c,0);
            Check(!gate(player.data(),definition.data(),hand),"Native semi-auto must allow its first shot");++nativeCases;
            Put(player.data(),0x200+hand*0x1c,1);
            Check(gate(player.data(),definition.data(),hand),"Native semi-auto must stop repeating after one held shot");++nativeCases;
            owned.applied(weapon);weapon.fireMode=0;
            Put(definition.data(),mod::kWeaponFireMode,weapon.fireMode);
            for(int shots=1;shots<=256;++shots) {
                Put(player.data(),0x200+hand*0x1c,shots);
                Check(!gate(player.data(),definition.data(),hand),"Converted semi-auto still blocked held shots");++nativeCases;
            }
            Check(owned.canRestore(weapon),"Held definition must remain restorable");
            weapon.fireMode=owned.original;owned.clear();Put(definition.data(),mod::kWeaponFireMode,weapon.fireMode);
            Check(gate(player.data(),definition.data(),hand),"OFF must restore the native single-shot gate");++nativeCases;
        }
        for(int mode=2;mode<=4;++mode)for(int hand=0;hand<2;++hand) {
            Put(definition.data(),mod::kWeaponFireMode,mode);
            Put(player.data(),0x200+hand*0x1c,mode-1);
            Check(!gate(player.data(),definition.data(),hand),"Native burst must continue below its shot threshold");++nativeCases;
            Put(player.data(),0x200+hand*0x1c,mode);
            Check(gate(player.data(),definition.data(),hand),"Native burst threshold must remain unchanged");++nativeCases;
        }
        Check(weapon.fireTime==125,"Held-fire conversion changed the firing delay");
        munmap(code,4096);
        std::cout<<"{\"probe\":\"unchanged native held-shot gate\",\"native_rva\":\"0x83f90\",\"native_cases\":"<<nativeCases
            <<",\"both_hands_repeat\":true,\"maximum_tested_held_shots\":256,\"off_restores_single_shot\":true,"
            "\"burst_modes_unchanged\":true,\"fire_delay_preserved\":true,\"runtime_gameplay_tested\":false}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
