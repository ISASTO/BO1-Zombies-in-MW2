#include "bo1_round_controller.hpp"
#include "zombie_rehearsal.hpp"
#include <cstdlib>
#include <iostream>

void Check(bool ok,const char* why) {
    if(!ok) { std::cerr<<"FAIL: "<<why<<'\n'; std::exit(1); }
}
int main() {
    using namespace mod::bo1;
    RoundController c;
    Check(!c.start(0,0) && !c.start(101,0),"unsupported rounds fail without starting");
    Check(c.start(1,0),"normal solo start");
    Check(c.status().phase==RoundPhase::Intro && c.status().queued==6 && c.status().health==150,
        "round one uses recovered normal tuning");
    Check(!c.tick(8249) && c.status().alive==0,"full first intro precedes spawn requests");
    auto request=c.tick(8250);
    Check(request && c.status().queued==6 && c.status().pending,"request reserves capacity without spending budget");
    Check(!c.tick(8250),"one outstanding request only");
    const auto first=request->ticket;
    Check(c.confirm(first,true,8250),"successful actor creation acknowledged");
    Check(c.status().alive==1 && c.status().queued==5,"only success spends budget");
    Check(!c.confirm(first,true,8250),"duplicate acknowledgement ignored");
    Check(!c.tick(10249),"two second spawn pacing");
    request=c.tick(10250);
    Check(request && c.confirm(request->ticket,false,10250),"failed spawn acknowledged");
    Check(c.status().alive==1 && c.status().queued==5 && c.status().failed==1,"failed creation preserves budget");
    Check(!c.tick(12249),"failure still observes spawn delay");
    for(int i=0;i<5;++i) {
        const auto now=12250+i*2000;
        request=c.tick(now);
        Check(request && c.confirm(request->ticket,true,now),"remaining normal actors accepted");
    }
    Check(c.status().phase==RoundPhase::Draining && c.status().alive==6 && c.status().queued==0,
        "spent budget does not finish a living round");
    Check(c.retire(first,Retirement::Killed) && !c.retire(first,Retirement::Killed),"death notification idempotence");
    Check(c.retireOldest(Retirement::Recycle),"failsafe recycling supported by explicit callback");
    Check(c.status().alive==4 && c.status().queued==1 && c.status().killed==1,"recycle returns one obligation to queue");
    request=c.tick(22250);
    Check(request && c.confirm(request->ticket,true,22250),"requeued actor can be replaced");
    Check(c.retireAll(Retirement::Killed)==5 && c.status().killed==6,"round obligations conserved across recycling");
    c.tick(23250);
    Check(c.status().phase==RoundPhase::Between && c.status().round==1,"completion poll enters between rounds");
    c.tick(33249);
    Check(c.status().round==1,"ten full seconds between rounds");
    Check(!c.tick(33250) && c.status().round==2 && c.status().phase==RoundPhase::Intro,
        "next round has its own presentation intro");
    Check(!c.tick(35749),"later intro lasts 2.5 seconds");
    request=c.tick(35750);
    Check(request && request->round==2 && request->health==250 && c.status().queued==8,
        "second round count and health before confirmation");
    Check(c.confirm(request->ticket,true,35750),"second round begins");
    Check(!c.tick(37649),"second round interval reduces five percent");
    Check(c.tick(37650).has_value(),"second round next request at 1.9 seconds");
    const auto stale=c.status().pendingTicket;
    c.stop(StopReason::Manual);
    Check(c.status().alive==0 && c.status().queued==0 && !c.confirm(stale,true,37650),"end invalidates callbacks and clears only model actors");
    Check(c.start(10,40000),"debug round ten bounded start");
    Check(!c.confirm(first,true,40000),"previous-session callbacks cannot create a new-round actor");
    request=c.tick(42500);
    Check(request && request->health==1045 && c.status().total==33,"higher round uses same actual formulas");
    auto now=42500LL;
    while(c.status().alive<24) {
        if(request) Check(c.confirm(request->ticket,true,now),"fill simultaneous capacity");
        if(c.status().alive==24) break;
        now+=SpawnDelayMilliseconds(c.status().interval);
        request=c.tick(now);
    }
    Check(!c.tick(now+10000) && c.status().queued==9,"concurrent cap retains remaining budget");
    Check(c.retireOldest(Retirement::Killed),"capacity can be released");
    Check(!c.tick(now+10099),"capacity wait is polled at 100ms");
    Check(c.tick(now+10100).has_value(),"capacity resumes on next poll");
    c.stop(StopReason::Manual); c.start(1,0);
    c.setSpawnEnabled(false);
    Check(!c.tick(100000) && c.status().queued==6,"spawn flag gates requests");
    c.setSpawnEnabled(true);
    request=c.tick(100100);
    Check(request && c.confirm(request->ticket,true,100100),"spawn resume emits one request without a catchup burst");
    Check(!c.tick(100100),"same game time never double spawns");
    Check(!c.tick(0) && c.status().reason==StopReason::ClockRewind,"clock rewind stops the session");
    c.start(100,0); now=2500;
    while(c.status().queued>0 || c.status().alive>0) {
        request=c.tick(now);
        if(request) { c.confirm(request->ticket,true,now); c.retireAll(Retirement::Killed); }
        Check(c.invariant(),"high-round accounting invariant survives repeated callbacks");
        now+=1000;
    }
    c.tick(now);
    Check(c.status().phase==RoundPhase::Complete && c.status().round==100,"bound terminates safely instead of overflowing later formulas");
    Check(ChooseSpawner(4,-1,2,3)==2,"first random spawner");
    Check(ChooseSpawner(4,2,2,2)==2,"exactly one retry still allows a repeated spawner");
    Check(ChooseSpawner(4,2,2,1)==1 && ChooseSpawner(1,0,0,0)==0,"retry and singleton behavior");
    Check(!ChooseSpawner(0,-1,0,0) && !ChooseSpawner(4,2,4,1),"invalid random ranges fail");

    mod::RehearsalClock clock;
    Check(clock.begin({true,0x1000,50,"estate"}),"bind to an active exact-profile player");
    Check(clock.observe({true,0x1000,50,"estate"})==StopReason::None,"paused game clock remains valid");
    Check(clock.observe({true,0x1000,49,"estate"})==StopReason::ClockRewind,"checkpoint rewind invalidates model");
    clock.begin({true,0x1000,50,"estate"});
    Check(clock.observe({true,0x1000,60,"trainer"})==StopReason::MapChanged,"map changes terminate");
    clock.begin({true,0x1000,50,"estate"});
    Check(clock.observe({true,0x2000,60,"estate"})==StopReason::PlayerChanged,"client recycling terminates");
    clock.begin({true,0x1000,50,"estate"});
    Check(clock.observe({false,0,0,""})==StopReason::LevelUnavailable,"loading/death/read loss invalidates model");
    Check(!clock.begin({false,0x1000,50,"estate"}),"front-end start rejected");
    c.stop(StopReason::Manual); c.start(1,0); now=0;
    bool rejectedOne=false;
    while(c.status().phase!=RoundPhase::Complete && now<100000000) {
        request=c.tick(now);
        if(request) {
            const bool created=rejectedOne || request->ticket.sequence%7!=0;
            c.confirm(request->ticket,created,now);
            if(!created) rejectedOne=true;
        }
        if(now%300==0) c.retireOldest(Retirement::Killed);
        Check(c.invariant(),"full normal progression preserves budget and cap");
        now+=100;
    }
    Check(c.status().phase==RoundPhase::Complete && c.status().round==100 && rejectedOne,
        "normal controller reaches its bound with an injected creation failure");
    std::cout<<"All BO1 controller lifecycle, accounting, pacing, callback and clock checks passed\n";
}
