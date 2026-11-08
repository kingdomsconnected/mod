#pragma once

#include "i_entity_iterator.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CEntitySystem;

    /** Cursor into CEntitySystem's generation-checked entity table. */
    struct EntityMapIteratorCursorLayout {
        std::uint16_t generation; // 0x00
        std::uint16_t padding0002;
        std::uint32_t index;      // 0x04
    };

    static_assert(sizeof(EntityMapIteratorCursorLayout) == 0x8,
                  "EntityMapIteratorCursorLayout size mismatch");

    /**
     * Concrete map-backed iterator returned by CEntitySystem.
     *
     * The table at 0x184077008 contains exactly the seven IEntityIt entries
     * and adds no virtual methods. GetEntityIterator's allocation and the
     * scalar deleting destructor both prove the complete 0x20-byte size.
     * Every traversal operation acquires the owning entity system's table
     * read lock and validates the generation/index cursor before exposing an
     * IEntity pointer.
     */
    class CEntityItMap : public IEntityIt {
      public:
        ~CEntityItMap() override = default;

        CEntitySystem *GetEntitySystem() const { return m_pEntitySystem; }
        std::int32_t GetReferenceCountRaw() const {
            return m_referenceCount;
        }
        const EntityMapIteratorCursorLayout &GetCursor() const {
            return m_cursor;
        }

      private:
        CEntitySystem *m_pEntitySystem;       // 0x08
        std::int32_t m_referenceCount;        // 0x10
        EntityMapIteratorCursorLayout m_cursor; // 0x14
    };

    static_assert(sizeof(CEntityItMap) == 0x20, "CEntityItMap size mismatch");
} // namespace SDK
