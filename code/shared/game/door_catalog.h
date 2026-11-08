#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * `Properties.Lock.esLockTypes` -- what each side of a door takes to get
     * through it while it is locked. The first half is the front, the second
     * the back, before `bLockReversed` swaps them.
     */
    enum class DoorLockType : std::uint8_t {
        None,      // "none/none": nothing opens it but the script
        Manual,    // "manual/none": a bolt on one side, nothing on the other
        ManualKey, // "manual/key": a bolt on one side, a keyhole on the other
        Key        // "key/key": a keyhole on both
    };

    /** No row of `g_DoorModels`. */
    inline constexpr std::uint16_t kNoDoorModel = 0xFFFF;

    /**
     * One leaf mesh the levels hang in an AnimDoor, with the settings
     * AnimDoor.lua needs to swing it, as most of its placements give them.
     * Which side the hinge is on is a property of the mesh: the animation set
     * and the helpers name it, and every placement of a mesh agrees.
     */
    struct DoorModel {
        const char *path;            // `object_Model`, e.g. `objects/characters/assets/doors/lvl1_door_a_left.cdf`
        const char *animSet;         // `esDoorAnimSet`: `VillageLeft` or `VillageRight`
        const char *helpers;         // `soclass_SmartObjectHelpers`: `DoorLeft` or `DoorRight`
        const char *smartObjectType; // `guidSmartObjectType`
        std::uint16_t placements;    // how many doors the shipped levels hang it in
    };

    /** Every leaf mesh the shipped levels hang, sorted by path. */
    extern const std::vector<DoorModel> g_DoorModels;

    /** The row for a mesh path, matched case-insensitively, or `kNoDoorModel`. */
    inline std::uint16_t FindDoorModelIndex(const char *path) {
        for (std::size_t index = 0; index < g_DoorModels.size(); ++index) {
            const char *a = g_DoorModels[index].path;
            const char *b = path;
            while (*a != '\0' && *b != '\0' && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b == '\\' ? '/' : *b))) {
                ++a;
                ++b;
            }
            if (*a == '\0' && *b == '\0') {
                return static_cast<std::uint16_t>(index);
            }
        }
        return kNoDoorModel;
    }

    /** The row at `index`, or null. */
    inline const DoorModel *DoorModelAt(std::uint16_t index) {
        return index < g_DoorModels.size() ? &g_DoorModels[index] : nullptr;
    }

    /**
     * One AnimDoor placed in a shipped level.
     *
     * Mined out of the level paks by `tools/generate_door_catalog.py`, so the
     * server knows every door before anyone walks past it, and knows where it
     * stands without taking a client's word for it. Every lock field is the
     * placement's own value, or AnimDoor.lua's default where it sets none.
     *
     * `guid` is what `IEntity::GetGuid()` returns, shuffled from the level's
     * EntityGuid string the way `ParseEntityGuidString` does it -- see
     * `GatePlacement::guid`.
     */
    struct DoorPlacement {
        const char *level; // level folder name, e.g. `kutnohorsko`
        std::uint64_t guid;
        const char *name; // the entity name, as `IEntity::GetName()` returns it

        float positionX;
        float positionY;
        float positionZ;

        /** The horizontal part of `IEntity::GetForwardDir()`, normalized. */
        float forwardX;
        float forwardY;

        /** `guidItemClassId`: the item class that works the keyhole, empty for none. */
        const char *keyItemClass;

        DoorLockType lockType;

        /**
         * Locked when the level is built: `bLocked`, or `bLockOutside`, which
         * AnimDoor:Reset applies through `ShouldBeLocked(false)`.
         */
        bool startsLocked;

        /** `bLockReversed`: the keyhole is on the front rather than the back. */
        bool lockReversed;

        /** `bCanLockPick`. */
        bool lockpickable;

        /** `bCanUnlockWithDynamicKey`: the generated home or shop key works it too. */
        bool dynamicKey;

        /** The leaf it hangs, a row of `g_DoorModels`. */
        std::uint16_t model = kNoDoorModel;
    };

    /** Every AnimDoor placement across every shipped level. */
    extern const std::vector<DoorPlacement> g_DoorPlacements;

    /** The placement for one level GUID, or null when it is not a door the level places. */
    inline const DoorPlacement *FindDoorPlacement(const char *level, std::uint64_t guid) {
        for (const DoorPlacement &placement : g_DoorPlacements) {
            if (placement.guid == guid && std::strcmp(placement.level, level) == 0) {
                return &placement;
            }
        }
        return nullptr;
    }

    /**
     * How far in front of the door's plane a point stands, in metres; negative
     * behind it. AnimDoor:IsAccessedFromFront calls a point in front when
     * `dot(doorPos - point, forward) >= 0`, so "front" is the side the
     * forward axis points away from, and this is that dot product's sign.
     */
    inline float DoorFrontDistance(const DoorPlacement &door, float x, float y) {
        return (door.positionX - x) * door.forwardX + (door.positionY - y) * door.forwardY;
    }

    /** AnimDoor:IsOnKeySide: whether a body on this side needs a key or a pick to get through. */
    inline bool IsDoorKeySide(const DoorPlacement &door, bool fromFront) {
        switch (door.lockType) {
        case DoorLockType::ManualKey: return door.lockReversed ? fromFront : !fromFront;
        case DoorLockType::Key: return true;
        case DoorLockType::None:
        case DoorLockType::Manual: return false;
        }
        return false;
    }
} // namespace KCDC::Shared::Game
