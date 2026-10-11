#include "player_fields.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    using namespace mod;
    constexpr std::uintptr_t entity = 0x1000;
    std::array<unsigned char, 0x2a0> bytes{};
    // Independently encode the fields from native allocator/cheat/damage instructions.
    // +0x00 is a nonzero type; +0x7e is the WORD entity index. Adjacent bytes are unrelated.
    bytes[0] = 1;
    bytes[0x80] = 0x7b;
    bytes[0x81] = 0x3c;
    const std::int32_t health = 100;
    const std::uint32_t flags = 0x40;
    std::memcpy(bytes.data() + 0x164, &health, sizeof(health));
    std::memcpy(bytes.data() + 0x150, &flags, sizeof(flags));
    std::uintptr_t unreadable = 0;
    auto read = [&](std::uintptr_t at, void* out, std::size_t size) {
        if (at < entity || at == unreadable || size > bytes.size() || at - entity > bytes.size() - size) return false;
        std::memcpy(out, bytes.data() + at - entity, size); return true;
    };
    auto fields = ReadPlayerFields(entity, read);
    Check(fields.status == PlayerReadStatus::Complete && fields.number == 0 && fields.health == 100 && fields.flags == 0x40,
          "native slot-zero player is accepted despite nonzero type and unrelated adjacent WORD");
    bytes[0x7e] = 1;
    Check(ReadPlayerFields(entity, read).status == PlayerReadStatus::WrongSlot, "healthy slot-one entity is rejected");
    bytes[0x7e] = 0;
    unreadable = entity + 0x7e;
    Check(ReadPlayerFields(entity, read).status == PlayerReadStatus::NumberUnreadable, "unreadable identity fails closed");
    unreadable = entity + 0x164;
    Check(ReadPlayerFields(entity, read).status == PlayerReadStatus::HealthUnreadable, "unreadable health fails closed");
    unreadable = entity + 0x150;
    Check(ReadPlayerFields(entity, read).status == PlayerReadStatus::FlagsUnreadable, "unreadable flags fail closed");
    unreadable = 0;
    const std::int32_t deadHealth = 0;
    std::memcpy(bytes.data() + 0x164, &deadHealth, sizeof(deadHealth));
    fields = ReadPlayerFields(entity, read);
    Check(fields.status == PlayerReadStatus::Complete && fields.health == 0, "readable dead player differs from a failed health read");
    const std::int32_t invalidHealth = 100001;
    std::memcpy(bytes.data() + 0x164, &invalidHealth, sizeof(invalidHealth));
    Check(ReadPlayerFields(entity, read).status == PlayerReadStatus::InvalidHealth, "implausible health fails closed");
    std::cout << "Player field regression and failure-gate tests passed\n";
}
