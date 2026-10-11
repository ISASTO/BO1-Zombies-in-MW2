#pragma once
#include <cstddef>
#include <cstdint>

namespace mod {
// Partial fields for the exact supported executable, not a complete entity layout.
inline constexpr std::size_t kEntityStride = 0x2a0;
// Native G_Spawn assigns the array index with mov WORD PTR [entity+0x7e], dx.
// +0x00 is an entity-type byte, not a 32-bit entity number.
inline constexpr std::size_t kEntityNumber = 0x7e;
inline constexpr std::size_t kEntityFlags = 0x150;
inline constexpr std::size_t kEntityHealth = 0x164;

enum class PlayerReadStatus { NotRead, NumberUnreadable, WrongSlot, HealthUnreadable, InvalidHealth, FlagsUnreadable, Complete };
struct PlayerFields {
    PlayerReadStatus status = PlayerReadStatus::NotRead;
    int number = -1;
    int health = 0;
    unsigned flags = 0;
};
inline const char* PlayerReadText(PlayerReadStatus status) {
    switch (status) {
        case PlayerReadStatus::NotRead: return "NOT_READ";
        case PlayerReadStatus::NumberUnreadable: return "NUMBER_UNREADABLE";
        case PlayerReadStatus::WrongSlot: return "WRONG_ENTITY_NUMBER";
        case PlayerReadStatus::HealthUnreadable: return "HEALTH_UNREADABLE";
        case PlayerReadStatus::InvalidHealth: return "HEALTH_OUT_OF_RANGE";
        case PlayerReadStatus::FlagsUnreadable: return "FLAGS_UNREADABLE";
        case PlayerReadStatus::Complete: return "COMPLETE";
    }
    return "UNKNOWN";
}
template<class Reader>
PlayerFields ReadPlayerFields(std::uintptr_t entity, Reader read) {
    PlayerFields fields;
    std::uint16_t number = 0xffff;
    if (!read(entity + kEntityNumber, &number, sizeof(number))) {
        fields.status = PlayerReadStatus::NumberUnreadable; return fields;
    }
    fields.number = number;
    if (fields.number != 0) { fields.status = PlayerReadStatus::WrongSlot; return fields; }
    if (!read(entity + kEntityHealth, &fields.health, sizeof(fields.health))) {
        fields.status = PlayerReadStatus::HealthUnreadable; return fields;
    }
    if (fields.health < -100000 || fields.health > 100000) {
        fields.status = PlayerReadStatus::InvalidHealth; return fields;
    }
    if (!read(entity + kEntityFlags, &fields.flags, sizeof(fields.flags))) {
        fields.status = PlayerReadStatus::FlagsUnreadable; return fields;
    }
    fields.status = PlayerReadStatus::Complete;
    return fields;
}
} // namespace mod
