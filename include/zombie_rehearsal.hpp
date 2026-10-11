#pragma once
#include "bo1_round_controller.hpp"
#include <cstdint>
#include <cstring>

namespace mod {
struct RehearsalClockSample {
    bool ready=false;
    std::uintptr_t client=0;
    int commandTime=0;
    const char* map="";
};
// The confirmed IW4 player command time is a translated clock, not a claim
// that the BO1 VM or actor layout exists in this executable.
class RehearsalClock {
public:
    bool begin(const RehearsalClockSample& sample) {
        active_=false;
        if(!valid(sample)) return false;
        client_=sample.client; time_=sample.commandTime;
        std::strcpy(map_,sample.map); active_=true; return true;
    }
    bo1::StopReason observe(const RehearsalClockSample& sample) {
        using bo1::StopReason;
        StopReason reason=StopReason::None;
        if(!active_ || !valid(sample)) reason=StopReason::LevelUnavailable;
        else if(std::strcmp(map_,sample.map)!=0) reason=StopReason::MapChanged;
        else if(client_!=sample.client) reason=StopReason::PlayerChanged;
        else if(sample.commandTime<time_) reason=StopReason::ClockRewind;
        if(reason!=StopReason::None) active_=false;
        else time_=sample.commandTime;
        return reason;
    }
    void clear() { active_=false; }
private:
    static bool valid(const RehearsalClockSample& sample) {
        return sample.ready && sample.client && sample.commandTime>=0 && sample.map
            && sample.map[0] && std::strlen(sample.map)<96;
    }
    bool active_=false;
    std::uintptr_t client_=0;
    int time_=0;
    char map_[96]{};
};
struct ZombieRehearsalStatus {
    bo1::ControllerStatus controller;
    bool autoClear=false,ready=false;
    int virtualSpawner=-1;
    static constexpr int virtualSpawners=4;
};
} // namespace mod
