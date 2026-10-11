#include "runtime.hpp"
#include <bcrypt.h>
#include <intrin.h>
#include <array>
#include <cstdio>
#include <vector>

namespace mod {
namespace {
struct Profile {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    bool supported = false;
    bool resolved = false;
    std::uintptr_t serverDvarSlot = 0;
    std::uintptr_t mapDvarSlot = 0;
    std::uintptr_t entitySlot = 0;
    std::uintptr_t fovDvarSlot = 0;
    bool fovResolved = false;
    bool clientResolved = false;
    bool ammoResolved = false;
    std::uintptr_t sustainSlot = 0;
    bool sustainResolved = false;
    std::uintptr_t speedSlot = 0;
    bool speedResolved = false;
    std::uintptr_t jumpSlot = 0;
    std::uintptr_t jumpHeight = 0;
    std::uintptr_t jumpTwiceHeight = 0;
    bool jumpResolved = false;
    std::uintptr_t giveAllSlot=0, explosiveSlot=0, aiSlot=0, timeSlot=0, gravitySlot=0;
    std::uintptr_t gravityLiteral=0, weaponTable=0, weaponDefTable=0, weaponCount=0;
    bool giveAllResolved=false, explosiveResolved=false, aiResolved=false, timeResolved=false;
    bool gravityResolved=false, weaponResolved=false, grenadeSustainResolved=false;
    std::uintptr_t dualSlot=0;
    bool dualResolved=false,inventoryResolved=false,dualAnimResolved=false;
    std::uintptr_t weaponAnimCache=0;
} profile;
struct DualAssetOwnership {
    WeaponFields identity;
    std::uintptr_t normalTree=0;
    bool active=false,fallback=false;
    ScalarOwnership<std::uint8_t> veto;
    std::array<ScalarOwnership<std::uintptr_t>,4> pointers{};
};

struct FeatureRuntime {
    PlayerPreferences preferences;
    PlayerLifetime lifetime;
    ReserveOwnership ammo;
    bool clearInvulnerability = true;
    bool clearSustain = true;
    bool clearSpeed = true;
    ULONGLONG nextUpdate = 0;
    ScalarOwnership<std::uint8_t> sustain;
    ScalarOwnership<int> speed;
    bool jumpOwned=false;
    ExtraPreferences extra;
    ScalarOwnership<std::uint8_t> giveAll, explosive, ai;
    ScalarOwnership<float> time, gravity;
    ScalarOwnership<float> gravityLiteral;
    RapidOwnership rapid;
    InventoryOwnership inventory;
    bool inventoryReconcilePending=false;
    bool inventoryNeedsBaseline=false;
    int weaponBank=0,weaponPages=1;
    ScalarOwnership<std::uint8_t> dual;
    std::array<DualSlotOwnership,15> dualSlots{};
    std::array<DualAmmoOwnership,15> dualAmmo{};
    ScalarOwnership<int> lastHand;
    std::array<WeaponFields,kWeaponCapacity> inventoryIdentity{};
    DualAssetOwnership dualAsset;
    unsigned dualHeld=0;
    bool dualReady=false;
    struct Timing { unsigned long long updates=0,ticks=0,peak=0,reads=0; long long frequency=0; } timing;
    char dualMap[96]{};
    std::uintptr_t dualClient=0;
} features;
std::mutex featuresMutex;
struct CandidateCache {
    unsigned count=0;
    std::array<bool,kWeaponCapacity> read{};
    std::array<WeaponCandidate,kWeaponCapacity> candidates{};
    unsigned long long hits=0,misses=0;
} candidateCache;
std::mutex candidateMutex;
thread_local unsigned long long readCalls=0;
struct MeasureUpdate {
    LARGE_INTEGER start{};
    unsigned long long before=readCalls;
    MeasureUpdate() {
        QueryPerformanceCounter(&start);
        if (!features.timing.frequency) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); features.timing.frequency=f.QuadPart; }
    }
    ~MeasureUpdate() {
        LARGE_INTEGER end{}; QueryPerformanceCounter(&end);
        const auto ticks=static_cast<unsigned long long>(end.QuadPart-start.QuadPart);
        ++features.timing.updates; features.timing.ticks+=ticks;
        features.timing.peak=std::max(features.timing.peak,ticks); features.timing.reads+=readCalls-before;
    }
};

