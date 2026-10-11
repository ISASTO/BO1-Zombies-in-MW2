#include "player_controls.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main() {
    using namespace mod;
    PlayerPreferences fresh;
    for (int n=0;n<static_cast<int>(PlayerFeature::Count);++n)
        Check(!fresh.wanted(static_cast<PlayerFeature>(n)),"fresh process has no saved cheat requests");
    // Bug regression: OFF must clear a restored native flag without a user ON/OFF cycle.
    Check(DesiredFlags(0x10c01,3,0)==0x10c00,"OFF reconciles a restored God flag");
    Check(DesiredFlags(0x10c02,3,0)==0x10c00,"OFF clears native demigod protection too");
    Check(DesiredFlags(0x10c00,3,1)==0x10c01,"ON is idempotent and preserves campaign flags");
    Check(DesiredFlags(0x10c01,3,1)==0x10c01,"repeated ON never turns protection off");
    Check(DesiredFlags(0x201,1,0)==0x200,"one-time invulnerability cleanup preserves other script flags");
    Check(DesiredFlags(0x9,3,0)==8,"noclip cleanup preserves unrelated movement flags");
    fresh.toggle(PlayerFeature::God);
    PlayerLifetime life;
    Check(life.observe(true,0x2000,1000,"estate"),"first ready player is a new lifetime");
    Check(!life.observe(true,0x2000,1200,"estate"),"normal time advance retains ownership");
    Check(life.observe(true,0x2000,800,"estate"),"same-pointer checkpoint rewind invalidates ownership");
    Check(fresh.wanted(PlayerFeature::God),"checkpoint keeps the chosen session God state");
    life.observe(false,0,0,"");
    Check(life.observe(true,0x2000,900,"estate"),"server/loading gap invalidates ownership");
    Check(life.observe(true,0x3000,1000,"estate"),"client swap invalidates ownership");
    Check(life.observe(true,0x3000,1100,"cliffhanger"),"map change invalidates ownership");
    fresh.reset(); Check(!fresh.wanted(PlayerFeature::God),"reset clears every request");

    constexpr std::uintptr_t entity=0x1000,client=0x2000;
    std::vector<unsigned char> e(0x2a0),c(0xb000);
    auto put=[&](std::size_t off, auto value) { std::memcpy(c.data()+off,&value,sizeof(value)); };
    std::memcpy(e.data()+0x108,&client,sizeof(client));
    put(0x34,13.5f); put(0xaca4,2); put(0x4,0); put(0x14c,100); put(0x0,1000);
    put(0x10,std::uint32_t{0x201}); put(0xad54,std::uint32_t{8});
    std::uintptr_t unreadable=0;
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        if (at==unreadable) return false;
        const auto* data=at>=client?&c:&e; const auto base=at>=client?client:entity;
        if (at<base || size>data->size() || at-base>data->size()-size) return false;
        std::memcpy(out,data->data()+at-base,size); return true;
    };
    auto fields=ReadClientFields(entity,read);
    Check(fields.valid && fields.address==client && fields.otherFlags==0x201 && fields.cheatFlags==8,"verified partial client fields decode");
    put(0xaca4,1); Check(!ReadClientFields(entity,read).valid,"reject disconnected/connecting client"); put(0xaca4,2);
    put(0x4,8); Check(!ReadClientFields(entity,read).valid,"reject unknown movement type"); put(0x4,0);
    unreadable=client+0xad54; Check(!ReadClientFields(entity,read).valid,"unreadable movement flags fail closed"); unreadable=0;
    put(0x14c,0); Check(ReadClientFields(entity,read).valid,"dead client read remains distinct from validity");

    ReserveTable table{}; table[0]={12,90}; table[1]={13,0}; table[2]={0,-1};
    Check(ValidReserveTable(table),"valid stock and empty entries accepted");
    table[1].id=5000; Check(!ValidReserveTable(table),"implausible pool identity fails closed"); table[1].id=13;
    table[1].count=-1; Check(!ValidReserveTable(table),"negative occupied ammo count fails closed"); table[1].count=0;
    ReserveOwnership owned;
    Check(PackReserve({12,90})!=PackReserve({14,90}),"atomic reserve comparison includes pool identity, unlike a count-only CAS");
    Check(static_cast<std::uint32_t>(PackReserve({12,999}))==12,"refill preserves pool identity");
    owned.record(0,table[0]); table[0].count=999;
    Check(owned.canRestore(0,table[0]) && owned.entries[0].original==90,"unchanged injected count can restore baseline");
    owned.record(0,table[0]); Check(owned.entries[0].original==90,"replenishment does not capture injected count as baseline");
    table[0].count=30; Check(!owned.canRestore(0,table[0]),"engine/script count change prevents stale restore");
    table[0]={14,999}; Check(!owned.canRestore(0,table[0]),"recycled ammo entry never gets another pool's baseline");
    owned.clear(); Check(!owned.canRestore(0,{12,999}),"checkpoint invalidates previous ammo ownership");
    std::cout<<"Player state, restored-God regression, lifecycle and ammo ownership tests passed\n";
}
