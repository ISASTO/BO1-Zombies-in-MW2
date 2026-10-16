#pragma once
#include "bo1_round_rules.hpp"
#include <array>
#include <cstdint>
#include <cmath>
#include <functional>
#include <optional>

namespace mod::bo1 {
// Owned compatibility logic derived from the owner's patched GSC control flow.
// This model never reads engine memory, creates an actor, or applies damage.
inline constexpr std::int64_t kFirstIntroMilliseconds=8250;
inline constexpr std::int64_t kLaterIntroMilliseconds=2500;
inline constexpr std::int64_t kCapacityPollMilliseconds=100;
inline constexpr std::int64_t kRoundPollMilliseconds=1000;
inline std::int64_t SpawnDelayMilliseconds(float seconds) {
    return static_cast<std::int64_t>(std::ceil(static_cast<double>(seconds)*1000.0));
}
enum class RoundPhase { Stopped, Intro, Spawning, Draining, Between, Complete };
enum class StopReason { None, Manual, ClockRewind, LevelUnavailable, MapChanged, PlayerChanged, RoundBound };
inline const char* PhaseText(RoundPhase phase) {
    switch(phase) {
        case RoundPhase::Stopped: return "STOPPED";
        case RoundPhase::Intro: return "INTRO";
        case RoundPhase::Spawning: return "SPAWNING";
        case RoundPhase::Draining: return "DRAINING";
        case RoundPhase::Between: return "BETWEEN";
        case RoundPhase::Complete: return "COMPLETE";
    }
    return "UNKNOWN";
}
inline const char* ReasonText(StopReason reason) {
    switch(reason) {
        case StopReason::None: return "NONE";
        case StopReason::Manual: return "MANUAL";
        case StopReason::ClockRewind: return "CLOCK_REWIND";
        case StopReason::LevelUnavailable: return "LEVEL_UNAVAILABLE";
        case StopReason::MapChanged: return "MAP_CHANGED";
        case StopReason::PlayerChanged: return "PLAYER_CHANGED";
        case StopReason::RoundBound: return "ROUND_BOUND";
    }
    return "UNKNOWN";
}
struct ActorTicket {
    std::uint64_t session=0,sequence=0;
    friend bool operator==(ActorTicket a,ActorTicket b) { return a.session==b.session && a.sequence==b.sequence; }
    friend bool operator!=(ActorTicket a,ActorTicket b) { return !(a==b); }
};
struct SpawnRequest { ActorTicket ticket; int round=0,health=0; bool dogs=false; };
// A round that does not follow the normal solo rules (a hellhound round).
// _zombiemode_ai_dogs.gsc: fixed total and health, at most `concurrent`
// alive, a decreasing per-spawn wait, a 2 s capacity poll and an 8 s
// aftermath (last dog down, max ammo, intermission) before the 10 s break.
struct RoundPlan {
    int total=0,health=0,concurrent=kConcurrentLimit;
    bool dogs=false;
    float dogBaseWait=0.f;
    std::int64_t capacityPoll=kCapacityPollMilliseconds,endDelay=0;
};
using RoundPlanner=std::function<std::optional<RoundPlan>(int round,bool first)>;
enum class Retirement { Killed, Recycle };
struct ControllerStatus {
    RoundPhase phase=RoundPhase::Stopped;
    StopReason reason=StopReason::None;
    int round=0,total=0,health=0,queued=0,alive=0,killed=0;
    bool pending=false,spawnEnabled=true,dogs=false;
    int concurrent=kConcurrentLimit;
    float interval=kInitialSpawnInterval;
    std::uint64_t session=0,requested=0,confirmed=0,failed=0,recycled=0;
    ActorTicket pendingTicket;
    std::int64_t waitMilliseconds=0;
    bool running() const { return phase!=RoundPhase::Stopped && phase!=RoundPhase::Complete; }
};
// Mirrors the one additional RandomInt call when the previous spawner repeats.
// A second identical draw is valid. Zone eligibility must be supplied by a map adapter.
inline std::optional<int> ChooseSpawner(int count,int previous,int first,int retry) {
    if(count<=0 || first<0 || first>=count) return {};
    if(previous>=0 && first==previous) {
        if(retry<0 || retry>=count) return {};
        return retry;
    }
    return first;
}
class RoundController {
public:
    const ControllerStatus& status() const { return status_; }
    // Optional per-round override; the normal solo rules apply when it
    // returns nothing. Set before start(); kept across stop()/start().
    void setPlanner(RoundPlanner planner) { planner_=std::move(planner); }
    bool start(int round,std::int64_t now) {
        if(status_.running() || now<0 || !EvaluateNormalSoloRound(round,round==1)) return false;
        ++session_;
        status_={}; status_.session=session_;
        actors_={}; sequence_=0; lastTime_=now;
        beginRound(round,now,round==1);
        return true;
    }
    void stop(StopReason reason) {
        status_.phase=RoundPhase::Stopped; status_.reason=reason;
        status_.queued=0; status_.alive=0; status_.pending=false;
        status_.pendingTicket={}; status_.waitMilliseconds=0; actors_={};
    }
    void setSpawnEnabled(bool enabled) { status_.spawnEnabled=enabled; }
    std::optional<SpawnRequest> tick(std::int64_t now) {
        if(!status_.running()) return {};
        if(now<lastTime_) { stop(StopReason::ClockRewind); return {}; }
        lastTime_=now;
        if(status_.phase==RoundPhase::Between) {
            status_.waitMilliseconds=remaining(deadline_,now);
            if(now<deadline_) return {};
            beginRound(status_.round+1,now,false);
        }
        if(status_.phase==RoundPhase::Intro) {
            status_.waitMilliseconds=remaining(deadline_,now);
            if(now<deadline_) return {};
            status_.phase=RoundPhase::Spawning;
            nextSpawn_=now; nextRoundPoll_=now+kRoundPollMilliseconds;
        }
        if(now>=nextRoundPoll_) {
            nextRoundPoll_=now+kRoundPollMilliseconds;
            if(status_.alive==0 && status_.queued==0 && !status_.pending) {
                if(status_.round==kPreviewMaxRound) {
                    status_.phase=RoundPhase::Complete; status_.reason=StopReason::RoundBound;
                    status_.waitMilliseconds=0; return {};
                }
                status_.phase=RoundPhase::Between;
                deadline_=now+endDelay_+kBetweenRoundSeconds*1000;
                status_.waitMilliseconds=endDelay_+kBetweenRoundSeconds*1000;
                return {};
            }
        }
        status_.waitMilliseconds=remaining(nextSpawn_,now);
        if(status_.pending || now<nextSpawn_) return {};
        if(status_.queued<=0 || status_.alive>=status_.concurrent || !status_.spawnEnabled) {
            nextSpawn_=now+capacityPoll_;
            status_.waitMilliseconds=capacityPoll_; return {};
        }
        status_.pending=true; status_.pendingTicket={session_,++sequence_}; ++status_.requested;
        return SpawnRequest{status_.pendingTicket,status_.round,status_.health,status_.dogs};
    }
    bool confirm(ActorTicket ticket,bool created,std::int64_t now) {
        if(!status_.running() || !status_.pending || ticket!=status_.pendingTicket || now<lastTime_) return false;
        if(created) {
            if(status_.queued<=0 || status_.alive>=status_.concurrent) return false;
            bool stored=false;
            for(auto& slot:actors_) if(!slot.sequence) { slot=ticket; stored=true; break; }
            if(!stored) return false;
            --status_.queued; ++status_.alive; ++status_.confirmed; ++spawnedThisRound_;
            if(!status_.queued) status_.phase=RoundPhase::Draining;
        } else ++status_.failed;
        status_.pending=false; status_.pendingTicket={}; lastTime_=now;
        // The source waits after failed and successful attempts. Additional VM/network
        // frame quantization is adapter-specific, not guessed in this millisecond model.
        // waiting_for_next_dog_spawn: base - count/max seconds, count = dogs so far.
        const float wait=status_.dogs?dogBaseWait_-static_cast<float>(spawnedThisRound_)/static_cast<float>(status_.total>0?status_.total:1)
            :status_.interval;
        nextSpawn_=now+SpawnDelayMilliseconds(wait>0.f?wait:0.f);
        status_.waitMilliseconds=remaining(nextSpawn_,now);
        return true;
    }
    bool retire(ActorTicket ticket,Retirement reason) {
        if(!status_.running() || !ticket.sequence) return false;
        for(auto& slot:actors_) if(slot==ticket) {
            slot={}; --status_.alive;
            if(reason==Retirement::Recycle) { ++status_.queued; ++status_.recycled; status_.phase=RoundPhase::Spawning; }
            else ++status_.killed;
            return true;
        }
        return false;
    }
    bool retireOldest(Retirement reason) {
        ActorTicket oldest{};
        for(const auto slot:actors_) if(slot.sequence && (!oldest.sequence || slot.sequence<oldest.sequence)) oldest=slot;
        return retire(oldest,reason);
    }
    // Abandon obligations that cannot be spawned (e.g. hellhounds refused by
    // the engine). They count as killed so the round can end normally.
    int cancelQueued() {
        if(!status_.running() || status_.queued<=0) return 0;
        const int count=status_.queued; status_.killed+=count; status_.queued=0;
        if(status_.phase==RoundPhase::Spawning) status_.phase=RoundPhase::Draining;
        return count;
    }
    int retireAll(Retirement reason) {
        int count=0;
        for(const auto slot:actors_) if(slot.sequence && retire(slot,reason)) ++count;
        return count;
    }
    bool invariant() const {
        int live=0;
        for(const auto slot:actors_) if(slot.sequence) { if(slot.session!=session_) return false; ++live; }
        if(live!=status_.alive || live<0 || live+(status_.pending?1:0)>kConcurrentLimit || status_.concurrent>kConcurrentLimit) return false;
        if(status_.running() || status_.phase==RoundPhase::Complete)
            return status_.queued>=0 && status_.killed>=0 && status_.queued+live+status_.killed==status_.total;
        return live==0 && !status_.pending && !status_.queued;
    }
private:
    static std::int64_t remaining(std::int64_t until,std::int64_t now) { return until>now?until-now:0; }
    void beginRound(int round,std::int64_t now,bool first) {
        const auto rules=*EvaluateNormalSoloRound(round,first);
        const auto plan=planner_?planner_(round,first):std::optional<RoundPlan>{};
        status_.phase=RoundPhase::Intro; status_.reason=StopReason::None;
        status_.round=round; status_.total=rules.total; status_.queued=rules.total;
        status_.health=rules.health; status_.alive=0; status_.killed=0;
        status_.interval=rules.spawnInterval; actors_={};
        status_.dogs=false; status_.concurrent=kConcurrentLimit;
        capacityPoll_=kCapacityPollMilliseconds; endDelay_=0; dogBaseWait_=0.f; spawnedThisRound_=0;
        if(plan && plan->total>0 && plan->health>0 && plan->concurrent>0 && plan->concurrent<=kConcurrentLimit) {
            status_.total=plan->total; status_.queued=plan->total; status_.health=plan->health;
            status_.dogs=plan->dogs; status_.concurrent=plan->concurrent;
            capacityPoll_=plan->capacityPoll>0?plan->capacityPoll:kCapacityPollMilliseconds;
            endDelay_=plan->endDelay>0?plan->endDelay:0; dogBaseWait_=plan->dogBaseWait;
        }
        deadline_=now+(first?kFirstIntroMilliseconds:kLaterIntroMilliseconds);
        status_.waitMilliseconds=remaining(deadline_,now);
    }
    ControllerStatus status_;
    std::array<ActorTicket,kConcurrentLimit> actors_{};
    std::uint64_t session_=0,sequence_=0;
    std::int64_t lastTime_=0,deadline_=0,nextSpawn_=0,nextRoundPoll_=0;
    std::int64_t capacityPoll_=kCapacityPollMilliseconds,endDelay_=0;
    float dogBaseWait_=0.f;
    int spawnedThisRound_=0;
    RoundPlanner planner_;
};
} // namespace mod::bo1