bool Read(std::uintptr_t address, void* output, std::size_t size) {
    ++readCalls;
    SIZE_T read = 0;
    return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &read) && read == size;
}
template<class T> bool ReadValue(std::uintptr_t address, T& output) { return Read(address, &output, sizeof(T)); }
bool InImage(std::uintptr_t address, std::size_t size) {
    return address >= profile.base && size <= profile.size && address - profile.base <= profile.size - size;
}
bool ReadString(std::uintptr_t address, char* result, std::size_t capacity) {
    if (!address || capacity < 2) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto available = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize - address;
    const auto count = (available < capacity ? static_cast<std::size_t>(available) : capacity);
    if (!Read(address, result, count)) return false;
    for (std::size_t i = 0; i < count; ++i) if (result[i] == '\0') return true;
    result[capacity - 1] = '\0';
    return false;
}
bool NamedDvar(std::uintptr_t slot, const char* name, std::uint8_t type, std::uintptr_t& dvar) {
    std::uintptr_t string = 0;
    std::uint8_t actualType = 255;
    char actualName[64]{};
    return ReadValue(slot, dvar) && dvar && ReadValue(dvar, string) && ReadValue(dvar + 0xc, actualType) &&
           actualType == type && ReadString(string, actualName, sizeof(actualName)) && std::strcmp(actualName, name) == 0;
}
std::string ExecutableHash() {
    wchar_t file[32768]{};
    if (!GetModuleFileNameW(nullptr, file, 32768)) return {};
    HANDLE input = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (input == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, returned = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                                  sizeof(objectLength), &returned, 0) >= 0;
    if (ok) {
        object.resize(objectLength);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    }
    unsigned char block[65536];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(input, block, sizeof(block), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, block, count, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(input);
    if (!ok) return {};
    char text[65]{};
    for (std::size_t i = 0; i < digest.size(); ++i) std::snprintf(text + i * 2, 3, "%02x", digest[i]);
    return text;
}
std::uintptr_t Unique(const std::vector<std::uint8_t>& text, std::uintptr_t start, const char* name, const char* pattern) {
    const auto matches = FindPattern(text.data(), text.size(), ParsePattern(pattern));
    if (matches.size() != 1) {
        Log("RESOLVE FAILED %s matches=%llu (expected exactly 1)", name, static_cast<unsigned long long>(matches.size()));
        return 0;
    }
    const auto at = start + matches[0];
    Log("RESOLVE %s address=%p rva=0x%llx", name, reinterpret_cast<void*>(at),
        static_cast<unsigned long long>(at - profile.base));
    return at;
}
std::uintptr_t ResolveRip(std::uintptr_t at, unsigned offset) {
    std::uint8_t bytes[7]{};
    if (!at || !Read(at + offset, bytes, sizeof(bytes))) return 0;
    return RipTarget(at + offset, bytes, 3, 7);
}
std::uintptr_t ResolveScalarRip(std::uintptr_t at, unsigned offset) {
    std::uint8_t bytes[8]{};
    return at && Read(at+offset,bytes,sizeof(bytes)) ? RipTarget(at+offset,bytes,4,8) : 0;
}
std::uintptr_t ResolveDwordRip(std::uintptr_t at,unsigned offset) {
    std::uint8_t bytes[6]{};
    return at && Read(at+offset,bytes,sizeof(bytes)) ? RipTarget(at+offset,bytes,2,6) : 0;
}
std::uintptr_t ResolveCall(std::uintptr_t at,unsigned offset) {
    std::uint8_t bytes[5]{};
    return at && Read(at+offset,bytes,sizeof(bytes)) && bytes[0]==0xe8 ? RipTarget(at+offset,bytes,1,5) : 0;
}
bool Anchor(std::uintptr_t at, unsigned offset, const char* wanted) {
    char name[64]{};
    return ReadString(ResolveRip(at, offset), name, sizeof(name)) && std::strcmp(name, wanted) == 0;
}
bool Writable(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = memory.Protect & 0xff;
    return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
FovSnapshot ReadFov() {
    FovSnapshot s;
    s.resolved=profile.supported && profile.resolved && profile.fovResolved;
    if (!s.resolved || !NamedDvar(profile.fovDvarSlot,"cg_fov",1,s.dvar)) return s;
    const auto field=ReadNativeFovValue(s.dvar,Read);
    s.valid=field.valid; s.current=field.current; return s;
}
BoolDvarSnapshot ReadSustain() {
    BoolDvarSnapshot s; s.resolved=profile.supported && profile.resolved && profile.sustainResolved;
    s.valid=s.resolved && NamedDvar(profile.sustainSlot,"player_sustainAmmo",0,s.dvar)
        && ReadValue(s.dvar+0x10,s.current) && s.current<=1;
    return s;
}
IntDvarSnapshot ReadSpeed() {
    IntDvarSnapshot s; s.resolved=profile.supported && profile.resolved && profile.speedResolved;
    s.valid=s.resolved && NamedDvar(profile.speedSlot,"g_speed",5,s.dvar)
        && ReadValue(s.dvar+0x10,s.current) && s.current>=0 && s.current<=5000;
    return s;
}
JumpSnapshot ReadJump() {
    JumpSnapshot s; s.resolved=profile.supported && profile.resolved && profile.jumpResolved;
    s.valid=s.resolved && ReadValue(profile.jumpHeight,s.height) && ReadValue(profile.jumpTwiceHeight,s.twiceHeight)
        && ValidJumpPair(s.height,s.twiceHeight);
    return s;
}
BoolDvarSnapshot ReadBoolDvar(std::uintptr_t slot,bool resolved,const char* name) {
    BoolDvarSnapshot s; s.resolved=profile.supported && profile.resolved && resolved;
    s.valid=s.resolved && NamedDvar(slot,name,0,s.dvar) && ReadValue(s.dvar+0x10,s.current) && s.current<=1;
    return s;
}
FloatDvarSnapshot ReadFloatDvar(std::uintptr_t slot,bool resolved,const char* name,float low,float high) {
    FloatDvarSnapshot s; s.resolved=profile.supported && profile.resolved && resolved;
    s.valid=s.resolved && NamedDvar(slot,name,1,s.dvar) && ReadValue(s.dvar+0x10,s.current)
        && s.current>=low && s.current<=high;
    return s;
}
WeaponFields ReadWeapon(unsigned index,bool requireTiming=true) {
    WeaponFields w; w.index=index;
    unsigned count=0; std::uintptr_t linked=0,name=0;
    if (!profile.supported || !profile.resolved || !profile.weaponResolved
        || !ReadValue(profile.weaponCount,count) || !count || count>=kWeaponCapacity || !index || index>count
        || !ReadValue(profile.weaponTable+index*8,w.complete) || !w.complete || (w.complete&7)
        || !ReadValue(profile.weaponDefTable+index*8,w.definition) || !w.definition || (w.definition&7)
        || !ReadValue(w.complete+kCompleteDefinition,linked) || linked!=w.definition
        || !ReadValue(w.complete,name) || !ReadString(name,w.name,sizeof(w.name)) || !w.name[0]
        || !ReadValue(w.complete+kWeaponFireTime,w.fireTime) || (requireTiming && (w.fireTime<1 || w.fireTime>10000))
        || !ReadValue(w.definition+kWeaponType,w.type) || w.type<0 || w.type>6) return w;
    for (const unsigned char* c=reinterpret_cast<const unsigned char*>(w.name);*c;++c)
        if (*c<32 || *c>126) return w;
    w.valid=true; return w;
}
WeaponCandidate ReadCandidateRaw(unsigned index) {
    WeaponCandidate w; w.weapon=ReadWeapon(index);
    std::uintptr_t models=0,model=0,name=0; char modelName[128]{};
    std::uint8_t noDual=255;
    if (!w.weapon.valid || !profile.inventoryResolved
        || !ReadValue(w.weapon.definition+kGunModels,models) || !models || (models&7)
        || !ReadValue(models,model) || !model || (model&7) || !ReadValue(model,name)
        || !ReadString(name,modelName,sizeof(modelName)) || !modelName[0]
        || std::strcmp(modelName,"void")==0 || std::strcmp(modelName,"default_model")==0
        || std::strcmp(w.weapon.name,"defaultweapon")==0 || std::strstr(modelName,"invisible")
        || !ReadValue(w.weapon.definition+kInventoryType,w.inventoryType)
        || !ReadValue(w.weapon.definition+kWeaponClass,w.weaponClass)
        || !ReadValue(w.weapon.definition+kAmmoIndex,w.ammoId)
        || !ReadValue(w.weapon.definition+kClipIndex,w.clipId)
        || !ReadValue(w.weapon.complete+kClipSize,w.clipSize)
        || !ReadValue(w.weapon.complete+kAlternateWeapon,w.alternate)
        || !ReadValue(w.weapon.definition+kNoDualWield,noDual) || noDual>1) return w;
    w.modelsValid=true; w.noDualWield=noDual!=0; return w;
}
WeaponCandidate ReadCandidate(unsigned index) {
    if (!index || index>=kWeaponCapacity) return {};
    unsigned count=0;
    if (!profile.inventoryResolved || !ReadValue(profile.weaponCount,count) || !count || count>=kWeaponCapacity || index>count) return {};
    std::lock_guard<std::mutex> lock(candidateMutex);
    if (candidateCache.count!=count) { candidateCache.count=count; candidateCache.read={}; }
    if (!candidateCache.read[index]) {
        candidateCache.candidates[index]=ReadCandidateRaw(index); candidateCache.read[index]=true; ++candidateCache.misses;
    } else ++candidateCache.hits;
    return candidateCache.candidates[index];
}
void InvalidateCandidates() { std::lock_guard<std::mutex> lock(candidateMutex); candidateCache.read={}; }
bool SameAsset(const WeaponFields& actual,const WeaponFields& expected) {
    return actual.valid && expected.valid && actual.index==expected.index && actual.complete==expected.complete
        && actual.definition==expected.definition && std::strcmp(actual.name,expected.name)==0;
}
bool InventoryAssetsValid() {
    for (unsigned n=1;n<kWeaponCapacity;++n) if (features.inventoryIdentity[n].valid
        && !SameAsset(ReadWeapon(n,false),features.inventoryIdentity[n])) return false;
    return true;
}
bool ReadInventory(std::uintptr_t client,InventoryData& d) {
    unsigned count=0;
    if (!profile.inventoryResolved || !client || !ReadValue(profile.weaponCount,count) || !count || count>=kWeaponCapacity
        || !Read(client+kOwnedWeapons,d.weapons.data(),sizeof(d.weapons))
        || !Read(client+kWeaponMetadata,d.metadata.data(),sizeof(d.metadata))
        || !Read(client+kReserveAmmo,d.reserve.data(),sizeof(d.reserve))
        || !Read(client+kClipAmmo,d.clips.data(),sizeof(d.clips))
        || !ReadValue(client+kCurrentWeapon,d.current) || !d.current || d.current>count || !OwnsWeapon(d,d.current)) return false;
    for (int n=0;n<15;++n) {
        if (d.weapons[n]>count || (d.weapons[n] && (d.metadata[n][1]>1 || d.metadata[n][2]>=16))) return false;
        if (d.reserve[n].id && (d.reserve[n].id>=kWeaponCapacity || d.reserve[n].count<0 || d.reserve[n].count>100000)) return false;
        if (d.clips[n].id && (d.clips[n].id>=kWeaponCapacity || d.clips[n].count[0]<0 || d.clips[n].count[0]>100000
            || d.clips[n].count[1]<0 || d.clips[n].count[1]>100000)) return false;
    }
    return true;
}
std::vector<WeaponCandidate> ReadCatalog() {
    std::vector<WeaponCandidate> result; unsigned count=0;
    if (!profile.inventoryResolved || !ReadValue(profile.weaponCount,count) || count>=kWeaponCapacity) return result;
    for (unsigned n=1;n<=count;++n) { const auto w=ReadCandidate(n); if (UsableGun(w)) result.push_back(w); }
    return result;
}
bool SetMasked(std::uintptr_t at, std::uint32_t mask, std::uint32_t wanted, const char* name) {
    if ((at&3) || !Writable(at)) return false;
    for (int attempt=0;attempt<4;++attempt) {
        std::uint32_t old=0; if (!ReadValue(at,old)) return false;
        const auto next=DesiredFlags(old,mask,wanted);
        if (next==old) return true;
        if (static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),
            static_cast<LONG>(next),static_cast<LONG>(old)))==old) {
            Log("PLAYER_APPLY %s before=0x%08x after=0x%08x",name,old,next); return true;
        }
    }
    return false;
}
bool SetInt(std::uintptr_t at, int old, int target) {
    return !(at&3) && Writable(at) && (old==target
        || InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),target,old)==old);
}
bool SetBool(std::uintptr_t at, std::uint8_t old, std::uint8_t target) {
    if (!Writable(at)) return false;
    if (old==target) return true;
#if defined(_MSC_VER)
    return static_cast<std::uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(at),
        static_cast<CHAR>(target),static_cast<CHAR>(old)))==old;
