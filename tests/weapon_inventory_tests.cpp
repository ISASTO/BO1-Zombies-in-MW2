#include "weapon_inventory.hpp"
#include "core.hpp"
#include <iostream>
#include <cstdlib>
void Check(bool ok,const char* text) { if (!ok) { std::cerr<<text<<'\n'; std::exit(1); } }
int main() {
    using namespace mod;
    InventoryData base; base.weapons[0]=4; base.metadata[0][2]=2; base.current=4;
    base.reserve[0]={8,19}; base.clips[0]={9,{3,0}};
    WeaponCandidate gun; gun.weapon.valid=true; gun.weapon.index=5; gun.weapon.type=0;
    gun.modelsValid=true; gun.inventoryType=0; gun.weaponClass=1;
    gun.ammoId=10; gun.clipId=11; gun.clipSize=30;
    Check(UsableGun(gun),"usable loaded primary gun accepted");
    auto invisible=gun; invisible.modelsValid=false; Check(!UsableGun(invisible),"missing player model rejected");
    auto alt=gun; alt.inventoryType=3; Check(!UsableGun(alt),"alternate/scripted inventory rejected");
    alt=gun; alt.alternate=7; Check(!UsableGun(alt),"unimplemented alternate-weapon chains are excluded");
    auto bad=gun; bad.clipId=0; Check(!UsableGun(bad),"unresolved ammo pool rejected");
    auto plan=BuildWeaponBank(base,{gun},0);
    Check(plan.added==1 && plan.data.weapons[0]==4 && plan.data.weapons[1]==5,"original equipment preserved when granting");
    Check(plan.data.metadata[0][2]==2 && plan.data.metadata[1][2]==0,"existing model selection preserved, new model verified at zero");
    Check(plan.data.reserve[1].id==10 && plan.data.reserve[1].count==150,"new reserve pool initialized with ammo");
    Check(plan.data.clips[1].id==11 && plan.data.clips[1].count[0]==30,"new magazine initialized");
    auto natural=base; natural.weapons[0]=gun.weapon.index; natural.metadata[0][1]=1;
    natural.reserve[0]={gun.ammoId,19}; natural.clips[0]={gun.clipId,{3,2}};
    Check(BuildWeaponBank(natural,{gun},0).data.clips[0].count[1]==30,"original akimbo gun retains a usable left magazine");
    InventoryOwnership naturalOwner; naturalOwner.capture(0x1000,100,"estate",natural);
    naturalOwner.applied=BuildWeaponBank(natural,{gun},0).data;
    Check(naturalOwner.original.metadata[0][1]==1 && naturalOwner.original.clips[0].count[1]==2,
        "bank refill leaves the naturally dual original undo baseline intact");
    std::array<DualSlotOwnership,15> dualSlots{}; std::array<DualAmmoOwnership,15> dualAmmo{};
    Check(!HasDualPlayerEdits(dualSlots,dualAmmo,false),"unmodified player needs no dual rollback");
    dualAmmo[14]={true,gun.clipId,2};
    Check(HasDualPlayerEdits(dualSlots,dualAmmo,false),"last-slot pending ammo restore blocks a new bank baseline");
    dualAmmo={}; dualSlots[0]={true,gun.weapon.index,0};
    Check(HasDualPlayerEdits(dualSlots,dualAmmo,false),"pending dual flag restore blocks a new bank baseline");
    dualSlots={}; Check(HasDualPlayerEdits(dualSlots,dualAmmo,true),"pending hand restore also blocks capture");
    InventoryOwnership owner; owner.capture(0x1000,100,"estate",base); owner.applied=plan.data;
    auto live=plan.data; live.clips[1].count[0]=12; live.current=5;
    Check(owner.sameLifetime(0x1000,200,"estate") && owner.sameWeapons(live),"normal firing/switching allows reversible mode");
    Check(!owner.sameLifetime(0x1000,50,"estate") && !owner.sameLifetime(0x1000,200,"cliffhanger"),"checkpoint rewind/map transition blocks stale snapshot");
    Check(!owner.sameLifetime(0x2000,200,"estate"),"reused player slot must not inherit old inventory");
    Check(owner.sameScope(0x1000,"estate"),"checkpoint rewind keeps the same validated campaign scope");
    live.weapons[1]=6; Check(!owner.sameWeapons(live),"mission/pickup inventory changes prevent blind restoration");
    std::vector<WeaponCandidate> guns;
    for (unsigned n=0;n<30;++n) { auto w=gun; w.weapon.index=20+n; w.ammoId=10; w.clipId=11; guns.push_back(w); }
    auto second=BuildWeaponBank(base,guns,1);
    Check(second.pages==3 && second.added==14 && second.data.weapons[1]==34,"native fifteen-slot limit uses bounded banks");
    InventoryOwnership session; session.capture(0x1000,100,"estate",base);
    auto first=BuildWeaponBank(base,guns,0); session.recordApplied(first.data);
    session.recordApplied(second.data);
    auto checkpoint=first.data; checkpoint.current=20;
    auto recovered=BuildInventoryRestore(session,checkpoint);
    Check(recovered.weapons==base.weapons && recovered.current==base.current,
        "previous-bank checkpoint restores the original loadout, not a full modded baseline");
    Check(BuildWeaponBank(recovered,guns,1).pages==3,
        "checkpoint recovery retains free slots and all bank pages");
    std::swap(checkpoint.weapons[0],checkpoint.weapons[1]);
    Check(BuildInventoryRestore(session,checkpoint).weapons==base.weapons,
        "slot reorder does not defeat ID-based bank undo");
    checkpoint.weapons[14]=99; checkpoint.metadata[14][2]=3;
    checkpoint.reserve[14]={99,17}; checkpoint.clips[14]={99,{2,0}};
    auto mixed=BuildInventoryRestore(session,checkpoint);
    Check(OwnsWeapon(mixed,99) && !OwnsWeapon(mixed,20) && !OwnsWeapon(mixed,21),
        "foreign pickup is retained while introduced bank weapons are removed");
    Check(mixed.reserve[14].id==99 && mixed.reserve[14].count==17 && mixed.clips[14].id==99,
        "foreign ammo identities survive selective undo");
    Check(DualPlan(true,true,true,false,false).fallback && DualPlan(true,true,true,false,false).ready,
        "campaign veto with available normal animations uses compatible fallback");
    Check(!DualPlan(true,false,true,false,false).ready && !DualPlan(true,true,false,false,false).ready,
        "missing animation names or unregistered native definition cannot force dual");
    Check(DualPlan(true,true,true,true,true).ready && !DualPlan(true,true,true,true,true).fallback,
        "authored paired animations remain the preferred path");
    std::array<std::uintptr_t,kWeaponAnimCount> nativeNames{};
    nativeNames.fill(0x2000); nativeNames[0]=0; nativeNames[1]=0x1000;
    auto names=[](std::uintptr_t at,char* out,std::size_t) {
        if(!at) return false;
        out[0]=at==0x1000?'i':'\0'; out[1]='\0'; return true;
    };
    Check(UsableAnimationNames(nativeNames,names),"native unused zero slot and optional empty names use idle fallback");
    nativeNames[1]=0x2000;
    Check(!UsableAnimationNames(nativeNames,names),"native idle name must be present");
    nativeNames[1]=0x1000; nativeNames[20]=0;
    Check(!UsableAnimationNames(nativeNames,names),"referenced animation-name pointer must remain readable");
    WeaponBankPreview display;
    Check(!display.known && display.needsRefresh(base,1),"initial bank display starts unknown instead of publishing one fake page");
    const auto initialPlan=BuildWeaponBank(base,guns,0);
    display.publish(base,1,initialPlan.pages);
    Check(display.pages==3 && !display.needsRefresh(base,1),"catalog pages publish before enabling or pressing an arrow");
    Check(display.needsRefresh(base,2),"map/catalog generation forces page refresh");
    auto pickup=base; pickup.weapons[1]=20;
    Check(display.needsRefresh(pickup,1),"inventory slot changes refresh pages without rereading ammo every update");
    auto ammoOnly=base; ammoOnly.reserve[0].count=4;
    Check(!display.needsRefresh(ammoOnly,1),"ordinary ammo usage does not trigger a catalog pass");
    InventoryData full=base;
    for (int n=0;n<15;++n) { full.reserve[n]={unsigned(60+n),1}; full.clips[n]={unsigned(80+n),{1,0}}; }
    Check(BuildWeaponBank(full,{gun},0).added==0,"full ammo tables never overwrite slot zero");
    MenuState m; m.visible=true; m.page=Page::Weapons; m.selected=0;
    Check(m.input(Input::Right)==Action::NextWeaponBank,"weapon bank arrow routed"); m.selected=6;
    Check(m.input(Input::Select)==Action::ToggleDualWield,"dual wield toggle routed");
    std::cout<<"Weapon eligibility, ammo allocation, bank bounds and reversible lifetime checks passed\n";
}
