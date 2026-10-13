#include "runtime.hpp"
#include "mp_feedback_assets.hpp"
#include "checked_memory.hpp"
#include <cmath>
#include "mp_hit_alert_pcm.hpp"
#include <mmsystem.h>
#include <fstream>
#include <vector>
#include <array>
namespace mod {
namespace {
std::atomic<bool> available{false};
HWAVEOUT output=nullptr;
struct Voice {WAVEHDR header{};std::array<std::int16_t,kMpHitAlertPcm.size()/2> pcm{};};
std::array<Voice,8> voices{};
bool soundAttempted=false;
float masterGain=1.f;
std::vector<std::uint8_t> textureBytes;
bool imageAttempted=false;
float FloatDvar(std::uintptr_t slot,const char* expected,float low,float high,float fallback) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t dvar=0,name=0;std::uint8_t type=255;float value=0;char text[64]{};
    const auto bytes=std::strlen(expected)+1;
    if(!CheckedReadMemory(base+slot,&dvar,8) || !dvar || !CheckedReadMemory(dvar,&name,8) ||
       !CheckedReadMemory(dvar+0xc,&type,1) || type!=1 || !CheckedReadMemory(name,text,bytes) || std::strcmp(text,expected) ||
       !CheckedReadMemory(dvar+0x10,&value,4) || !std::isfinite(value) || value<low || value>high)return fallback;
    return value;
}
bool SoundReady() {
    if(soundAttempted)return output!=nullptr;
    soundAttempted=true;
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;
    format.nSamplesPerSec=44100;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=88200;
    if(waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){output=nullptr;return false;}
    for(auto& voice:voices) {
        for(std::size_t n=0;n<voice.pcm.size();++n) {
            std::int16_t original;std::memcpy(&original,kMpHitAlertPcm.data()+n*2,2);
            voice.pcm[n]=static_cast<std::int16_t>(original*.93f);
        }
        voice.header.lpData=reinterpret_cast<char*>(voice.pcm.data());voice.header.dwBufferLength=kMpHitAlertPcm.size();
        if(waveOutPrepareHeader(output,&voice.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {
            for(auto& prepared:voices)if(prepared.header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(output,&prepared.header,sizeof(WAVEHDR));
            waveOutClose(output);output=nullptr;return false;
        }
    }
    return true;
}
}
bool MpFeedbackReady() {return available.load(std::memory_order_acquire);}
float MpCrosshairOffset() {return FloatDvar(0x52c6c8,"cg_crosshairVerticalOffset",-2.f,2.f,0.f);}
void PrepareMpFeedback(Overlay& overlay) {
    if(overlay.hitTexture || overlay.hitAttempted)return;
    overlay.hitAttempted=true;
    if(!imageAttempted) {
        imageAttempted=true;
        wchar_t exe[32768]{};const auto count=GetModuleFileNameW(nullptr,exe,32768);
        if(count && count<32768) {
            std::wstring path(exe);const auto slash=path.find_last_of(L"\\/");
            if(slash!=std::wstring::npos) {
                path.resize(slash+1);path+=L"bo1mw2\\assets\\damage_feedback.iwi";
                HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(file!=INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER bytes{};
                    if(GetFileSizeEx(file,&bytes) && bytes.QuadPart>=32 && bytes.QuadPart<=65536) {
                        textureBytes.resize(static_cast<std::size_t>(bytes.QuadPart));DWORD read=0;
                        if(!ReadFile(file,textureBytes.data(),static_cast<DWORD>(textureBytes.size()),&read,nullptr) || read!=textureBytes.size())textureBytes.clear();
                    }
                    CloseHandle(file);
                }
            }
        }
    }
    const auto image=ReadMpHitmarkerImage(textureBytes.data(),textureBytes.size());
    if(!image || !SoundReady()){Log("MP_HITMARKER available=0 reason=ORIGINAL_TEXTURE_OR_SOUND_UNAVAILABLE");return;}
    IDirect3DTexture9* texture=nullptr;
    if(FAILED(overlay.device->CreateTexture(image->width,image->height,1,0,D3DFMT_DXT5,D3DPOOL_MANAGED,&texture,nullptr)) || !texture)return;
    D3DLOCKED_RECT locked{};
    if(FAILED(texture->LockRect(0,&locked,nullptr,0)) || locked.Pitch<static_cast<int>(image->rowBytes)){texture->Release();return;}
    for(std::size_t row=0;row<image->rows;++row)std::memcpy(static_cast<char*>(locked.pBits)+row*locked.Pitch,
        textureBytes.data()+image->offset+row*image->rowBytes,image->rowBytes);
    texture->UnlockRect(0);overlay.hitTexture=texture;available.store(true,std::memory_order_release);
    Log("MP_HITMARKER available=1 texture=damage_feedback dimensions=36x72 hud=24x48 offset=-12,-12 fade_ms=1000 sound=MP_hit_alert pcm_bytes=2968 volume=0.93 pitch=1");
}
void InvalidateMpFeedback() {available.store(false,std::memory_order_release);}
void PlayMpHitAlert() {
    if(!MpFeedbackReady() || !output)return;
    masterGain=FloatDvar(0x1a23f20,"snd_volume",0.f,1.f,1.f);
    for(auto& voice:voices)if(!(voice.header.dwFlags&WHDR_INQUEUE)) {
        for(std::size_t n=0;n<voice.pcm.size();++n){std::int16_t original;std::memcpy(&original,kMpHitAlertPcm.data()+n*2,2);voice.pcm[n]=static_cast<std::int16_t>(original*.93f*masterGain);}
        waveOutWrite(output,&voice.header,sizeof(WAVEHDR));return;
    }
}
} // namespace mod
