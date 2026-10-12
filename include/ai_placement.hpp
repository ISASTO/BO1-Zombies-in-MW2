#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace mod {
using AiPoint=std::array<float,3>;
// Native Bounds is midpoint + half size, NOT mins/maxs.
struct AiBounds { AiPoint midpoint{},halfSize{}; };
static_assert(sizeof(AiBounds)==24,"Exact native Bounds width");
inline constexpr AiBounds kAiHumanBounds{{0.f,0.f,36.f},{15.f,15.f,36.f}};
inline constexpr AiBounds kAiPointBounds{};
inline constexpr int kAiPlacementMask=0x02820015;
inline bool FinitePoint(const AiPoint& p) {
    for(float x:p) if(!std::isfinite(x) || std::abs(x)>1000000.f) return false;
    return true;
}
inline float PointDistanceSquared(const AiPoint& a,const AiPoint& b) {
    float out=0.f; for(int n=0;n<3;++n) {const float d=a[n]-b[n];out+=d*d;} return out;
}
struct AiTraceResult { bool valid=false,allSolid=false,startSolid=false; float fraction=1.f; AiPoint normal{}; };
inline AiTraceResult DecodeAiTrace(const std::array<std::uint8_t,48>& bytes) {
    AiTraceResult r;
    std::memcpy(&r.fraction,bytes.data(),4); std::memcpy(r.normal.data(),bytes.data()+4,12);
    r.allSolid=bytes[0x2c]!=0; r.startSolid=bytes[0x2d]!=0;
    r.valid=std::isfinite(r.fraction) && r.fraction>=0.f && r.fraction<=1.f
        && bytes[0x2c]<=1 && bytes[0x2d]<=1;
    for(float x:r.normal) if(!std::isfinite(x) || std::abs(x)>1.001f) r.valid=false;
    return r;
}
inline std::vector<AiPoint> SpawnCandidates(const AiPoint& center,bool friendly) {
    std::vector<AiPoint> out;
    if(!FinitePoint(center)) return out;
    if(!friendly) out.push_back(center);
    // Alternate directions at the first radius before moving farther away.
    constexpr std::array<float,3> enemyRadii{48.f,96.f,144.f},friendlyRadii{96.f,144.f,192.f};
    constexpr std::array<int,8> directions{0,4,2,6,1,5,3,7};
    for(float radius:friendly?friendlyRadii:enemyRadii) for(int n:directions) {
        const float angle=static_cast<float>(n)*.7853981633974483f;
        out.push_back({center[0]+radius*std::cos(angle),center[1]+radius*std::sin(angle),center[2]});
    }
    return out;
}
struct AiPlacement { AiPoint position{}; int attempts=0; };
template<class Trace> std::optional<AiPlacement> FindSpawnPlacement(const AiPoint& center,bool friendly,Trace trace) {
    int attempts=0;
    for(const auto& candidate:SpawnCandidates(center,friendly)) {
        ++attempts;
        AiPoint start=candidate,end=candidate; start[2]+=32.f; end[2]-=160.f;
        const auto ground=trace(start,end,kAiPointBounds);
        if(!ground.valid || ground.allSolid || ground.startSolid || ground.fraction>=1.f || ground.normal[2]<.7f) continue;
        AiPoint feet=candidate; feet[2]=start[2]+(end[2]-start[2])*ground.fraction+1.f;
        if(!FinitePoint(feet) || (friendly && std::abs(feet[2]-center[2])>64.f)) continue;
        const auto body=trace(feet,feet,kAiHumanBounds);
        if(!body.valid || body.allSolid || body.startSolid || body.fraction<1.f) continue;
        return AiPlacement{feet,attempts};
    }
    return {};
}
template<class Trace> std::optional<AiPlacement> FindRecoveryPlacement(const AiPoint& previous,const AiPoint& player,Trace trace) {
    if(FinitePoint(previous) && FinitePoint(player) && PointDistanceSquared(previous,player)<=2048.f*2048.f)
        if(const auto result=FindSpawnPlacement(previous,false,trace))return result;
    return FindSpawnPlacement(player,true,trace);
}
} // namespace mod
