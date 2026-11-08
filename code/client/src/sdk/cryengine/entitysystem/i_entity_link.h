#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One named link from an entity to another, as the level editor placed
     * it: CryEngine's `IEntityLink`,
     * with KCD2's 8-byte EntityGUID. The list is the entity's own, walked
     * from `IEntity::GetEntityLinks`; `C_Dice::InitializeDiceTableScene`
     * (0x182E8C3E8) reads exactly these three fields to find a dice table's
     * seats and tag points.
     */
    struct IEntityLink {
        CryStringT<char> name;   // 0x00
        EntityId entityId;       // 0x08
        std::uint64_t entityGuid; // 0x10
        IEntityLink *next;       // 0x18
    };

    static_assert(offsetof(IEntityLink, name) == 0x00, "IEntityLink::name offset mismatch");
    static_assert(offsetof(IEntityLink, entityId) == 0x08, "IEntityLink::entityId offset mismatch");
    static_assert(offsetof(IEntityLink, entityGuid) == 0x10, "IEntityLink::entityGuid offset mismatch");
    static_assert(offsetof(IEntityLink, next) == 0x18, "IEntityLink::next offset mismatch");
    static_assert(sizeof(IEntityLink) == 0x20, "IEntityLink size mismatch");
} // namespace SDK