#else
    // This MinGW Windows header does not declare the byte-sized MSVC intrinsic.
    // GCC emits a locked byte compare/exchange without an imported helper.
    auto expected=old;
    return __atomic_compare_exchange_n(reinterpret_cast<volatile std::uint8_t*>(at),&expected,target,false,
        __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
#endif
}
struct InventoryChange { std::uintptr_t at; unsigned width; std::uint64_t before,after; };
bool ExchangeInventory(const InventoryChange& c,bool undo=false) {
    const auto before=undo?c.after:c.before,after=undo?c.before:c.after;
    if (c.width==1) return SetBool(c.at,static_cast<std::uint8_t>(before),static_cast<std::uint8_t>(after));
    if (c.width==4) return SetInt(c.at,static_cast<int>(before),static_cast<int>(after));
    return !(c.at&7) && Writable(c.at) && static_cast<std::uint64_t>(InterlockedCompareExchange64(
        reinterpret_cast<volatile LONG64*>(c.at),static_cast<LONG64>(after),static_cast<LONG64>(before)))==before;
}
bool WriteInventory(std::uintptr_t client,const InventoryData& before,const InventoryData& after) {
    std::vector<InventoryChange> done;
    auto edit=[&](std::size_t offset,unsigned width,std::uint64_t old,std::uint64_t target) {
        if (old==target) return true;
        const InventoryChange c{client+offset,width,old,target};
        if (!ExchangeInventory(c)) return false;
        done.push_back(c); return true;
    };
    bool ok=edit(kCurrentWeapon,4,before.current,after.current);
    for (int n=0;n<15 && ok;++n) {
        for (int b=0;b<5 && ok;++b) ok=edit(kWeaponMetadata+n*5+b,1,before.metadata[n][b],after.metadata[n][b]);
        std::uint64_t old=0,next=0;
        std::memcpy(&old,&before.reserve[n],8); std::memcpy(&next,&after.reserve[n],8);
        if (ok) ok=edit(kReserveAmmo+n*8,8,old,next);
        const auto clip=kClipAmmo+n*12;
        // Remove a disappearing pool before its counts; publish a new ID last.
        if (ok && !after.clips[n].id) ok=edit(clip,4,before.clips[n].id,0);
        for (int hand=0;hand<2 && ok;++hand)
            ok=edit(clip+4+hand*4,4,static_cast<unsigned>(before.clips[n].count[hand]),static_cast<unsigned>(after.clips[n].count[hand]));
        if (ok && after.clips[n].id) ok=edit(clip,4,before.clips[n].id,after.clips[n].id);
    }
    // Publish owned weapon IDs after model metadata and ammo are initialized.
    for (int n=0;n<15 && ok;++n) ok=edit(kOwnedWeapons+n*4,4,before.weapons[n],after.weapons[n]);
    if (!ok) {
        unsigned restored=0;
        for (auto it=done.rbegin();it!=done.rend();++it) if (ExchangeInventory(*it,true)) ++restored;
        Log("WEAPON_TRANSACTION failed changes=%llu rolled_back=%u",static_cast<unsigned long long>(done.size()),restored);
    }
    return ok;
}
bool RestoreInventory(const Snapshot& s) {
    auto& owned=features.inventory;
    if (!owned.owned) return true;
    InventoryData live;
    if (!owned.sameScope(s.client.address,s.map) || !InventoryAssetsValid()) {
        Log("WEAPON_RESTORE GIVE_ALL blocked: changed player/map/assets"); return false;
    }
    if (!ReadInventory(s.client.address,live)) return false;
    const auto target=BuildInventoryRestore(owned,live);
    if (!target.current) return false;
    if (!WriteInventory(s.client.address,live,target)) return false;
    Log("WEAPON_RESTORE GIVE_ALL inventory_and_ammo=original current=%u",target.current);
    features.ammo.clear(); owned.owned=false; return true;
}
bool SetPointer(std::uintptr_t at,std::uintptr_t before,std::uintptr_t after) {
    return before==after || ExchangeInventory({at,8,before,after});
}
void RestoreDualAsset() {
    auto& a=features.dualAsset;
    if (!a.active) return;
    if (!SameAsset(ReadWeapon(a.identity.index,false),a.identity)) { a={}; return; }
    if (a.veto.owned) {
        std::uint8_t value=255;
        if (ReadValue(a.veto.dvar,value) && (!a.veto.canRestore(a.veto.dvar,value)
            || SetBool(a.veto.dvar,value,a.veto.original))) a.veto.clear();
    }
    for (auto& p:a.pointers) if (p.owned) {
        std::uintptr_t value=0;
        if (ReadValue(p.dvar,value) && (!p.canRestore(p.dvar,value) || SetPointer(p.dvar,value,p.original))) p.clear();
    }
    if (!a.veto.owned && std::none_of(a.pointers.begin(),a.pointers.end(),[](const auto& p) { return p.owned; })) {
        Log("WEAPON_RESTORE DUAL_ASSET index=%u name=%s",a.identity.index,a.identity.name); a={};
    }
}
bool ReadAnimNames(std::uintptr_t table) {
    std::array<std::uintptr_t,kWeaponAnimCount> names{};
    if (!table || !Read(table,names.data(),sizeof(names))) return false;
    char text[128]{};
    for (std::size_t n=0;n<names.size();++n) {
        if (!ReadString(names[n],text,sizeof(text))) return false;
        if ((n==1 || n==3 || n==9 || n==13) && !text[0]) return false;
    }
    return true;
}
bool ConfigureDualAsset(const WeaponCandidate& w) {
    if (!profile.dualAnimResolved || !PrimaryGun(w)) return false;
    const auto at=profile.weaponAnimCache+w.weapon.index*kWeaponAnimCacheStride;
    std::array<std::uintptr_t,3> trees{};
    if (!Read(at,trees.data(),sizeof(trees)) || !trees[0]) return false;
    auto& a=features.dualAsset;
    if (a.active) return SameAsset(w.weapon,a.identity) && trees[0]==a.normalTree;
    std::uintptr_t normal=0,right=0,left=0;
    std::uint8_t veto=255;
    if (!ReadValue(w.weapon.complete+kNormalWeaponAnims,normal) || !ReadAnimNames(normal)
        || !ReadValue(w.weapon.definition+kRightWeaponAnims,right) || !ReadValue(w.weapon.definition+kLeftWeaponAnims,left)
        || !ReadValue(w.weapon.definition+kNoDualWield,veto) || veto>1) return false;
    const bool authored=right && left && ReadAnimNames(right) && ReadAnimNames(left);
    const auto plan=DualPlan(true,true,true,authored,trees[1] && trees[2]);
    a.active=true; a.identity=ReadWeapon(w.weapon.index,false); a.normalTree=trees[0]; a.fallback=plan.fallback;
    auto pointer=[&](int n,std::uintptr_t field,std::uintptr_t old,std::uintptr_t target) {
        if (old==target) return true;
        if (!SetPointer(field,old,target)) return false;
        a.pointers[n].applied(field,old,target); return true;
    };
    bool ok=true;
    if (plan.fallback) {
        // Arena-owned XAnim definitions are shared; the engine creates a
        // distinct playback tree for each hand. No native allocation/call.
        ok=pointer(0,w.weapon.definition+kRightWeaponAnims,right,normal)
            && pointer(1,w.weapon.definition+kLeftWeaponAnims,left,normal)
            && pointer(2,at+8,trees[1],trees[0]) && pointer(3,at+16,trees[2],trees[0]);
    }
    if (ok && veto) {
        ok=SetBool(w.weapon.definition+kNoDualWield,veto,0);
        if (ok) a.veto.applied(w.weapon.definition+kNoDualWield,veto,0);
    }
    if (!ok) { RestoreDualAsset(); return false; }
    Log("WEAPON_APPLY DUAL_ASSET index=%u name=%s veto_before=%u animation_fallback=%d",w.weapon.index,w.weapon.name,veto,plan.fallback);
    return true;
}
void RestoreDual(const Snapshot& s) {
    RestoreDualAsset();
    if (features.dualClient!=s.client.address || std::strcmp(features.dualMap,s.map)!=0) {
        features.dualSlots={}; features.dualAmmo={}; features.lastHand.clear(); return;
    }
    if (!s.inventoryValid) return;
    for (int n=0;n<15;++n) {
        auto& owned=features.dualSlots[n];
        if (!owned.owned) continue;
        if (s.inventory.weapons[n]!=owned.weapon || s.inventory.metadata[n][1]!=1
            || SetBool(s.client.address+kWeaponMetadata+n*5+1,1,owned.original)) owned={};
    }
    for (int n=0;n<15;++n) {
        auto& owned=features.dualAmmo[n];
        if (!owned.owned) continue;
        if (s.inventory.clips[n].id!=owned.pool || SetInt(s.client.address+kClipAmmo+n*12+8,s.inventory.clips[n].count[1],owned.original)) owned={};
    }
    int hand=0;
    if (features.lastHand.owned && ReadValue(s.client.address+kLastWeaponHand,hand)) {
        if (!features.lastHand.canRestore(s.client.address,hand)) features.lastHand.clear();
        else {
            InventoryData live;
            if (ReadInventory(s.client.address,live)) {
                int natural=0;
                for (int n=0;n<15;++n) if (live.weapons[n]==live.current) natural=live.metadata[n][1]?1:0;
                if (SetInt(s.client.address+kLastWeaponHand,hand,natural)) features.lastHand.clear();
            }
        }
    }
}
void ApplyDual(const Snapshot& s) {
    if (!s.inventoryValid || !features.extra.wanted(ExtraFeature::ForceDualWield)) return;
    for (int n=0;n<15;++n) {
        if (s.inventory.weapons[n]!=s.inventory.current) continue;
        const auto w=ReadCandidate(s.inventory.weapons[n]);
        if (!ConfigureDualAsset(w)) continue;
        features.dualReady=true;
        auto& owned=features.dualSlots[n];
        if (owned.owned && owned.weapon!=w.weapon.index) owned={};
        if (!s.inventory.metadata[n][1] && SetBool(s.client.address+kWeaponMetadata+n*5+1,0,1)) {
            owned={true,w.weapon.index,0};
            Log("WEAPON_APPLY DUAL_FLAG index=%u name=%s",w.weapon.index,w.weapon.name);
        }
        for (int k=0;k<15;++k) if (s.inventory.clips[k].id==w.clipId) {
            auto& ammo=features.dualAmmo[k];
            if (ammo.owned && ammo.pool!=w.clipId) ammo={};
            if (!ammo.owned && s.inventory.clips[k].count[1]!=w.clipSize
                && SetInt(s.client.address+kClipAmmo+k*12+8,s.inventory.clips[k].count[1],w.clipSize))
                ammo={true,w.clipId,s.inventory.clips[k].count[1]};
            break;
        }
        if (s.inventory.current==w.weapon.index) {
            int hand=0;
            if (ReadValue(s.client.address+kLastWeaponHand,hand) && (hand==0 || hand==1) && hand==0
                && SetInt(s.client.address+kLastWeaponHand,0,1)) features.lastHand.applied(s.client.address,0,1);
        }
    }
}
void RestoreRapid();
void ApplyBool(ScalarOwnership<std::uint8_t>&,const BoolDvarSnapshot&,bool,std::uint8_t,const char*);
void UpdateWeaponInventory(Snapshot& s) {
    const bool give=features.extra.wanted(ExtraFeature::GiveAll),dual=features.extra.wanted(ExtraFeature::ForceDualWield);
    features.dualReady=false;
    if (!dual) ApplyBool(features.dual,s.dualWield,false,0,"FORCE_DUAL_WIELD");
    if (!dual || features.dualHeld!=s.inventory.current) {
        RestoreDual(s); features.dualHeld=s.inventory.current;
    }
    if (!give && features.inventory.owned) {
        // Undo the current bank's dual edits before restoring/reapplying against
        // the original inventory. Its ammo must never become a new baseline.
        RestoreDual(s);
        if (features.dualAsset.active || HasDualPlayerEdits(features.dualSlots,features.dualAmmo,features.lastHand.owned)
            || !RestoreInventory(ReadSnapshot())) return;
        s=ReadSnapshot();
    }
    else if (give && s.inventoryValid && (!features.inventory.owned || features.inventory.page!=features.weaponBank)) {
        RestoreRapid(); RestoreDual(s);
        if (features.dualAsset.active || HasDualPlayerEdits(features.dualSlots,features.dualAmmo,features.lastHand.owned)) return;
        if (features.inventory.owned && !RestoreInventory(ReadSnapshot())) return;
        s=ReadSnapshot(); if (!s.inventoryValid) return;
        const auto catalog=ReadCatalog();
        const auto bank=BuildWeaponBank(s.inventory,catalog,features.weaponBank);
        if (!features.inventory.baselineValid) {
            features.inventory.capture(s.client.address,s.client.commandTime,s.map,s.inventory);
            features.inventoryIdentity={};
            for (const auto id:s.inventory.weapons) if (id) features.inventoryIdentity[id]=ReadWeapon(id,false);
        }
        if (!WriteInventory(s.client.address,s.inventory,bank.data)) { features.inventory.owned=false; return; }
        features.inventory.recordApplied(bank.data);
        features.weaponBank=bank.page; features.weaponPages=bank.pages; features.inventory.page=bank.page;
        features.inventory.pages=bank.pages; features.inventory.added=bank.added;
        for (const auto id:bank.data.weapons) if (id && !features.inventoryIdentity[id].valid)
            features.inventoryIdentity[id]=ReadWeapon(id,false);
        features.ammo.clear();
        Log("WEAPON_APPLY GIVE_ALL bank=%d/%d granted=%d eligible=%d original_inventory_preserved=1 ammo_initialized=1",
            bank.page+1,bank.pages,bank.added,bank.eligible);
        for (const auto& w:catalog) if (OwnsWeapon(bank.data,w.weapon.index))
            Log("WEAPON_BANK index=%u name=%s ammo_pool=%u clip_pool=%u clip_size=%d",w.weapon.index,w.weapon.name,w.ammoId,w.clipId,w.clipSize);
        s=ReadSnapshot();
    }
    if (dual) {
        features.dualClient=s.client.address;
        std::memcpy(features.dualMap,s.map,sizeof(features.dualMap));
        features.dualHeld=s.inventory.current;
        ApplyDual(s);
    }
    ApplyBool(features.dual,s.dualWield,dual,features.dualReady?1:0,"FORCE_DUAL_WIELD");
}
bool SetReserve(std::uintptr_t at, const AmmoPool& before, int target) {
    if ((at&7) || !Writable(at)) return false;
    const auto old=PackReserve(before),next=PackReserve({before.id,target});
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONGLONG*>(at),
        static_cast<LONGLONG>(next),static_cast<LONGLONG>(old)))==old;
}
bool SetFloat(std::uintptr_t at,float old,float target) {
    LONG before=0,after=0; std::memcpy(&before,&old,4); std::memcpy(&after,&target,4);
    return !(at&3) && Writable(at) && (old==target
        || InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),after,before)==before);
}
bool SetGravityLiteral(float target) {
    float old=0.f;
    if (!profile.gravityResolved || !ReadValue(profile.gravityLiteral,old) || !ValidGravityLiteral(old)
        || !ValidGravityLiteral(target) || (profile.gravityLiteral&3)) return false;
    if (old==target) return true;
    DWORD protection=0,unused=0;
    if (!VirtualProtect(reinterpret_cast<void*>(profile.gravityLiteral),4,PAGE_READWRITE,&protection)) return false;
    const bool applied=SetFloat(profile.gravityLiteral,old,target);
    const bool restored=VirtualProtect(reinterpret_cast<void*>(profile.gravityLiteral),4,protection,&unused)!=0;
    if (applied) {
        features.gravityLiteral.applied(profile.gravityLiteral,old,target);
        Log("WORLD_APPLY PLAYER_GRAVITY literal_before=%.1f literal_after=%.1f protection_restored=%d",old,target,restored);
    }
    if (!applied || !restored) Log("WORLD_APPLY PLAYER_GRAVITY failed exchange=%d protection_restored=%d",applied,restored);
    return applied && restored;
}
void RestoreGravityLiteral() {
    auto& owned=features.gravityLiteral;
    if (!owned.owned) return;
    float current=0.f;
    if (!ReadValue(profile.gravityLiteral,current) || !owned.canRestore(profile.gravityLiteral,current)) owned.clear();
    else if (SetGravityLiteral(owned.original)) owned.clear();
}
// These two immutable floats are pooled only by the verified jump consumers.
// Change data in this process; never patch instructions or the disk executable.
bool SetJump(float height) {
    const auto s=ReadJump();
    const float twice=height*2.f;
    if (!s.valid || !ValidJumpPair(height,twice)) return false;
    const bool increasing=height>s.height;
    if (s.height==height) return true;
    MEMORY_BASIC_INFORMATION memory{};
    if ((profile.jumpHeight&3) || profile.jumpTwiceHeight!=profile.jumpHeight+4
        || !VirtualQuery(reinterpret_cast<void*>(profile.jumpHeight),&memory,sizeof(memory))
        || memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    DWORD oldProtection=0;
    if (!VirtualProtect(reinterpret_cast<void*>(profile.jumpHeight),8,PAGE_READWRITE,&oldProtection)) return false;
    auto exchange=[&](std::uintptr_t at,float from,float to) {
        LONG old=0,next=0; std::memcpy(&old,&from,4); std::memcpy(&next,&to,4);
        return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),next,old)==old;
    };
    // Raise the cap before the impulse; lower the impulse before the cap.
    bool ok=increasing?exchange(profile.jumpHeight,s.height,height):exchange(profile.jumpTwiceHeight,s.twiceHeight,twice);
    if (ok) {
        ok=increasing?exchange(profile.jumpTwiceHeight,s.twiceHeight,twice):exchange(profile.jumpHeight,s.height,height);
        if (!ok) {
            if (increasing) exchange(profile.jumpHeight,height,s.height);
            else exchange(profile.jumpTwiceHeight,twice,s.twiceHeight);
        }
    }
    DWORD unused=0;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(profile.jumpHeight),8,oldProtection,&unused)!=0;
    if (!ok || !restored) Log("PLAYER_APPLY SUPER_JUMP failed exchange=%d protection_restored=%d",ok,restored);
    if (ok) {
        features.jumpOwned=height!=kDefaultJump;
        Log("PLAYER_APPLY SUPER_JUMP height_before=%.1f height_after=%.1f twice_after=%.1f protection_restored=%d",s.height,height,twice,restored);
    }
    return ok && restored;
}
void RestoreDvars() {
    const auto ammo=ReadSustain();
    if (features.sustain.owned) {
        if (!ammo.valid || ammo.dvar!=features.sustain.dvar || ammo.current!=1) features.sustain.owned=false;
        else if (SetBool(ammo.dvar+0x10,ammo.current,features.sustain.original)) {
            Log("PLAYER_RESTORE NO_RELOAD value=%u",features.sustain.original); features.sustain.owned=false;
        }
    }
    const auto speed=ReadSpeed();
    if (features.speed.owned) {
        if (!speed.valid || !features.speed.canRestore(speed.dvar,speed.current)) features.speed.clear();
        else if (SetInt(speed.dvar+0x10,speed.current,features.speed.original)) {
            Log("PLAYER_RESTORE SUPER_SPEED value=%d",features.speed.original); features.speed.owned=false;
        }
    }
    if (features.jumpOwned) SetJump(kDefaultJump);
}
void RestoreRapid() {
    if (!features.rapid.owned) return;
    const auto w=ReadWeapon(features.rapid.identity.index);
    if (!features.rapid.canRestore(w)) features.rapid.clear();
    else if (SetInt(w.complete+kWeaponFireTime,w.fireTime,features.rapid.original)) {
        Log("WEAPON_RESTORE RAPID_FIRE index=%u name=%s fire_time=%d",w.index,w.name,features.rapid.original);
        features.rapid.clear();
    }
}
void ApplyBool(ScalarOwnership<std::uint8_t>& owned,const BoolDvarSnapshot& s,bool enabled,std::uint8_t target,const char* name) {
    if (!s.valid) return;
    if (enabled) {
        if (s.current!=target && SetBool(s.dvar+0x10,s.current,target)) {
            owned.applied(s.dvar,s.current,target);
            Log("EXTRA_APPLY %s before=%u after=%u",name,s.current,target);
        }
    } else if (owned.owned) {
        if (!owned.canRestore(s.dvar,s.current)) owned.clear();
        else if (SetBool(s.dvar+0x10,s.current,owned.original)) {
            Log("EXTRA_RESTORE %s value=%u",name,owned.original); owned.clear();
        }
    }
}
void ApplyFloat(ScalarOwnership<float>& owned,const FloatDvarSnapshot& s,bool enabled,float target,const char* name) {
    if (!s.valid) return;
    if (enabled) {
        if (s.current!=target && SetFloat(s.dvar+0x10,s.current,target)) {
            owned.applied(s.dvar,s.current,target);
            Log("EXTRA_APPLY %s before=%.3f after=%.3f",name,s.current,target);
        }
    } else if (owned.owned) {
        if (!owned.canRestore(s.dvar,s.current)) owned.clear();
        else if (SetFloat(s.dvar+0x10,s.current,owned.original)) {
            Log("EXTRA_RESTORE %s value=%.3f",name,owned.original); owned.clear();
        }
    }
}
void RestoreExtras() {
    RestoreDualAsset(); features.dualReady=false;
    ApplyBool(features.giveAll,ReadBoolDvar(profile.giveAllSlot,profile.giveAllResolved,"g_giveAll"),false,1,"GIVE_ALL_LOADED");
    ApplyBool(features.dual,ReadBoolDvar(profile.dualSlot,profile.dualResolved,"bg_forceDualWield"),false,1,"FORCE_DUAL_WIELD");
    ApplyBool(features.explosive,ReadBoolDvar(profile.explosiveSlot,profile.explosiveResolved,"bg_forceExplosiveBullets"),false,1,"EXPLOSIVE_BULLETS");
    ApplyBool(features.ai,ReadBoolDvar(profile.aiSlot,profile.aiResolved,"g_ai"),false,0,"FREEZE_AI");
    ApplyFloat(features.time,ReadFloatDvar(profile.timeSlot,profile.timeResolved,"timescale",.001f,1000.f),false,1.f,"SLOW_MOTION");
    ApplyFloat(features.gravity,ReadFloatDvar(profile.gravitySlot,profile.gravityResolved,"g_gravity",0.f,10000.f),false,800.f,"GRAVITY");
    RestoreGravityLiteral();
    RestoreRapid();
}
}
const char* StateText(LevelState state) {
    switch (state) {
        case LevelState::Unsupported: return "UNSUPPORTED EXECUTABLE";
        case LevelState::Unresolved: return "SIGNATURES NOT RESOLVED";
        case LevelState::NoLevel: return "NO ACTIVE LEVEL";
        case LevelState::Transition: return "LOADING / PLAYER NOT READY";
        case LevelState::PlayerDead: return "LEVEL LOADED / PLAYER DEAD";
        case LevelState::Ready: return "LEVEL ACTIVE / PLAYER ALIVE";
    }
    return "UNKNOWN";
}
void InitializeEngine() {
    profile.base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!ReadValue(profile.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadValue(profile.base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) { Log("Host PE invalid or not AMD64"); return; }
    profile.size = nt.OptionalHeader.SizeOfImage;
    const std::string hash = ExecutableHash();
    profile.supported = hash == kExeSha256;
    Log("HOST sha256=%s base=%p timestamp=0x%08lx image_size=0x%llx supported=%d", hash.c_str(),
        reinterpret_cast<void*>(profile.base), nt.FileHeader.TimeDateStamp,
        static_cast<unsigned long long>(profile.size), profile.supported);
    if (!profile.supported) { Log("Game-state access and all memory writes DISABLED: unknown executable hash"); return; }
    const auto sectionStart = profile.base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned n = 0; n < nt.FileHeader.NumberOfSections; ++n) {
        IMAGE_SECTION_HEADER section{};
        if (!ReadValue(sectionStart + n * sizeof(section), section)) return;
        if (std::memcmp(section.Name, ".text\0", 6) != 0) continue;
        const auto start = profile.base + section.VirtualAddress;
        const auto length = section.Misc.VirtualSize;
        if (!InImage(start, length)) return;
        std::vector<std::uint8_t> text(length);
        if (!Read(start, text.data(), text.size())) return;
        const auto sv = Unique(text, start, "sv_running registration", kSvRunningPattern);
        const auto map = Unique(text, start, "mapname registration", kMapNamePattern);
        const auto ent = Unique(text, start, "god command entity pointer", kEntityPattern);
        const auto damage = Unique(text, start, "god damage check", kGodDamagePattern);
        const auto health = Unique(text, start, "cheat alive guard", kHealthGuardPattern);
        if (!sv || !map || !ent || !damage || !health || !Anchor(sv, 29, "sv_running") || !Anchor(map, 13, "mapname")) {
            Log("Profile resolution failed; no writes will be enabled"); return;
        }
        profile.serverDvarSlot = ResolveRip(sv, 54);
        profile.mapDvarSlot = ResolveRip(map, 27);
        profile.entitySlot = ResolveRip(ent, 0);
        profile.resolved = InImage(profile.serverDvarSlot, 8) && InImage(profile.mapDvarSlot, 8) && InImage(profile.entitySlot, 8);
        Log("SLOTS sv_running=0x%llx mapname=0x%llx entities=0x%llx resolved=%d",
            static_cast<unsigned long long>(profile.serverDvarSlot - profile.base),
            static_cast<unsigned long long>(profile.mapDvarSlot - profile.base),
            static_cast<unsigned long long>(profile.entitySlot - profile.base), profile.resolved);
        // Optional feature resolution is independent of the working core gate.
        const auto fov=Unique(text,start,"cg_fov registration",kFovRegistrationPattern);
        const auto camera=Unique(text,start,"cg_fov camera read",kFovCameraPattern);
        if (fov && camera && Anchor(fov,9,"cg_fov")) {
            profile.fovDvarSlot=ResolveRip(fov,68);
            auto constant=[&](unsigned offset,unsigned displacement,unsigned length,float wanted) {
                std::uint8_t bytes[9]{}; float value=0.f;
                return Read(fov+offset,bytes,length)
                    && ReadValue(RipTarget(fov+offset,bytes,displacement,length),value) && value==wanted;
            };
            profile.fovResolved=InImage(profile.fovDvarSlot,8) && profile.fovDvarSlot==ResolveRip(camera,0)
                && constant(0,5,9,1.f) && constant(16,4,8,160.f) && constant(28,4,8,static_cast<float>(kDefaultFov));
        }
        Log("FOV slot_rva=0x%llx resolved=%d native_default=%d menu_range=%d..%d",
            static_cast<unsigned long long>(profile.fovDvarSlot?profile.fovDvarSlot-profile.base:0),
            profile.fovResolved,kDefaultFov,kMinFov,kMaxFov);
        const auto client=Unique(text,start,"connected client / movement fields",kClientReadinessPattern);
        const auto clientDamage=Unique(text,start,"client invulnerability / noclip damage guards",kClientDamageGuardPattern);
        const auto clearInvul=Unique(text,start,"native clear invulnerability field",kClientInvulnerabilityClearPattern);
        const auto noclip=Unique(text,start,"native noclip command",kNoclipCommandPattern);
        const auto movement=Unique(text,start,"native noclip movement types",kNoclipMovementPattern);
        profile.clientResolved=client && clientDamage && clearInvul && noclip && movement;
        profile.ammoResolved=Unique(text,start,"reserve ammo read layout",kReserveReadPattern)
            && Unique(text,start,"reserve ammo write layout",kReserveWritePattern);
        const auto sustain=Unique(text,start,"player_sustainAmmo registration",kSustainRegistrationPattern);
        const auto consume=Unique(text,start,"player_sustainAmmo clip consumption",kSustainConsumerPattern);
        if (sustain && consume && Anchor(sustain,2,"player_sustainAmmo")) {
            profile.sustainSlot=ResolveRip(sustain,20);
            profile.sustainResolved=InImage(profile.sustainSlot,8) && profile.sustainSlot==ResolveRip(consume,0);
        }
        const auto speed=Unique(text,start,"g_speed registration",kSpeedRegistrationPattern);
        const auto speedRead=Unique(text,start,"g_speed player movement read",kSpeedConsumerPattern);
        if (speed && speedRead && Anchor(speed,0,"g_speed")) {
            profile.speedSlot=ResolveRip(speed,64);
            profile.speedResolved=InImage(profile.speedSlot,8) && profile.speedSlot==ResolveRip(speedRead,0);
        }
        const auto jump=Unique(text,start,"jump_height registration",kJumpRegistrationPattern);
        const auto impulse=Unique(text,start,"native fixed jump impulse",kJumpImpulsePattern);
        const auto clamp=Unique(text,start,"native fixed jump velocity clamp",kJumpClampPattern);
        const auto step=Unique(text,start,"native fixed jump step limit",kJumpStepPattern);
        const auto ceiling=Unique(text,start,"native fixed jump ceiling",kJumpCeilingPattern);
        if (jump && impulse && clamp && step && ceiling && Anchor(jump,8,"jump_height")) {
            profile.jumpSlot=ResolveRip(jump,62);
            profile.jumpHeight=ResolveScalarRip(jump,0);
            profile.jumpTwiceHeight=ResolveScalarRip(impulse,8);
            auto references=[&](std::uintptr_t target) {
                int count=0;
                for (std::size_t i=0;i+8<=text.size();++i)
                    if (text[i]==0xf3 && text[i+1]==0x0f && (text[i+3]&0xc7)==5
                        && RipTarget(start+i,text.data()+i,4,8)==target) ++count;
                return count;
            };
            float height=0.f,twice=0.f;
            profile.jumpResolved=InImage(profile.jumpSlot,8) && InImage(profile.jumpHeight,8)
                && profile.jumpTwiceHeight==profile.jumpHeight+4
                && profile.jumpHeight==ResolveScalarRip(clamp,8) && profile.jumpHeight==ResolveScalarRip(step,8)
                && profile.jumpHeight==ResolveScalarRip(ceiling,8)
                && ReadValue(profile.jumpHeight,height) && height==kDefaultJump
                && ReadValue(profile.jumpTwiceHeight,twice) && twice==kDefaultJump*2.f
                && references(profile.jumpHeight)==4 && references(profile.jumpTwiceHeight)==1;
        }
        Log("PLAYER_PROFILE client=%d ammo=%d sustain=%d sustain_slot=0x%llx speed=%d speed_slot=0x%llx jump=%d jump_height=0x%llx jump_twice=0x%llx",
            profile.clientResolved,profile.ammoResolved,profile.sustainResolved,
            static_cast<unsigned long long>(profile.sustainSlot?profile.sustainSlot-profile.base:0),
            profile.speedResolved,static_cast<unsigned long long>(profile.speedSlot?profile.speedSlot-profile.base:0),
            profile.jumpResolved,static_cast<unsigned long long>(profile.jumpHeight?profile.jumpHeight-profile.base:0),
            static_cast<unsigned long long>(profile.jumpTwiceHeight?profile.jumpTwiceHeight-profile.base:0));
        const auto give=Unique(text,start,"g_giveAll registration",kGiveAllRegistrationPattern);
        const auto giveRead=Unique(text,start,"g_giveAll inventory consumer",kGiveAllConsumerPattern);
        if (give && giveRead && Anchor(give,15,"g_giveAll")) {
            profile.giveAllSlot=ResolveRip(give,34);
            profile.giveAllResolved=InImage(profile.giveAllSlot,8) && profile.giveAllSlot==ResolveRip(giveRead,0);
        }
        const auto explosive=Unique(text,start,"explosive bullet registration",kExplosiveRegistrationPattern);
        const auto explosiveRead=Unique(text,start,"explosive bullet native consumer",kExplosiveConsumerPattern);
        if (explosive && explosiveRead && Anchor(explosive,6,"bg_forceExplosiveBullets")) {
            profile.explosiveSlot=ResolveRip(explosive,34);
            profile.explosiveResolved=InImage(profile.explosiveSlot,8) && profile.explosiveSlot==ResolveRip(explosiveRead,9);
        }
        const auto ai=Unique(text,start,"g_ai registration",kAiRegistrationPattern);
        const auto aiRead=Unique(text,start,"g_ai actor think gate",kAiConsumerPattern);
        if (ai && aiRead && Anchor(ai,20,"g_ai")) {
            profile.aiSlot=ResolveRip(ai,41);
            profile.aiResolved=InImage(profile.aiSlot,8) && profile.aiSlot==ResolveRip(aiRead,0);
        }
        const auto time=Unique(text,start,"timescale registration",kTimeRegistrationPattern);
        const auto timeRead=Unique(text,start,"timescale clock consumer",kTimeConsumerPattern);
        if (time && timeRead && Anchor(time,0,"timescale")) {
            profile.timeSlot=ResolveRip(time,48);
            profile.timeResolved=InImage(profile.timeSlot,8) && profile.timeSlot==ResolveRip(timeRead,0);
        }
        const auto gravity=Unique(text,start,"g_gravity registration",kGravityRegistrationPattern);
        const auto trajectory=Unique(text,start,"g_gravity trajectory consumer",kGravityTrajectoryPattern);
        const auto playerGravity=Unique(text,start,"fixed player gravity source",kPlayerGravityPattern);
        if (gravity && trajectory && playerGravity && Anchor(gravity,8,"g_gravity")) {
            profile.gravitySlot=ResolveRip(gravity,57);
            profile.gravityLiteral=ResolveScalarRip(playerGravity,0);
            int references=0; float defaultGravity=0.f,playerDefault=0.f;
            for (std::size_t i=0;i+8<=text.size();++i)
                if (text[i]==0xf3 && text[i+1]==0x0f && (text[i+3]&0xc7)==5
                    && RipTarget(start+i,text.data()+i,4,8)==profile.gravityLiteral) ++references;
            profile.gravityResolved=InImage(profile.gravitySlot,8) && profile.gravitySlot==ResolveRip(trajectory,0)
                && InImage(profile.gravityLiteral,4) && references==1
                && ReadValue(ResolveScalarRip(gravity,0),defaultGravity) && defaultGravity==800.f
                && ReadValue(profile.gravityLiteral,playerDefault) && playerDefault==kDefaultGravityLiteral;
        }
        const auto catalog=Unique(text,start,"weapon catalog capacity",kWeaponCatalogPattern);
        const auto count=Unique(text,start,"weapon catalog last index",kWeaponCountPattern);
        const auto pointers=Unique(text,start,"complete/definition table lookups",kWeaponPointersPattern);
        const auto binding=Unique(text,start,"complete to definition binding",kWeaponBindingPattern);
        const auto scriptFire=Unique(text,start,"weaponfiretime script field",kWeaponFireScriptPattern);
        const auto fireRead=Unique(text,start,"native fire-time consumer",kWeaponFireConsumerPattern);
        if (catalog && count && pointers && binding && scriptFire && fireRead) {
            profile.weaponTable=ResolveRip(pointers,2); profile.weaponDefTable=ResolveRip(pointers,16);
            profile.weaponCount=ResolveDwordRip(count,17);
            profile.weaponResolved=InImage(profile.weaponTable,kWeaponCapacity*8) && InImage(profile.weaponDefTable,kWeaponCapacity*8)
                && InImage(profile.weaponCount,4) && profile.weaponTable==ResolveRip(catalog,6)
                && profile.weaponTable==ResolveRip(count,6) && profile.weaponDefTable==ResolveRip(catalog,26)
                && profile.weaponDefTable==ResolveRip(binding,4) && ResolveCall(scriptFire,2)==pointers;
        }
        const auto grenades=Unique(text,start,"frag/flash native ammo-sustain branches",kGrenadeSustainPattern);
        profile.grenadeSustainResolved=grenades && Anchor(grenades,3,"fraggrenade") && Anchor(grenades,26,"flash_grenade");
        const auto dual=Unique(text,start,"dual wield registration",kDualRegistrationPattern);
        const auto dualRead=Unique(text,start,"dual wield hand selection",kDualConsumerPattern);
        if (dual && dualRead && Anchor(dual,7,"bg_forceDualWield")) {
            profile.dualSlot=ResolveRip(dual,25);
            profile.dualResolved=InImage(profile.dualSlot,8) && profile.dualSlot==ResolveRip(dualRead,0);
        }
        const auto slots=Unique(text,start,"owned weapon slot grant",kWeaponSlotGrantPattern);
        const auto metadata=Unique(text,start,"weapon metadata stride",kWeaponMetadataPattern);
        const auto ammoIndex=Unique(text,start,"weapon ammo pool index",kAmmoIndexPattern);
        const auto clipIndex=Unique(text,start,"weapon clip pool index",kClipIndexPattern);
        const auto clipSize=Unique(text,start,"weapon magazine size",kClipSizePattern);
        const auto playable=Unique(text,start,"player weapon model gate",kPlayableWeaponPattern);
        const auto alternate=Unique(text,start,"alternate weapon chain",kAlternateChainPattern);
        const auto clips=Unique(text,start,"native clip pool layout",kClipPoolPattern);
        profile.inventoryResolved=profile.weaponResolved && slots && metadata && ammoIndex && clipIndex && clipSize && playable
            && alternate && clips && ResolveCall(alternate,0)==pointers
            && ResolveCall(ammoIndex,7)==pointers+16 && ResolveCall(clipIndex,3)==pointers+16
            && ResolveCall(clipSize,6)==pointers;
        const auto animSupport=Unique(text,start,"dual animation name support",kDualAnimSupportPattern);
        const auto animSelect=Unique(text,start,"native handed animation selection",kDualAnimSelectPattern);
        const auto animCache=Unique(text,start,"native weapon animation cache read",kDualAnimCacheReadPattern);
        const auto animReset=Unique(text,start,"native animation cache reset",kDualAnimCacheResetPattern);
        const auto animStride=Unique(text,start,"three animation definitions per weapon",kDualAnimCacheStridePattern);
        const auto animBuild=Unique(text,start,"37 animation names and idle fallback",kWeaponAnimBuildPattern);
        if (animCache && animReset) profile.weaponAnimCache=ResolveRip(animCache,0);
        profile.dualAnimResolved=profile.inventoryResolved && profile.dualResolved && animSupport && animSelect
            && animCache && animReset && animStride && animBuild
            && InImage(profile.weaponAnimCache,kWeaponCapacity*kWeaponAnimCacheStride)
            && profile.weaponAnimCache==ResolveRip(animReset,2);
        Log("DUAL_ANIM_PROFILE resolved=%d cache_rva=0x%llx stride=%llu",profile.dualAnimResolved,
            static_cast<unsigned long long>(profile.weaponAnimCache?profile.weaponAnimCache-profile.base:0),
            static_cast<unsigned long long>(kWeaponAnimCacheStride));
        Log("WEAPON_PROFILE inventory=%d dual=%d dual_slot_rva=0x%llx",profile.inventoryResolved,profile.dualResolved,
            static_cast<unsigned long long>(profile.dualSlot?profile.dualSlot-profile.base:0));
        Log("EXTRA_PROFILE give_all=%d explosive=%d ai=%d timescale=%d gravity=%d weapons=%d grenade_sustain=%d gravity_literal_rva=0x%llx weapon_table_rva=0x%llx count_rva=0x%llx",
            profile.giveAllResolved,profile.explosiveResolved,profile.aiResolved,profile.timeResolved,profile.gravityResolved,
            profile.weaponResolved,profile.grenadeSustainResolved,
            static_cast<unsigned long long>(profile.gravityLiteral?profile.gravityLiteral-profile.base:0),
            static_cast<unsigned long long>(profile.weaponTable?profile.weaponTable-profile.base:0),
            static_cast<unsigned long long>(profile.weaponCount?profile.weaponCount-profile.base:0));
        return;
    }
}
Snapshot ReadSnapshot() {
    Snapshot s;
    s.supported = profile.supported;
    s.resolved = profile.resolved;
    if (!s.supported) { s.state = LevelState::Unsupported; return s; }
    if (!s.resolved) { s.state = LevelState::Unresolved; return s; }
    s.fov=ReadFov();
    s.sustain=ReadSustain(); s.speed=ReadSpeed(); s.jump=ReadJump();
    s.giveAll=ReadBoolDvar(profile.giveAllSlot,profile.giveAllResolved,"g_giveAll");
    s.dualWield=ReadBoolDvar(profile.dualSlot,profile.dualResolved,"bg_forceDualWield");
    s.inventoryResolved=profile.inventoryResolved;
    s.explosive=ReadBoolDvar(profile.explosiveSlot,profile.explosiveResolved,"bg_forceExplosiveBullets");
    s.ai=ReadBoolDvar(profile.aiSlot,profile.aiResolved,"g_ai");
    s.timeScale=ReadFloatDvar(profile.timeSlot,profile.timeResolved,"timescale",.001f,1000.f);
    s.gravity=ReadFloatDvar(profile.gravitySlot,profile.gravityResolved,"g_gravity",0.f,10000.f);
    s.gravityLiteralValid=profile.gravityResolved && ReadValue(profile.gravityLiteral,s.gravityLiteral) && ValidGravityLiteral(s.gravityLiteral);
    s.weaponResolved=profile.weaponResolved; s.grenadeSustainResolved=profile.grenadeSustainResolved;
    if (s.weaponResolved) ReadValue(profile.weaponCount,s.weaponCount);
    s.clientResolved=profile.clientResolved; s.ammoResolved=profile.ammoResolved;
    s.state = LevelState::Transition;
    std::uintptr_t server = 0, map = 0, mapString = 0;
    std::uint8_t running = 0;
    if (!NamedDvar(profile.serverDvarSlot, "sv_running", 0, server) || !ReadValue(server + 0x10, running) || running > 1) return s;
    s.serverRunning = running != 0;
    if (!s.serverRunning) { s.state = LevelState::NoLevel; return s; }
    if (!NamedDvar(profile.mapDvarSlot, "mapname", 7, map) || !ReadValue(map + 0x10, mapString) ||
        !ReadString(mapString, s.map, sizeof(s.map)) || !s.map[0]) return s;
    if (!ReadValue(profile.entitySlot, s.entity) || !InImage(s.entity, kEntityStride)) return s;
    const auto fields = ReadPlayerFields(s.entity, Read);
    s.entityNumber = fields.number;
    s.playerRead = fields.status;
    s.health = fields.health;
    s.flags = fields.flags;
    if (s.playerRead != PlayerReadStatus::Complete) return s;
    s.god = (s.flags & 1) != 0;
    std::uint8_t damage=255;
    s.damageRead=ReadValue(s.entity+kEntityTakeDamage,damage) && damage<=1;
    s.takesDamage=s.damageRead && damage!=0;
    if (s.clientResolved) s.client=ReadClientFields(s.entity,Read);
    if (s.client.valid) {
        unsigned current=0;
        if (ReadValue(s.client.address+kCurrentWeapon,current)) s.weapon=ReadWeapon(current);
        s.inventoryValid=ReadInventory(s.client.address,s.inventory);
        const auto candidate=ReadCandidate(current);
        s.dualCurrentSupported=PrimaryGun(candidate);
        ReadValue(s.client.address+kLastWeaponHand,s.lastWeaponHand);
        s.playerGravityRead=ReadValue(s.client.address+kPlayerGravity,s.playerGravity) && s.playerGravity>=0 && s.playerGravity<=10000;
    }
    s.ammoValid=s.ammoResolved && s.client.valid
        && Read(s.client.address+kReserveAmmo,s.ammo.data(),sizeof(s.ammo)) && ValidReserveTable(s.ammo);
    s.state = s.health > 0 ? LevelState::Ready : LevelState::PlayerDead;
    return s;
}
bool FeatureAvailable(const Snapshot& s, PlayerFeature feature) {
    if (s.state!=LevelState::Ready) return false;
    if (feature==PlayerFeature::God) return true;
    if (!s.client.valid || s.client.health<=0 || s.client.moveType>=6) return false;
    switch (feature) {
        case PlayerFeature::InfiniteAmmo: return s.ammoValid;
        case PlayerFeature::NoReload: return s.sustain.valid;
        case PlayerFeature::Noclip: return s.clientResolved;
        case PlayerFeature::SuperSpeed: return s.speed.valid;
        case PlayerFeature::SuperJump: return s.jump.valid;
        default: return false;
    }
}
PlayerPreferences GetPlayerPreferences() {
    std::lock_guard<std::mutex> lock(featuresMutex); return features.preferences;
}
bool TogglePlayerFeature(PlayerFeature feature) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    const auto s=ReadSnapshot();
    if (!FeatureAvailable(s,feature)) {
        Log("PLAYER_REQUEST %s blocked state=%s client_valid=%d client_health=%d move_type=%d",FeatureName(feature),StateText(s.state),
            s.client.valid,s.client.health,s.client.moveType); return false;
    }
    features.preferences.toggle(feature); features.nextUpdate=0;
    const bool enabled=features.preferences.wanted(feature);
    if (feature==PlayerFeature::God && !enabled) features.clearInvulnerability=true;
    if (feature==PlayerFeature::NoReload && !enabled) features.clearSustain=true;
    if (feature==PlayerFeature::SuperSpeed && !enabled) features.clearSpeed=true;
    Log("PLAYER_REQUEST %s %s map=%s health=%d flags=0x%08x other_flags=0x%08x move_flags=0x%08x",
        FeatureName(feature),enabled?"ON":"OFF",s.map,s.health,s.flags,s.client.otherFlags,s.client.cheatFlags);
    return true;
}
bool AdjustPlayerFeature(PlayerFeature feature,int direction) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    if (!FeatureAvailable(ReadSnapshot(),feature) || !features.preferences.adjust(feature,direction)) return false;
    features.nextUpdate=0;
    Log("PLAYER_ADJUST %s speed=%d jump=%d requested=%d",FeatureName(feature),features.preferences.speed,features.preferences.jump,
        features.preferences.wanted(feature));
    return true;
}
ExtraPreferences GetExtraPreferences() { std::lock_guard<std::mutex> lock(featuresMutex); return features.extra; }
bool RapidFireApplied(const WeaponFields& weapon) { std::lock_guard<std::mutex> lock(featuresMutex); return features.rapid.canRestore(weapon); }
WeaponModeStatus GetWeaponModeStatus() {
    std::lock_guard<std::mutex> lock(featuresMutex);
    const bool dualOwned=features.dual.owned || features.dualAsset.active || HasDualPlayerEdits(features.dualSlots,features.dualAmmo,features.lastHand.owned);
    return {features.inventory.owned,features.weaponBank,features.weaponPages,features.inventory.added,dualOwned,features.dualReady,features.dualAsset.fallback,
        features.inventoryReconcilePending,features.inventoryNeedsBaseline};
}
bool AdjustWeaponBank(int direction) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    const auto s=ReadSnapshot(); if (!ExtraAvailable(s,ExtraFeature::GiveAll) || !direction) return false;
    const auto plan=BuildWeaponBank(features.inventory.owned?features.inventory.original:s.inventory,ReadCatalog(),features.weaponBank+(direction>0?1:-1));
    features.weaponPages=plan.pages;
    if (plan.page==features.weaponBank) return false;
    features.weaponBank=plan.page; features.nextUpdate=0;
    Log("WEAPON_REQUEST bank=%d/%d",plan.page+1,plan.pages); return true;
}
bool ExtraAvailable(const Snapshot& s,ExtraFeature feature) {
    if (s.state!=LevelState::Ready || !s.client.valid || s.client.health<=0 || s.client.moveType>=6) return false;
    switch (feature) {
        case ExtraFeature::GiveAll: return s.giveAll.valid && s.inventoryValid;
        case ExtraFeature::RapidFire: return s.weapon.valid && s.weapon.type==0 && s.weapon.fireTime>=20 && s.weapon.fireTime<=4000;
        case ExtraFeature::ExplosiveBullets: return s.explosive.valid;
        case ExtraFeature::SlowMotion: return s.timeScale.valid;
        case ExtraFeature::FreezeAI: return s.ai.valid;
        case ExtraFeature::Gravity: return s.gravity.valid && s.gravityLiteralValid && s.playerGravityRead;
        case ExtraFeature::ForceDualWield: return profile.dualAnimResolved && s.dualWield.valid && s.inventoryValid;
        default: return false;
    }
}
bool ToggleExtraFeature(ExtraFeature feature) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    const auto state=ReadSnapshot();
    if (feature==ExtraFeature::GiveAll && !features.extra.wanted(feature) && !features.inventory.baselineValid
        && state.inventoryValid && std::count(state.inventory.weapons.begin(),state.inventory.weapons.end(),0u)==0) {
        features.inventoryNeedsBaseline=true;
        Log("EXTRA_REQUEST GIVE_ALL blocked: no clean baseline/free slots; restart mission once"); return false;
    }
    if (!features.extra.wanted(feature) && !ExtraAvailable(state,feature)) {
        Log("EXTRA_REQUEST %s blocked",ExtraName(feature)); return false;
    }
    features.extra.toggle(feature); features.nextUpdate=0;
    if (feature==ExtraFeature::GiveAll) features.inventoryNeedsBaseline=false;
    Log("EXTRA_REQUEST %s %s",ExtraName(feature),features.extra.wanted(feature)?"ON":"OFF"); return true;
}
bool AdjustExtraFeature(ExtraFeature feature,int direction) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    if (!ExtraAvailable(ReadSnapshot(),feature) || !features.extra.adjust(feature,direction)) return false;
    features.nextUpdate=0;
    Log("EXTRA_ADJUST %s timescale=%.2f gravity=%d requested=%d",ExtraName(feature),features.extra.timeScale,features.extra.gravity,
        features.extra.wanted(feature)); return true;
}
void ResetWeaponFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex);
    features.extra.resetWeapons(); features.preferences.enabled[static_cast<std::size_t>(PlayerFeature::NoReload)]=false;
    features.clearSustain=true; features.nextUpdate=0; RestoreRapid();
    Log("EXTRA_REQUEST RESET_WEAPONS linked_no_reload=OFF saved_fov_unchanged=1");
}
void ResetWorldFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex);
    features.extra.resetWorld(); features.nextUpdate=0;
    Log("EXTRA_REQUEST RESET_WORLD saved_fov_unchanged=1");
}
void ResetPlayerFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex);
    features.preferences.reset(); features.clearInvulnerability=true; features.clearSustain=true; features.clearSpeed=true; features.nextUpdate=0;
    Log("PLAYER_REQUEST RESET all_cheats=OFF saved_fov_unchanged=1");
}
void UpdatePlayerFeatures(ULONGLONG now) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    if (now<features.nextUpdate) return;
    features.nextUpdate=now+16;
    MeasureUpdate measure;
    auto s=ReadSnapshot();
    if (s.state!=LevelState::Ready) {
        features.lifetime.observe(false,0,0,""); features.ammo.clear(); RestoreDvars(); RestoreExtras(); return;
    }
    const bool newLifetime=features.lifetime.observe(true,s.client.address,s.client.commandTime,s.map);
    if (newLifetime) {
        RestoreDual(s);
        features.inventoryReconcilePending=features.inventory.baselineValid;
        if (features.dualClient!=s.client.address || std::strcmp(features.dualMap,s.map)!=0) {
            features.dualSlots={}; features.dualAmmo={}; features.lastHand.clear();
        }
        RestoreRapid();
        InvalidateCandidates(); s=ReadSnapshot();
        features.ammo.clear();
        features.clearInvulnerability=true;
        Log("PLAYER_LIFETIME map=%s client=%p command_time=%d god_requested=%d flags=0x%08x client_valid=%d other_flags=0x%08x move_flags=0x%08x takes_damage=%d",
            s.map,reinterpret_cast<void*>(s.client.address),s.client.commandTime,features.preferences.wanted(PlayerFeature::God),
            s.flags,s.client.valid,s.client.otherFlags,s.client.cheatFlags,s.takesDamage);
    }
    if (features.inventoryReconcilePending && s.client.valid && s.inventoryValid) {
        if (!features.inventory.sameScope(s.client.address,s.map)) {
            Log("WEAPON_LIFETIME new player/map; baseline cleared");
            features.inventory.clear(); features.inventoryIdentity={}; features.inventoryReconcilePending=false;
        } else if (InventoryAssetsValid()) {
            features.inventory.commandTime=s.client.commandTime;
            features.inventory.owned=features.inventory.owned || features.inventory.hasIntroduced(s.inventory);
            RestoreDual(s);
            if (!features.dualAsset.active && !HasDualPlayerEdits(features.dualSlots,features.dualAmmo,features.lastHand.owned)
                && RestoreInventory(ReadSnapshot())) {
                features.inventoryReconcilePending=false; s=ReadSnapshot();
                Log("WEAPON_LIFETIME original baseline retained; known bank IDs recovered across checkpoint");
            }
        }
        // Failed/transitional reads never become an inventory capture baseline.
        // Other PLAYER/WORLD controls still run through their independent gates.
    }
    // Absolute session state always wins over a checkpoint-restored God/demigod bit.
    // It starts OFF on every process launch; no cheat preferences are serialized.
    SetMasked(s.entity+kEntityFlags,3,features.preferences.wanted(PlayerFeature::God)?1:0,"GOD_FLAGS");
    if (!s.client.valid || s.client.health<=0 || s.client.moveType>=6) {
        features.ammo.clear(); RestoreDvars(); RestoreExtras(); return;
    }
    if (s.client.valid && s.client.health>0 && s.client.moveType<6) {
        SetMasked(s.client.address+kClientCheatFlags,3,features.preferences.wanted(PlayerFeature::Noclip)?1:0,"NOCLIP_FLAGS");
        // One-time OFF/launch/reset cleanup of the verified independent damage
        // guard. Do not continuously erase protection requested later by a mission.
        if (features.clearInvulnerability && !features.preferences.wanted(PlayerFeature::God)
            && s.damageRead && s.takesDamage && (s.client.moveType==0 || s.client.moveType==1)) {
            if (SetMasked(s.client.address+kClientOtherFlags,1,0,"INVULNERABILITY_CLEANUP"))
                features.clearInvulnerability=false;
        }
    }
    if (s.ammoValid) {
        const bool enabled=features.preferences.wanted(PlayerFeature::InfiniteAmmo);
        for (int n=0;n<kAmmoPoolCount;++n) {
            const auto pool=s.ammo[n]; if (!pool.id) { features.ammo.entries[n]={}; continue; }
            const auto at=s.client.address+kReserveAmmo+n*sizeof(AmmoPool);
            if (enabled && pool.count<kInfiniteReserve && SetReserve(at,pool,kInfiniteReserve))
                features.ammo.record(n,pool);
            else if (!enabled) {
                auto& owned=features.ammo.entries[n];
                if (features.ammo.canRestore(n,pool)) {
                    if (!SetReserve(at,pool,owned.original)) continue;
                    Log("PLAYER_RESTORE INFINITE_AMMO pool=%u value=%d",pool.id,owned.original);
                }
                owned={};
            }
        }
    }
    if (s.sustain.valid) {
        auto& owned=features.sustain;
        if (owned.dvar!=s.sustain.dvar) owned.clear();
        if (features.preferences.wanted(PlayerFeature::NoReload)) {
            if (s.sustain.current!=1 && SetBool(s.sustain.dvar+0x10,s.sustain.current,1)) {
                owned.applied(s.sustain.dvar,s.sustain.current,1);
                Log("PLAYER_APPLY NO_RELOAD before=%u after=1",s.sustain.current);
            }
        } else if (features.clearSustain || owned.owned) {
            const auto target=features.clearSustain?std::uint8_t{0}:owned.original;
            if (SetBool(s.sustain.dvar+0x10,s.sustain.current,target)) {
                if (s.sustain.current!=target) Log("PLAYER_RESTORE NO_RELOAD value=%u",target);
                owned.owned=false; features.clearSustain=false;
            }
        }
    }
    if (s.speed.valid) {
        auto& owned=features.speed;
        if (owned.dvar!=s.speed.dvar) owned.clear();
        if (features.preferences.wanted(PlayerFeature::SuperSpeed)) {
            const int target=features.preferences.speed;
            if (s.speed.current!=target && SetInt(s.speed.dvar+0x10,s.speed.current,target)) {
                owned.applied(s.speed.dvar,s.speed.current,target);
                Log("PLAYER_APPLY SUPER_SPEED before=%d after=%d",s.speed.current,target);
            }
        } else if (owned.owned || features.clearSpeed) {
            if (owned.owned && !owned.canRestore(s.speed.dvar,s.speed.current)) { owned.clear(); features.clearSpeed=false; }
            else if (!owned.owned && s.speed.current!=kSuperSpeed) features.clearSpeed=false;
            else if (SetInt(s.speed.dvar+0x10,s.speed.current,owned.owned?owned.original:kDefaultSpeed)) {
                Log("PLAYER_RESTORE SUPER_SPEED value=%d",owned.owned?owned.original:kDefaultSpeed);
                owned.owned=false; features.clearSpeed=false;
            }
        }
    }
    if (s.jump.valid && FeatureAvailable(s,PlayerFeature::SuperJump))
        SetJump(features.preferences.wanted(PlayerFeature::SuperJump)?static_cast<float>(features.preferences.jump):kDefaultJump);
    // Keep native global catalog bypass OFF while the filtered bank is active.
    ApplyBool(features.giveAll,s.giveAll,features.extra.wanted(ExtraFeature::GiveAll),0,"FILTERED_GIVE_ALL");
    if (!features.inventoryReconcilePending) UpdateWeaponInventory(s);
    else {
        RestoreDual(s); features.dualReady=false;
        ApplyBool(features.dual,s.dualWield,false,0,"FORCE_DUAL_WIELD");
    }
    ApplyBool(features.explosive,s.explosive,features.extra.wanted(ExtraFeature::ExplosiveBullets),1,"EXPLOSIVE_BULLETS");
    ApplyBool(features.ai,s.ai,features.extra.wanted(ExtraFeature::FreezeAI),0,"FREEZE_AI");
    ApplyFloat(features.time,s.timeScale,features.extra.wanted(ExtraFeature::SlowMotion),features.extra.timeScale,"SLOW_MOTION");
    if (ExtraAvailable(s,ExtraFeature::Gravity)) {
        const bool enabled=features.extra.wanted(ExtraFeature::Gravity);
        ApplyFloat(features.gravity,s.gravity,enabled,static_cast<float>(features.extra.gravity),"GRAVITY");
        if (enabled) SetGravityLiteral(static_cast<float>(features.extra.gravity)+.5f);
        else RestoreGravityLiteral();
    }
    auto held=s; // Bank changes refresh s; only the held timing needs a live read.
    unsigned current=0;
    if (ReadValue(s.client.address+kCurrentWeapon,current)) held.weapon=ReadWeapon(current);
    const bool rapid=features.extra.wanted(ExtraFeature::RapidFire);
    if (!rapid || !ExtraAvailable(held,ExtraFeature::RapidFire) || (features.rapid.owned && !features.rapid.sameIdentity(held.weapon))) RestoreRapid();
    // If restoring the previous asset must retry, keep its ownership record
    // instead of replacing it with the newly held gun.
    if (rapid && ExtraAvailable(held,ExtraFeature::RapidFire)
        && (!features.rapid.owned || features.rapid.sameIdentity(held.weapon))) {
        const int baseline=features.rapid.canRestore(held.weapon)?features.rapid.original:held.weapon.fireTime;
        const int target=RapidFireTime(baseline);
        if (held.weapon.fireTime!=target && SetInt(held.weapon.complete+kWeaponFireTime,held.weapon.fireTime,target)) {
            features.rapid.applied(held.weapon,target);
            Log("WEAPON_APPLY RAPID_FIRE index=%u name=%s fire_before=%d fire_after=%d",held.weapon.index,held.weapon.name,held.weapon.fireTime,target);
        }
    }
}
bool SetFov(int value) {
    const auto s=ReadSnapshot();
    const auto at=s.fov.dvar+kDvarFloatCurrent;
    if (!s.fov.valid || !FovWriteAllowed(s.supported,s.fov.resolved,s.state==LevelState::Ready,s.fov.current,value)
        || (at&3) || !Writable(at)) {
        Log("FOV blocked state=%s resolved=%d valid=%d current=%.3f requested=%d",
            StateText(s.state),s.fov.resolved,s.fov.valid,s.fov.current,value); return false;
    }
    const float target=static_cast<float>(value);
    if (s.fov.current==target) return true;
    static_assert(sizeof(LONG)==sizeof(float),"Atomic field write must stay four bytes");
    LONG before=0,after=0;
    std::memcpy(&before,&s.fov.current,sizeof(before)); std::memcpy(&after,&target,sizeof(after));
    if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),after,before)!=before) {
        Log("FOV concurrent value change; retry deferred"); return false;
    }
    Log("FOV applied current_before=%.3f current_after=%d map=%s",s.fov.current,value,s.map);
    return true;
}
void LogSnapshot() {
    const auto s = ReadSnapshot();
    Log("STATE %s supported=%d resolved=%d sv_running=%d map=%s entity=%p health=%d flags=0x%08x god=%d entity_number=%d player_read=%s fov_resolved=%d fov_valid=%d base_fov=%.3f",
        StateText(s.state), s.supported, s.resolved, s.serverRunning, s.map,
        reinterpret_cast<void*>(s.entity), s.health, s.flags, s.god, s.entityNumber, PlayerReadText(s.playerRead),
        s.fov.resolved,s.fov.valid,s.fov.current);
    Log("PLAYER_STATE client_resolved=%d client_valid=%d client=%p connected=%d client_health=%d command_time=%d move_type=%d other_flags=0x%08x move_flags=0x%08x damage_read=%d takes_damage=%d ammo_resolved=%d ammo_valid=%d sustain_resolved=%d sustain_valid=%d sustain=%u speed_resolved=%d speed_valid=%d speed=%d jump_resolved=%d jump_valid=%d jump_height=%.1f jump_twice=%.1f",
        s.clientResolved,s.client.valid,reinterpret_cast<void*>(s.client.address),s.client.connected,s.client.health,
        s.client.commandTime,s.client.moveType,s.client.otherFlags,s.client.cheatFlags,s.damageRead,s.takesDamage,
        s.ammoResolved,s.ammoValid,s.sustain.resolved,s.sustain.valid,s.sustain.current,s.speed.resolved,s.speed.valid,s.speed.current,
        s.jump.resolved,s.jump.valid,s.jump.height,s.jump.twiceHeight);
    if (s.ammoValid) {
        for (int n=0;n<kAmmoPoolCount;++n) if (s.ammo[n].id)
            Log("PLAYER_AMMO slot=%d pool=%u reserve=%d",n,s.ammo[n].id,s.ammo[n].count);
    }
    Log("EXTRA_STATE give_valid=%d give=%u explosive_valid=%d explosive=%u ai_valid=%d ai=%u time_valid=%d timescale=%.3f gravity_valid=%d gravity=%.1f literal_valid=%d gravity_literal=%.1f ps_gravity_read=%d ps_gravity=%d grenade_sustain_resolved=%d",
        s.giveAll.valid,s.giveAll.current,s.explosive.valid,s.explosive.current,s.ai.valid,s.ai.current,
        s.timeScale.valid,s.timeScale.current,s.gravity.valid,s.gravity.current,s.gravityLiteralValid,s.gravityLiteral,
        s.playerGravityRead,s.playerGravity,s.grenadeSustainResolved);
    Log("WEAPON_STATE resolved=%d last_loaded_index=%u valid=%d index=%u name=%s type=%d fire_time_ms=%d",
        s.weaponResolved,s.weaponCount,s.weapon.valid,s.weapon.index,s.weapon.name,s.weapon.type,s.weapon.fireTime);
    const auto w=ReadCandidate(s.weapon.index);
    std::uint8_t nativeVeto=255;
    if (s.weapon.valid) ReadValue(s.weapon.definition+kNoDualWield,nativeVeto);
    Log("WEAPON_INVENTORY resolved=%d valid=%d model=%d class=%d inventory_type=%d ammo_id=%u clip_id=%u clip_size=%d alternate=%u original_no_dual=%d native_no_dual=%u dual_valid=%d dual_value=%u dual_current_supported=%d hand=%d",
        s.inventoryResolved,s.inventoryValid,w.modelsValid,w.weaponClass,w.inventoryType,w.ammoId,w.clipId,w.clipSize,w.alternate,w.noDualWield,
        nativeVeto,s.dualWield.valid,s.dualWield.current,s.dualCurrentSupported,s.lastWeaponHand);
    if (s.inventoryValid) {
        for (int n=0;n<15;++n) if (s.inventory.weapons[n]) {
            const auto slot=ReadWeapon(s.inventory.weapons[n]);
            Log("WEAPON_SLOT slot=%d index=%u name=%s dual=%u model=%u",n,s.inventory.weapons[n],slot.name,s.inventory.metadata[n][1],s.inventory.metadata[n][2]);
        }
        for (int n=0;n<15;++n) if (s.inventory.clips[n].id)
            Log("WEAPON_CLIP slot=%d pool=%u right=%d left=%d",n,s.inventory.clips[n].id,s.inventory.clips[n].count[0],s.inventory.clips[n].count[1]);
    }
    {
        std::lock_guard<std::mutex> lock(featuresMutex);
        const auto& t=features.timing;
        const double scale=t.frequency?1000000.0/t.frequency:0.0;
        std::lock_guard<std::mutex> cacheLock(candidateMutex);
        Log("WEAPON_PERF updates=%llu average_us=%.1f peak_us=%.1f reads_per_update=%.1f cache_hits=%llu cache_misses=%llu dual_ready=%d dual_fallback=%d baseline_valid=%d",
            t.updates,t.updates?scale*t.ticks/t.updates:0.0,scale*t.peak,t.updates?double(t.reads)/t.updates:0.0,
            candidateCache.hits,candidateCache.misses,features.dualReady,features.dualAsset.fallback,features.inventory.baselineValid);
    }
}
} // namespace mod
