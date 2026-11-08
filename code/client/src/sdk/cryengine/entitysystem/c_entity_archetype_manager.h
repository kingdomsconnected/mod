#pragma once

#include "c_entity_archetype.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Descriptive layout for one node of the archetype registry's MSVC
     * `std::map<CryStringT<char>, _smart_ptr<CEntityArchetype>>`. This is an
     * ABI overlay for a private container, not a recovered source type.
     *
     * The registry lookup at 0x1803B87D8 proves every field it uses: it walks
     * from the head sentinel's parent, stops on the `_Isnil` byte at +0x19,
     * compares the key at +0x20 with `_stricmp`, and descends through +0x00
     * and +0x10. The archetype value is read at +0x28 by 0x180A6E2C4.
     */
    struct EntityArchetypeMapNodeLayout {
        EntityArchetypeMapNodeLayout *left;   // 0x00
        EntityArchetypeMapNodeLayout *parent; // 0x08
        EntityArchetypeMapNodeLayout *right;  // 0x10
        std::uint8_t color;                   // 0x18
        std::uint8_t isNil;                   // 0x19
        std::byte padding001A[0x6];
        const char *name;            // 0x20, CryStringT<char> payload pointer
        CEntityArchetype *archetype; // 0x28

        bool IsNil() const {
            return isNil != 0;
        }
    };

    static_assert(sizeof(EntityArchetypeMapNodeLayout) == 0x30, "EntityArchetypeMapNodeLayout size mismatch");

    /**
     * Descriptive layout for CEntitySystem's entity-archetype registry.
     *
     * Only the leading map is proven, so no total extent is asserted.
     * `IEntitySystem::LoadEntityArchetype` (0x180A6E1F4) looks the requested
     * name up in this map and, on a miss, loads the library named by the part
     * of the name before the first `.` from `Libs/EntityArchetypes/<lib>.xml`
     * (0x180A6DD00) before retrying. The library registers one archetype per
     * `EntityPrototype` node, keyed `<library>.<Name>`.
     *
     * The map therefore holds exactly the archetypes the running game has
     * already resolved -- the ones the loaded level placed entities with, plus
     * anything a later lookup pulled in.
     */
    struct EntityArchetypeManagerLayout {
        EntityArchetypeMapNodeLayout *mapHead; // 0x00, MSVC _Myhead sentinel
        std::size_t archetypeCount;            // 0x08, MSVC _Mysize

        /** Smallest key, or null when the registry is empty. */
        const EntityArchetypeMapNodeLayout *FirstArchetype() const;

        /** In-order successor, or null once the walk is complete. */
        static const EntityArchetypeMapNodeLayout *NextArchetype(const EntityArchetypeMapNodeLayout *node);
    };
} // namespace SDK
