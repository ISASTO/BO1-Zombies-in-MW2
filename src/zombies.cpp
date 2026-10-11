#include "runtime.hpp"

namespace mod {
namespace {
std::mutex rehearsalMutex;
bo1::RoundController controller;
RehearsalClock clockGuard;
bool autoClear=false,ready=false;
std::int64_t nextAutoClear=0;
int previousSpawner=-1;
std::uint32_t randomState=0xB01u;

RehearsalClockSample ClockSample(const Snapshot& s) {
    return {s.supported && s.resolved && s.state==LevelState::Ready && s.client.valid,
        s.client.address,s.client.commandTime,s.map};
}
void LogState(const char* event) {
    const auto& c=controller.status();
    Log("BO1_CONTROLLER event=%s adapter=SIMULATED native_actors=0 session=%llu phase=%s reason=%s "
        "round=%d health=%d total=%d alive=%d queued=%d killed=%d pending=%d spawn_enabled=%d "
        "auto_clear=%d interval=%.9f wait_ms=%lld requested=%llu confirmed=%llu failed=%llu recycled=%llu invariant=%d",
        event,static_cast<unsigned long long>(c.session),bo1::PhaseText(c.phase),bo1::ReasonText(c.reason),
        c.round,c.health,c.total,c.alive,c.queued,c.killed,c.pending,c.spawnEnabled,autoClear,c.interval,
        static_cast<long long>(c.waitMilliseconds),static_cast<unsigned long long>(c.requested),
        static_cast<unsigned long long>(c.confirmed),static_cast<unsigned long long>(c.failed),
        static_cast<unsigned long long>(c.recycled),controller.invariant());
}
// Repeatable test draws, not a replacement for the future engine RNG/zone adapter.
int RandomVirtualSpawner() {
    randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
    return static_cast<int>(randomState%ZombieRehearsalStatus::virtualSpawners);
}
void Update(const Snapshot& s) {
    const auto sample=ClockSample(s);
    ready=sample.ready;
    if(!controller.status().running()) return;
    const auto reason=clockGuard.observe(sample);
    if(reason!=bo1::StopReason::None) {
        controller.stop(reason); previousSpawner=-1; LogState("LIFETIME_STOP"); return;
    }
    const auto oldPhase=controller.status().phase;
    const auto oldRound=controller.status().round;
    const int oldAlive=controller.status().alive;
    const auto request=controller.tick(sample.commandTime);
    if(request) {
        const int draw=RandomVirtualSpawner();
        const int retry=draw==previousSpawner?RandomVirtualSpawner():draw;
        const auto selected=bo1::ChooseSpawner(ZombieRehearsalStatus::virtualSpawners,previousSpawner,draw,retry);
        const bool accepted=selected && controller.confirm(request->ticket,true,sample.commandTime);
        if(!selected) controller.confirm(request->ticket,false,sample.commandTime);
        if(accepted) {
            previousSpawner=*selected;
            if(!oldAlive) nextAutoClear=static_cast<std::int64_t>(sample.commandTime)+1000;
        }
        Log("BO1_SPAWN adapter=SIMULATED native_actors=0 session=%llu ticket=%llu round=%d health=%d virtual_spawner=%d accepted=%d",
            static_cast<unsigned long long>(request->ticket.session),static_cast<unsigned long long>(request->ticket.sequence),
            request->round,request->health,selected.value_or(-1),accepted);
    }
    if(autoClear && controller.status().alive>0 && sample.commandTime>=nextAutoClear) {
        nextAutoClear=static_cast<std::int64_t>(sample.commandTime)+1000;
        controller.retireOldest(bo1::Retirement::Killed);
        LogState("AUTO_CLEAR_ONE");
    }
    if(controller.status().round!=oldRound) previousSpawner=-1;
    if(controller.status().phase!=oldPhase || controller.status().round!=oldRound) LogState("TRANSITION");
    if(!controller.invariant()) {
        controller.stop(bo1::StopReason::Manual); clockGuard.clear(); LogState("INVARIANT_STOP");
    }
}
} // namespace
void UpdateZombieRehearsal(const Snapshot& snapshot) {
    std::lock_guard<std::mutex> lock(rehearsalMutex); Update(snapshot);
}
void HandleZombieRehearsal(Action action,const Snapshot& snapshot,int startingRound) {
    std::lock_guard<std::mutex> lock(rehearsalMutex);
    // Recheck the exact-profile player lifetime before every debug operation.
    Update(snapshot);
    const auto sample=ClockSample(snapshot);
    if(action==Action::ToggleRehearsal) {
        if(controller.status().running()) {
            controller.stop(bo1::StopReason::Manual); clockGuard.clear(); previousSpawner=-1;
            LogState("MANUAL_END");
        } else if(clockGuard.begin(sample) && controller.start(startingRound,sample.commandTime)) {
            nextAutoClear=static_cast<std::int64_t>(sample.commandTime)+1000;
            previousSpawner=-1; randomState=0xB01u; LogState("START");
        } else Log("BO1_CONTROLLER event=START_BLOCKED adapter=SIMULATED native_actors=0 ready=%d starting_round=%d",ready,startingRound);
    } else if(action==Action::ToggleRehearsalAutoClear) {
        autoClear=!autoClear;
        nextAutoClear=static_cast<std::int64_t>(sample.commandTime)+1000;
        LogState("AUTO_CLEAR_TOGGLE");
    } else if(action==Action::ToggleRehearsalSpawning) {
        if(controller.status().running()) {
            controller.setSpawnEnabled(!controller.status().spawnEnabled); LogState("SPAWN_GATE_TOGGLE");
        } else LogState("SPAWN_GATE_BLOCKED");
    } else if(action==Action::KillRehearsalActor) {
        controller.retireOldest(bo1::Retirement::Killed); LogState("MANUAL_CLEAR_ONE");
    } else if(action==Action::ClearRehearsalActors) {
        controller.retireAll(bo1::Retirement::Killed); LogState("MANUAL_CLEAR_ALIVE");
    } else if(action==Action::ChangeRehearsalRound) {
        Log("BO1_CONTROLLER event=STARTING_ROUND adapter=SIMULATED native_actors=0 selected=%d current=%d",startingRound,controller.status().round);
    } else if(action==Action::WriteRehearsal) LogState("SNAPSHOT");
}
ZombieRehearsalStatus GetZombieRehearsalStatus() {
    std::lock_guard<std::mutex> lock(rehearsalMutex);
    return {controller.status(),autoClear,ready,previousSpawner};
}
void LogZombieRehearsal() {
    std::lock_guard<std::mutex> lock(rehearsalMutex); LogState("HEARTBEAT");
}
} // namespace mod
