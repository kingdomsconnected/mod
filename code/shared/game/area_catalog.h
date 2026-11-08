#pragma once

#include <cstdint>
#include <vector>

namespace KCDC::Shared::Game {
    /** Which of the level's area entities a row came from; each has its own containment test. */
    enum class LevelAreaClass : std::uint8_t {
        TriggerArea,    // prism from triggerareas.fubar, tested as C_TriggerArea::IsPointInside
        AreaUnion,      // the union of its member TriggerAreas, as C_AreaUnion::IsPointInside
        SmartAreaShape, // a CryEngine area shape, tested as CArea::CalcPointWithinNonCached
    };

    /** `crime_doorKind`, the type a level gives an area's `crime_door` link. */
    enum class AreaDoorLink : std::uint8_t {
        Entrance,
        Basic,
        Storage,
    };

    /** One world-space polygon corner. */
    struct LevelAreaPoint {
        float x;
        float y;
        float z;
    };

    /** One `crime_door` link from an area to an AnimDoor, by the door's level GUID. */
    struct LevelAreaDoor {
        std::uint64_t doorGuid;
        AreaDoorLink link;
    };

    /**
     * One gameplay area placed in a shipped level.
     *
     * Mined out of the level paks by `tools/generate_area_catalog.py`, so the
     * server knows every one without a client, and tests them itself. `guid`
     * is what `IEntity::GetGuid()` returns, in the shuffled encoding of
     * `DoorPlacement::guid`, and is the key `triggerareas.fubar` uses.
     *
     * `boundsMin`/`boundsMax` are the box the engine checks before its
     * polygon test: for a TriggerArea every point plus the first one raised
     * by `height` (C_TriggerArea::SetShape, 0x18062EBFC); for a shape the
     * lowest point up to `height` above it -- every shipped shape has one,
     * and the generator refuses one that does not; for a union the box
     * around its members.
     */
    struct LevelArea {
        const char *level; // level folder name, e.g. `kutnohorsko`
        std::uint64_t guid;
        LevelAreaClass areaClass;
        const char *name;     // the entity name
        const char *labels;   // `Properties.Label`, trimmed and `;`-joined
        const char *location; // SmartAreaShape: the English name of its `guidLocationId` row, else empty
        float height;

        std::uint32_t firstPoint; // into g_LevelAreaPoints; none for a union
        std::uint32_t pointCount;
        std::uint32_t firstMember; // into g_LevelAreaMembers; unions only
        std::uint32_t memberCount;
        std::uint32_t firstDoor; // into g_LevelAreaDoors
        std::uint32_t doorCount;

        LevelAreaPoint boundsMin;
        LevelAreaPoint boundsMax;
    };

    /** The entity class a row came from, as the level names it. */
    inline const char *LevelAreaClassName(LevelAreaClass areaClass) {
        switch (areaClass) {
        case LevelAreaClass::TriggerArea: return "TriggerArea";
        case LevelAreaClass::AreaUnion: return "AreaUnion";
        case LevelAreaClass::SmartAreaShape: return "SmartAreaShape";
        }
        return "TriggerArea";
    }

    /** `crime_doorKind` as the level spells it. */
    inline const char *AreaDoorLinkName(AreaDoorLink link) {
        switch (link) {
        case AreaDoorLink::Entrance: return "entrance";
        case AreaDoorLink::Basic: return "basic";
        case AreaDoorLink::Storage: return "storage";
        }
        return "basic";
    }

    /** Every area across every shipped level, sorted by level then GUID. */
    extern const std::vector<LevelArea> g_LevelAreas;

    /** The polygons, each area's run contiguous. */
    extern const std::vector<LevelAreaPoint> g_LevelAreaPoints;

    /** The member TriggerArea GUIDs of each union, in the same level. */
    extern const std::vector<std::uint64_t> g_LevelAreaMembers;

    extern const std::vector<LevelAreaDoor> g_LevelAreaDoors;
} // namespace KCDC::Shared::Game
