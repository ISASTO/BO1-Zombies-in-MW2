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
} profile;

struct FeatureRuntime {
    PlayerPreferences preferences;
    PlayerLifetime lifetime;
    ReserveOwnership ammo;
    bool clearInvulnerability = true;
    bool clearSustain = true;
    bool clearSpeed = true;
    ULONGLONG nextUpdate = 0;
    struct { bool owned=false; std::uintptr_t dvar=0; std::uint8_t original=0; } sustain;
    struct { bool owned=false; std::uintptr_t dvar=0; int original=kDefaultSpeed; } speed;
    bool jumpOwned=false;
} features;
std::mutex featuresMutex;

bool Read(std::uintptr_t address, void* output, std::size_t size) {
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
        && (s.height==kDefaultJump || s.height==kSuperJump) && s.twiceHeight==s.height*2.f;
    return s;
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
bool SetReserve(std::uintptr_t at, const AmmoPool& before, int target) {
    if ((at&7) || !Writable(at)) return false;
    const auto old=PackReserve(before),next=PackReserve({before.id,target});
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONGLONG*>(at),
        static_cast<LONGLONG>(next),static_cast<LONGLONG>(old)))==old;
}
// These two immutable floats are pooled only by the verified jump consumers.
// Change data in this process; never patch instructions or the disk executable.
bool SetJump(bool enabled) {
    const auto s=ReadJump();
    if (!s.valid) return false;
    const float height=enabled?kSuperJump:kDefaultJump, twice=height*2.f;
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
    bool ok=enabled?exchange(profile.jumpHeight,s.height,height):exchange(profile.jumpTwiceHeight,s.twiceHeight,twice);
    if (ok) {
        ok=enabled?exchange(profile.jumpTwiceHeight,s.twiceHeight,twice):exchange(profile.jumpHeight,s.height,height);
        if (!ok) {
            if (enabled) exchange(profile.jumpHeight,height,s.height);
            else exchange(profile.jumpTwiceHeight,twice,s.twiceHeight);
        }
    }
    DWORD unused=0;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(profile.jumpHeight),8,oldProtection,&unused)!=0;
    if (!ok || !restored) Log("PLAYER_APPLY SUPER_JUMP failed exchange=%d protection_restored=%d",ok,restored);
    if (ok) {
        features.jumpOwned=enabled;
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
        if (!speed.valid || speed.dvar!=features.speed.dvar || speed.current!=kSuperSpeed) features.speed.owned=false;
        else if (SetInt(speed.dvar+0x10,speed.current,features.speed.original)) {
            Log("PLAYER_RESTORE SUPER_SPEED value=%d",features.speed.original); features.speed.owned=false;
        }
    }
    if (features.jumpOwned) SetJump(false);
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
void ResetPlayerFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex);
    features.preferences.reset(); features.clearInvulnerability=true; features.clearSustain=true; features.clearSpeed=true; features.nextUpdate=0;
    Log("PLAYER_REQUEST RESET all_cheats=OFF saved_fov_unchanged=1");
}
void UpdatePlayerFeatures(ULONGLONG now) {
    std::lock_guard<std::mutex> lock(featuresMutex);
    if (now<features.nextUpdate) return;
    features.nextUpdate=now+16;
    const auto s=ReadSnapshot();
    if (s.state!=LevelState::Ready) {
        features.lifetime.observe(false,0,0,""); features.ammo.clear(); RestoreDvars(); return;
    }
    const bool newLifetime=features.lifetime.observe(true,s.client.address,s.client.commandTime,s.map);
    if (newLifetime) {
        features.ammo.clear();
        features.clearInvulnerability=true;
        Log("PLAYER_LIFETIME map=%s client=%p command_time=%d god_requested=%d flags=0x%08x client_valid=%d other_flags=0x%08x move_flags=0x%08x takes_damage=%d",
            s.map,reinterpret_cast<void*>(s.client.address),s.client.commandTime,features.preferences.wanted(PlayerFeature::God),
            s.flags,s.client.valid,s.client.otherFlags,s.client.cheatFlags,s.takesDamage);
    }
    // Absolute session state always wins over a checkpoint-restored God/demigod bit.
    // It starts OFF on every process launch; no cheat preferences are serialized.
    SetMasked(s.entity+kEntityFlags,3,features.preferences.wanted(PlayerFeature::God)?1:0,"GOD_FLAGS");
    if (!s.client.valid || s.client.health<=0 || s.client.moveType>=6) {
        features.ammo.clear(); RestoreDvars(); return;
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
        if (owned.dvar!=s.sustain.dvar) owned={false,s.sustain.dvar,0};
        if (features.preferences.wanted(PlayerFeature::NoReload)) {
            if (s.sustain.current!=1 && SetBool(s.sustain.dvar+0x10,s.sustain.current,1)) {
                owned={true,s.sustain.dvar,s.sustain.current};
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
        if (owned.dvar!=s.speed.dvar) owned={false,s.speed.dvar,kDefaultSpeed};
        if (features.preferences.wanted(PlayerFeature::SuperSpeed)) {
            if (s.speed.current!=kSuperSpeed && SetInt(s.speed.dvar+0x10,s.speed.current,kSuperSpeed)) {
                owned={true,s.speed.dvar,s.speed.current};
                Log("PLAYER_APPLY SUPER_SPEED before=%d after=%d",s.speed.current,kSuperSpeed);
            }
        } else if (owned.owned || features.clearSpeed) {
            if (s.speed.current!=kSuperSpeed) { owned.owned=false; features.clearSpeed=false; }
            else if (SetInt(s.speed.dvar+0x10,s.speed.current,owned.owned?owned.original:kDefaultSpeed)) {
                Log("PLAYER_RESTORE SUPER_SPEED value=%d",owned.owned?owned.original:kDefaultSpeed);
                owned.owned=false; features.clearSpeed=false;
            }
        }
    }
    if (s.jump.valid && FeatureAvailable(s,PlayerFeature::SuperJump))
        SetJump(features.preferences.wanted(PlayerFeature::SuperJump));
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
}
} // namespace mod
