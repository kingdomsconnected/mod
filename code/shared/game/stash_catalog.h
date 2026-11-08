#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * One Stash placed in a shipped level: a chest, a box, a barrel.
     *
     * Mined out of the level paks by `tools/generate_stash_catalog.py`, so the
     * server can address, lock and gate a container the level places without
     * taking a client's word that it exists. Every lock field is the
     * placement's own value, or AnimStash.lua's default where it sets none.
     *
     * `guid` is what `IEntity::GetGuid()` returns, shuffled from the level's
     * EntityGuid string the way `ParseEntityGuidString` does it -- see
     * `GatePlacement::guid`.
     */
    struct StashPlacement {
        const char *level; // level folder name, e.g. `kutnohorsko`
        std::uint64_t guid;
        const char *name; // the entity name, as `IEntity::GetName()` returns it

        float positionX;
        float positionY;
        float positionZ;

        /**
         * The horizontal part of the entity's forward axis, normalized.
         * AnimStash.lua's IsUsable offers the container only to a body whose
         * offset to it runs along this within `fUseAngle` (0.7).
         */
        float forwardX;
        float forwardY;

        /** `fUseDistance`: how far from the chest AnimStash.lua's IsUsable offers it. */
        float useDistance;

        /** `guidItemClassId`: the item class that unlocks it, empty for none. */
        const char *keyItemClass;

        /**
         * The stash whose inventory this one opens, through a `masterStash`
         * link, or 0. Only links that reach another Stash in the same level;
         * the engine opens a stash's own inventory for the rest.
         */
        std::uint64_t masterGuid;

        /** `bLocked`: AnimStash:Reset locks it when the level is built. */
        bool startsLocked;

        /** `bCanLockPick`. */
        bool lockpickable;

        /** `bCanUnlockWithDynamicKey`: the generated home or shop key works it too. */
        bool dynamicKey;
    };

    /** Every Stash placement across every shipped level. */
    extern const std::vector<StashPlacement> g_StashPlacements;

    /** The placement for one level GUID, or null when it is not a stash the level places. */
    inline const StashPlacement *FindStashPlacement(const char *level, std::uint64_t guid) {
        for (const StashPlacement &placement : g_StashPlacements) {
            if (placement.guid == guid && std::strcmp(placement.level, level) == 0) {
                return &placement;
            }
        }
        return nullptr;
    }
} // namespace KCDC::Shared::Game
