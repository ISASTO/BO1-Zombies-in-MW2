#include "runtime.hpp"
#include <bcrypt.h>
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
} profile;
constexpr std::size_t kFlags = 0x150;
constexpr std::size_t kHealth = 0x164;
constexpr std::size_t kEntityStride = 0x2a0;

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
        return;
    }
}
Snapshot ReadSnapshot() {
    Snapshot s;
    s.supported = profile.supported;
    s.resolved = profile.resolved;
    if (!s.supported) { s.state = LevelState::Unsupported; return s; }
    if (!s.resolved) { s.state = LevelState::Unresolved; return s; }
    s.state = LevelState::Transition;
    std::uintptr_t server = 0, map = 0, mapString = 0;
    std::uint8_t running = 0;
    if (!NamedDvar(profile.serverDvarSlot, "sv_running", 0, server) || !ReadValue(server + 0x10, running) || running > 1) return s;
    s.serverRunning = running != 0;
    if (!s.serverRunning) { s.state = LevelState::NoLevel; return s; }
    if (!NamedDvar(profile.mapDvarSlot, "mapname", 7, map) || !ReadValue(map + 0x10, mapString) ||
        !ReadString(mapString, s.map, sizeof(s.map)) || !s.map[0]) return s;
    if (!ReadValue(profile.entitySlot, s.entity) || !InImage(s.entity, kEntityStride)) return s;
    int number = -1;
    if (!ReadValue(s.entity, number) || number != 0 || !ReadValue(s.entity + kHealth, s.health) ||
        !ReadValue(s.entity + kFlags, s.flags) || s.health < -100000 || s.health > 100000) return s;
    s.god = (s.flags & 1) != 0;
    s.state = s.health > 0 ? LevelState::Ready : LevelState::PlayerDead;
    return s;
}
bool ToggleGod() {
    // Single explicit atomic bit change. No freeze loop, patch, engine call, or copied player struct.
    const auto s = ReadSnapshot();
    if (s.state != LevelState::Ready || ((s.entity + kFlags) & 3) || !Writable(s.entity + kFlags)) {
        Log("GOD blocked: %s health=%d map=%s entity=%p", StateText(s.state), s.health, s.map, reinterpret_cast<void*>(s.entity));
        return false;
    }
    const LONG old = InterlockedXor(reinterpret_cast<volatile LONG*>(s.entity + kFlags), 1);
    Log("GOD %s flags_before=0x%08lx flags_after=0x%08lx map=%s health=%d", (old & 1) ? "OFF" : "ON",
        static_cast<unsigned long>(old), static_cast<unsigned long>(old ^ 1), s.map, s.health);
    return true;
}
void LogSnapshot() {
    const auto s = ReadSnapshot();
    Log("STATE %s supported=%d resolved=%d sv_running=%d map=%s entity=%p health=%d flags=0x%08x god=%d",
        StateText(s.state), s.supported, s.resolved, s.serverRunning, s.map,
        reinterpret_cast<void*>(s.entity), s.health, s.flags, s.god);
}
} // namespace mod
