#include "mp_feedback_assets.hpp"
#include <vector>
#include <iostream>
#include <cstdlib>
void Check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    std::vector<std::uint8_t> file(32+2592);std::memcpy(file.data(),"IWi\x08",4);file[8]=0x0d;
    auto word=[&](unsigned at,std::uint16_t v){std::memcpy(file.data()+at,&v,2);};word(10,36);word(12,72);word(14,1);
    for(unsigned at=16;at<32;at+=4){const auto size=static_cast<std::uint32_t>(file.size());std::memcpy(file.data()+at,&size,4);}
    auto image=mod::ReadMpHitmarkerImage(file.data(),file.size());Check(image && image->offset==32 && image->rowBytes==144 && image->rows==18,"Original MP DXT5 block layout not accepted");
    file.insert(file.begin()+32,16,0);const auto size=static_cast<std::uint32_t>(file.size());std::memcpy(file.data()+16,&size,4);
    image=mod::ReadMpHitmarkerImage(file.data(),file.size());Check(image && image->offset==48,"Small mips must not be treated as the base image");
    file[8]=0x0b;Check(!mod::ReadMpHitmarkerImage(file.data(),file.size()),"Unexpected format must not become a fabricated marker");file[8]=0x0d;
    word(10,64);Check(!mod::ReadMpHitmarkerImage(file.data(),file.size()),"Wrong original texture dimensions accepted");word(10,36);
    Check(!mod::ReadMpHitmarkerImage(file.data(),31),"Truncated IWI accepted");file.pop_back();Check(!mod::ReadMpHitmarkerImage(file.data(),file.size()),"Incomplete base level accepted");
    std::cout<<"MP texture layout and malformed asset checks passed\n";
}
