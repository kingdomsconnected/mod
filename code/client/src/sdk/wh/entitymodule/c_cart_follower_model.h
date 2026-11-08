#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../types.h"
#include "c_cart_path.h"
#include "e_cart_horse_slot.h"
#include "s_cart_harness_placement.h"
#include "i_cart.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One point of a cart's path cursor, 24 bytes. The cursor is a small tree:
     * a root (`parent` -1) that travels the path, and the chassis's front and
     * back points hung off it at a signed distance (`offset`), placed ahead or
     * behind by the root's `direction` -- 0 carries the cart towards rising
     * path distance, 1 towards falling. Laid out by `SetCartPath`: entry 1 is
     * the root (0x1828F0C18), entries 0 and 2 the front and back points with
     * `parent` 1 (0x1828F0CAC). Advanced by `AdvanceCartPathCursor`
     * (0x1828F1624), which turns a root round -- within fifty metres of
     * either end of the path -- only when a point could not advance.
     */
    struct S_CartPathCursorEntry {
        std::int32_t id;        // 0x00
        std::int32_t parent;    // 0x04, -1 for a root
        float offset;           // 0x08, from the parent, along its direction
        float distance;         // 0x0C, path distance of this point
        std::int32_t direction; // 0x10, 0 rising distance, 1 falling
        std::int32_t node;      // 0x14, the path node the point is past
    };

    static_assert(sizeof(S_CartPathCursorEntry) == 0x18, "S_CartPathCursorEntry size mismatch");

    /**
     * Where a cart is on its path, 0x40 bytes at C_CartFollowerModel+0xE8:
     * the entry tree, the ids of its roots, and its own reference to the
     * path. What `AdvanceCartPathCursor` (0x1828F1624) is handed every tick.
     */
    struct S_CartPathCursor {
        std::size_t GetEntryCount() const {
            return static_cast<std::size_t>(pEntriesEnd - pEntries);
        }

        S_CartPathCursorEntry &GetEntry(std::size_t index) const {
            return pEntries[index];
        }

        /** Points every root towards rising path distance. */
        void FaceRootsForward() const {
            for (S_CartPathCursorEntry *entry = pEntries; entry != pEntriesEnd; ++entry) {
                if (entry->parent == -1) {
                    entry->direction = 0;
                }
            }
        }

        /**
         * How far ahead of the root the cursor's foremost point rides. The
         * path has to reach that far past wherever the root is sent: a point
         * that cannot advance fails `AdvanceCursorEntries` (0x1828FA860), and
         * the cursor then turns round on the spot every tick.
         */
        float GetFrontReach() const {
            float reach = 0.0F;
            for (const S_CartPathCursorEntry *entry = pEntries; entry != pEntriesEnd; ++entry) {
                if (entry->parent != -1) {
                    reach = std::max(reach, entry->offset);
                }
            }
            return reach;
        }

        /**
         * How far behind the root the cursor's rearmost point rides: the
         * road a cart backing up needs behind wherever its root is sent. The
         * mirror of `GetFrontReach`: past the start of an open path
         * `AdvanceCursorRoot` (0x1828F1740) has no node to continue on and
         * the advance fails the same way.
         */
        /**
         * The furthest path node any point of the cursor is past: the
         * nodes up to the one after it are the road the cart stands on.
         */
        std::int32_t GetLeadingNode() const {
            std::int32_t leading = 0;
            for (const S_CartPathCursorEntry *entry = pEntries; entry != pEntriesEnd; ++entry) {
                leading = std::max(leading, entry->node);
            }
            return leading;
        }

        /** The nearest path node any point of the cursor is past: the road behind it is road the cart has left. */
        std::int32_t GetTrailingNode() const {
            std::int32_t trailing = pEntries != pEntriesEnd ? pEntries->node : 0;
            for (const S_CartPathCursorEntry *entry = pEntries; entry != pEntriesEnd; ++entry) {
                trailing = std::min(trailing, entry->node);
            }
            return std::max(trailing, 0);
        }

        float GetBackReach() const {
            float reach = 0.0F;
            for (const S_CartPathCursorEntry *entry = pEntries; entry != pEntriesEnd; ++entry) {
                if (entry->parent != -1) {
                    reach = std::max(reach, -entry->offset);
                }
            }
            return reach;
        }

        S_CartPathCursorEntry *pEntries;    // 0x00, std::vector: the front point, the root, the back point
        S_CartPathCursorEntry *pEntriesEnd; // 0x08
        void *pEntriesCapacity;             // 0x10
        std::byte roots[0x18];              // 0x18, std::vector<int> of root ids
        C_CartPath *pPath;                  // 0x30, std::shared_ptr<C_CartPath>
        void *pPathControl;                 // 0x38
    };

    static_assert(sizeof(S_CartPathCursor) == 0x40, "S_CartPathCursor size mismatch");
    static_assert(offsetof(S_CartPathCursor, pPath) == 0x30, "S_CartPathCursor::pPath offset mismatch");

    /**
     * How a cart's parts follow its path, 0x1F8 bytes at C_Cart+0x98.
     *
     * Built by `C_Cart::SetupCartOnReset` (0x180F8F588) out of the entity
     * tree the `WHCart` hangs in -- `ConstructCartFollowerModel` (0x180F90414)
     * then `BuildCartFollowers` (0x180F91238) -- whenever the `WHCart`'s parent,
     * or that parent's parent, is a `GeomEntity`. It is not a path, whatever
     * the field was once taken for: the path is a separate `C_CartPath` this
     * holds a reference to, and a cart spawned without one still gets a
     * complete follower model.
     *
     * `UpdateCartFollowers` (0x180F8DB48) is what poses a moving cart from it:
     * it advances the path cursor by `dt * speed`, sets the chassis root's
     * world transform from the cursor's front point, articulates the back
     * part from its rear point at `slt_back_part`, turns both wheel pairs by
     * the distance rolled over their measured radius, and walks each hitched
     * horse to its `slt_horse_*` helper through the horse's own movement
     * controller. It does all of that only while the cursor has a path.
     */
    class C_CartFollowerModel {
      public:
        /** The chassis root, whose world transform the movement writes. */
        IEntity *GetRootEntity() const {
            return m_pRootEntity;
        }

        /**
         * The harness entry of the horse in `slot`, as `UpdateCartFollowers`
         * hands it to `DriveHorseToHarness` (0x180F8E9FF / 0x180F8EC0A load it
         * from here); nullptr with no horse there. Opaque: the 88-byte entry
         * is modeled only from +0x10 (`S_CartHarnessHorse`).
         */
        const void *GetHarnessEntry(E_CartHorseSlot::Type slot) const {
            return slot == E_CartHorseSlot::Left ? m_pHarnessLeft : m_pHarnessRight;
        }

        /**
         * Where the horse of `slot` stands, in the chassis root's own frame:
         * the `slt_horse_left` / `slt_horse_right` helper of the chassis mesh,
         * read by `BuildCartFollowers` (0x180F91238). `UpdateCartFollowers`
         * hands the harness walker the chassis pose composed with it.
         */
        const QuatT &GetHarnessHelper(E_CartHorseSlot::Type slot) const {
            return slot == E_CartHorseSlot::Left ? m_horseHelperLeft : m_horseHelperRight;
        }

        /**
         * Turns each pair of wheels by how far it has rolled since the last
         * call (`RollCartWheel`, 0x1828FA3C8): the wheel entity's own world
         * movement along `forward`, over its measured radius, as a turn about
         * its axle. It reads the wheels where they stand, so it rolls them
         * under any pose the chassis was given, as `UpdateCartFollowers` calls
         * it after posing the cart.
         */
        void RollWheels(const Vec3 &forward) {
            hook::call<void>(gPatterns.RollCartWheel, m_frontWheels, &forward);
            hook::call<void>(gPatterns.RollCartWheel, m_backWheels, &forward);
        }

        /**
         * Steps the horse of `slot` to `placement` (`DriveHorseToHarness`,
         * 0x1828E15F8), as `UpdateCartFollowers` does once a tick per horse:
         * within 0.75 m it walks it there with a movement request that keeps
         * the harness heading, past that -- or with `placeOutright` -- it puts
         * it there; with `motion` zero it only draws the reins.
         */
        void DriveHorse(E_CartHorseSlot::Type slot, float frameTime, const S_CartHarnessPlacement &placement) {
            void *entry = slot == E_CartHorseSlot::Left ? m_pHarnessLeft : m_pHarnessRight;
            if (entry != nullptr) {
                hook::call<void>(gPatterns.DriveHorseToHarness, entry, frameTime, &placement);
            }
        }

        /** The path this cart is bound to, or nullptr while it has none. */
        C_CartPath *GetPath() const {
            return m_pPath;
        }

        /**
         * Binds the road keyed to `pathEntity` (`SetCartPath`, 0x180F8ED44),
         * or releases the one bound when that is null. The path manager hands
         * back the road it has cached for the entity or builds one --
         * `C_CartPath::BuildFromPathEntity` -- and the cart is projected onto
         * it; with `placeNow` it is put there at once and every part posed.
         * False when the road could not be built.
         */
        bool BindPath(IEntity *pathEntity, bool placeNow) {
            return hook::call<bool>(gPatterns.SetCartPath, this, pathEntity, placeNow);
        }

        /** Whether the cursor has a path to travel, which is `UpdateCartFollowers`' own gate. */
        bool HasCursorPath() const {
            return m_cursor.pPath != nullptr;
        }

        S_CartPathCursor *GetCursor() {
            return &m_cursor;
        }

        const S_CartPathCursor *GetCursor() const {
            return &m_cursor;
        }

      private:
        IEntity *m_pRootEntity;        // 0x00
        void *m_pBackPartFollower;     // 0x08, a 0xF8-byte follower of its own for the back part (BuildCartFollowers 0x180F91238); null on a two-wheeler
        void *m_pHarnessLeft;          // 0x10, the hitched horse's entry for E_CartHorseSlot::Left
        void *m_pHarnessRight;         // 0x18
        I_Cart *m_pCart;               // 0x20, the C_Cart that owns this model
        std::byte m_frontWheels[0x58]; // 0x28, rolled by RollCartWheel (0x1828FA3C8)
        std::byte m_backWheels[0x58];  // 0x80
        C_CartPath *m_pPath;           // 0xD8, std::shared_ptr<C_CartPath>
        void *m_pPathControl;          // 0xE0
        S_CartPathCursor m_cursor;     // 0xE8
        std::uint8_t m_state0128;      // 0x128, ConstructCartFollowerModel180F90414 zeroes
        std::byte m_opaque0129[0x13];
        QuatT m_backPartHelper;    // 0x13C, slt_back_part, where the body pivots
        QuatT m_horseHelperLeft;   // 0x158, slt_horse_left
        QuatT m_horseHelperRight;  // 0x174, slt_horse_right
        std::byte m_opaque0190[0x38];
        float m_value01C8; // 0x1C8, ctor zeroes
        float m_pitch; // 0x1CC
        std::uint32_t m_value01D0; // 0x1D0, ctor zeroes
        float m_placeDistance;         // 0x1D4, a snap PlaceAtPathPosition asks for; -1 when none
        std::int32_t m_placeDirection; // 0x1D8, the root's direction for the pending placement (entry 1's +0x10, SetupCartOnReset)
        bool m_placeDirectionPending;  // 0x1DC
        std::byte m_padding01DD[0x3];
        void *m_values01E0[3];         // 0x1E0, ctor zeroes all three (a vector-shaped triple)

        static void AssertLayout() {
            static_assert(offsetof(C_CartFollowerModel, m_pBackPartFollower) == 0x08, "C_CartFollowerModel::m_pBackPartFollower offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_pCart) == 0x20, "C_CartFollowerModel::m_pCart offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_frontWheels) == 0x28, "C_CartFollowerModel::m_frontWheels offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_backWheels) == 0x80, "C_CartFollowerModel::m_backWheels offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_pPath) == 0xD8, "C_CartFollowerModel::m_pPath offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_cursor) == 0xE8, "C_CartFollowerModel::m_cursor offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_backPartHelper) == 0x13C, "C_CartFollowerModel::m_backPartHelper offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_horseHelperLeft) == 0x158, "C_CartFollowerModel::m_horseHelperLeft offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_horseHelperRight) == 0x174, "C_CartFollowerModel::m_horseHelperRight offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_pitch) == 0x1CC, "C_CartFollowerModel::m_pitch offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_placeDistance) == 0x1D4, "C_CartFollowerModel::m_placeDistance offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_placeDirectionPending) == 0x1DC, "C_CartFollowerModel::m_placeDirectionPending offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_state0128) == 0x128, "C_CartFollowerModel::m_state0128 offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_value01C8) == 0x1C8, "C_CartFollowerModel::m_value01C8 offset mismatch");
            static_assert(offsetof(C_CartFollowerModel, m_values01E0) == 0x1E0, "C_CartFollowerModel::m_values01E0 offset mismatch");
        }
    };

    static_assert(sizeof(C_CartFollowerModel) == 0x1F8, "C_CartFollowerModel size mismatch");
} // namespace SDK::wh::entitymodule
