#pragma once

#include "shared/game/prefab_property.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** What a smart object is for, which decides what the server does with it. */
    enum class SmartObjectKind : std::uint8_t {
        Seat,      // sat on: benches, chairs
        Bed,       // lain on and slept in: beds, a tent
        Dice,      // a dice table
        Alchemy,   // an alchemy table
        Smithing,  // a smithy's anvil and forge
        Drying,    // a drying rack for meat and herbs
        Smoking,   // a smokehouse
        Sharpening // a grindstone
    };

    /** An entity link from one part to another, as the prefab draws it. */
    struct SmartObjectLink {
        std::uint8_t target = 0; // index into the blueprint's parts
        std::string_view name;   // the link's name, often empty
    };

    /**
     * One entity of a smart object, as its prefab describes it, posed in the
     * prefab's own frame. Every part is spawned under a GUID every client
     * agrees on (`SmartObjectPartGuid`), so a seat, a station or a dice
     * table is named the same on every machine, as the level's own are.
     */
    struct SmartObjectPart {
        std::string_view name;        // the prefab's own name, for logs
        std::string_view entityClass; // `StanceSmartObject`, `ActionTrigger`, `AlchemyTable`, ...
        /**
         * The model of a part the editor placed as a `GeomEntity` -- a
         * `.cgf`, or a `.cdf` for one that animates, like the forge's bag --
         * loaded once it is initialised, as the level loader loads it; empty
         * for one whose script loads its own.
         */
        std::string_view geometry;

        float posX = 0.0F, posY = 0.0F, posZ = 0.0F;
        float rotX = 0.0F, rotY = 0.0F, rotZ = 0.0F, rotW = 1.0F;
        float scaleX = 1.0F, scaleY = 1.0F, scaleZ = 1.0F;

        /** The prefab's `Properties`, given to the entity before it is initialised, as the level loader gives them. */
        const PrefabProperty *properties = nullptr;
        std::size_t propertyCount        = 0;

        const SmartObjectLink *links = nullptr;
        std::size_t linkCount        = 0;

        /**
         * An `ItemSlot` the station is linked to -- an alchemy table's recipe
         * book, a smithy's tongs and hammer -- which the client lets spawn its
         * item, as it does the level's own fixtures.
         */
        bool fixture = false;
    };

    /** One static mesh of a smart object, posed in the prefab's own frame. */
    struct SmartObjectBrush {
        std::string_view mesh; // `objects/...cgf`
        float posX = 0.0F, posY = 0.0F, posZ = 0.0F;
        float rotX = 0.0F, rotY = 0.0F, rotZ = 0.0F, rotW = 1.0F;
        float scaleX = 1.0F, scaleY = 1.0F, scaleZ = 1.0F;
    };

    /** A smart object the mod can place, and what it comes up as. */
    struct SmartObjectBlueprint {
        std::string_view name;       // the prefab's path under `Prefabs/`, e.g. `Bench/bench_2places_low`
        std::string_view prefabGuid; // the editor's prefab id, for tracing a blueprint back
        SmartObjectKind kind = SmartObjectKind::Seat;

        const SmartObjectPart *parts = nullptr;
        std::size_t partCount        = 0;

        const SmartObjectBrush *brushes = nullptr;
        std::size_t brushCount          = 0;

        /** The part a station is: the `AlchemyTable`, `Smithery`, `DiceInteractor`, ...; -1 for a seat or a bed. */
        int stationPart = -1;
    };

    /**
     * Every interactive prefab the game ships that the mod can place on its
     * own: seats, beds, a dice table, alchemy tables, a smithy, the drying
     * rack and the smokehouse. Mined out of `Prefabs/` in
     * `Data/IPL_GameData.pak` by `tools/generate_smart_object_catalog.py`,
     * minus the AI fittings (`PrefabPort`, `SchedulerHub`, `GhostDummy`) a
     * player never touches, and the shop a smithy carries.
     */
    extern const std::vector<SmartObjectBlueprint> g_SmartObjectBlueprints;

    /** The blueprint of that name, matched case-insensitively, or null. */
    const SmartObjectBlueprint *FindSmartObjectBlueprint(std::string_view name);

    /** How a script spells a kind. */
    const char *SmartObjectKindName(SmartObjectKind kind);

    /**
     * The high bits of the EntityGuid of every part of a smart object the
     * server places, above the serial and the part index that make it
     * unique: no level entity has them, and every client picks the same one.
     */
    inline constexpr std::uint64_t kSmartObjectGuidTag  = 0xF5AA000000000000ULL;
    inline constexpr std::uint64_t kSmartObjectGuidMask = 0x0000FFFFFFFFFFFFULL;

    inline constexpr std::uint64_t SmartObjectPartGuid(std::uint64_t serial, std::size_t part) {
        return kSmartObjectGuidTag | (((serial << 8) | (part & 0xFFU)) & kSmartObjectGuidMask);
    }

    inline constexpr bool IsSmartObjectGuid(std::uint64_t guid) {
        return (guid & ~kSmartObjectGuidMask) == kSmartObjectGuidTag;
    }
} // namespace KCDC::Shared::Game
