#pragma once

#include "../../types.h"
#include "i_entity.h"
#include "i_entity_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * The argument of `IEntitySystem::QueryProximity`.
     *
     * Not CryEngine's published 0x28-byte struct: Warhorse's carries its
     * result vector inline, backed by a 4096-byte arena good for 512 entity
     * pointers, and is 0x1060 bytes whole. The layout is
     * `CEntitySystem::QueryProximity` (0x180C887A4) read against its
     * constructor (0x180C8890C) and the buffer release (0x180C88FBC).
     *
     * Past 512 entities the vector grows into the game's allocator, which is
     * why construction and release both go through the natives rather than
     * being restated here.
     *
     * Game thread only.
     */
    class SEntityProximityQuery final {
      public:
        SEntityProximityQuery();
        ~SEntityProximityQuery();

        SEntityProximityQuery(const SEntityProximityQuery &)            = delete;
        SEntityProximityQuery &operator=(const SEntityProximityQuery &) = delete;

        int Count() const {
            return _count;
        }

        /** The caller has already bounded `index` against `Count()`. */
        IEntity *At(int index) const {
            return _begin[index];
        }

        /** The constructor leaves this alone, as the engine's own does. */
        AABB box {};

        /** Null for every class. */
        IEntityClass *entityClass = nullptr;

        /** Zero for every entity. */
        std::uint32_t entityFlags = 0;

      private:
        std::byte m_opaque0024[0x4];      // 0x024
        std::byte m_opaque0028[0x1008];   // 0x028, the arena and its in-use byte
        void *_arenaBase       = nullptr; // 0x1030
        void *_allocatorArena  = nullptr; // 0x1038, the vector's stateful allocator
        IEntity **_begin       = nullptr; // 0x1040
        IEntity **_end         = nullptr; // 0x1048
        IEntity **_capacityEnd = nullptr; // 0x1050
        int _count             = 0;       // 0x1058
        std::byte m_opaque105C[0x4];      // 0x105C

        static void AssertLayout() {
            static_assert(offsetof(SEntityProximityQuery, box) == 0x0000, "SEntityProximityQuery::box offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, entityClass) == 0x0018, "SEntityProximityQuery::entityClass offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, entityFlags) == 0x0020, "SEntityProximityQuery::entityFlags offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _arenaBase) == 0x1030, "SEntityProximityQuery::_arenaBase offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _allocatorArena) == 0x1038, "SEntityProximityQuery::_allocatorArena offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _begin) == 0x1040, "SEntityProximityQuery::_begin offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _end) == 0x1048, "SEntityProximityQuery::_end offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _capacityEnd) == 0x1050, "SEntityProximityQuery::_capacityEnd offset mismatch");
            static_assert(offsetof(SEntityProximityQuery, _count) == 0x1058, "SEntityProximityQuery::_count offset mismatch");
        }
    };

    static_assert(sizeof(SEntityProximityQuery) == 0x1060, "SEntityProximityQuery size mismatch");
} // namespace SDK
