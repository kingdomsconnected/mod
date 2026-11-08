#pragma once

#include "../../patterns.h"

#include "../../cryengine/action/i_actor.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * A pile of hurling stones, the game object extension of the
     * `StoneThrowingPile` entity class (`StoneThrowingPile.lua` derives it
     * from `UsableItem`; its prompt asks `StoneThrowing.CanUse` and its use
     * starts kind 9 of the minigames on the user).
     *
     * Everything it knows comes from its entity links, walked by
     * `ForEachPileLink` (0x180D7DBF4) whenever the pile is reset: a link to a
     * `StoneThrowingNode` starts the path the thrower stands on (followed on
     * through each node's own links), a link to a `GeomEntity` is one of its
     * stones, and a link named `pickPoint` to anything else is where the hands
     * reach down. With no stone or no path it is unusable ("The pile is
     * invalid: no stones or no path"). A path of one node -- one record whose
     * two ends coincide -- makes every grab throw at once
     * (`ShouldAutoThrowAfterGrab`, 0x182EABFD4); that is the `walls` piles the
     * Nebakov and Suchdol battlements are built from.
     *
     * A stone has three states (`SetStoneUsageState`, 0x180D7F28C): 0 lying
     * in the pile, static; 1 held, without physics; 2 thrown, a 300 kg rigid
     * body pushed once by `wh_pl_StoneThrowingLaunchStrength` pitched up by
     * `wh_pl_StoneThrowingLaunchXAngle` along the user's facing. Nothing
     * the pile does deals damage.
     *
     * 0xE0 bytes: `IGameFramework`'s creator for the class allocates exactly
     * that (0x18172B364) before the constructor at 0x18172B3E8.
     */
    class C_StoneThrowingPile: public SDK::IGameObjectExtension {
      public:
        /** The pile extension of an entity, or null when it is not a `StoneThrowingPile` (0x182EAB590). */
        static C_StoneThrowingPile *Find(EntityId entityId) {
            return hook::call<C_StoneThrowingPile *>(gPatterns.C_StoneThrowingPile__Find, entityId);
        }

        /**
         * Rebuilds the pile from its links and puts every stone back in it,
         * static, with the next one ready to be grabbed (0x180D7DD78). What
         * the entity events 36 and 51 run; a pile spawned at runtime has its
         * links only after it was initialized, so it is run again once they
         * are in.
         */
        void Reset() {
            hook::this_call<void>(gPatterns.C_StoneThrowingPile__ResetStonePile, this);
        }

        /**
         * Shows the first `count` stones in the pile and takes the rest out
         * of it, the top ones first (0x180D7F160). `Reset` passes INT_MAX.
         * Prepares no stone: `PrepareStoneForGrab` after it.
         */
        void SetActiveStoneCount(int count) {
            hook::this_call<void>(gPatterns.C_StoneThrowingPile__SetActiveStoneCount, this, count);
        }

        /**
         * Picks the next stone to be grabbed when none is (0x180D7F074): the
         * grab, and the game's prompt, need one. False when the pile shows
         * none.
         */
        bool PrepareStoneForGrab() {
            return hook::this_call<bool>(gPatterns.C_StoneThrowingPile__PrepareStoneForGrab, this);
        }

        /** The actor `C_StoneThrowing::StartWithPile` last handed the pile, whose facing a thrown stone takes. */
        EntityId GetUserEntityId() const {
            return m_userEntityId;
        }

      private:
        std::byte m_opaque0040[0x48];
        EntityId m_userEntityId; // 0x88, written by SetUserEntity (0x182EB23BC)
        std::byte m_opaque008C[0x54];

        static void AssertLayout() {
            static_assert(offsetof(C_StoneThrowingPile, m_userEntityId) == 0x88, "C_StoneThrowingPile::m_userEntityId offset mismatch");
        }
    };

    static_assert(sizeof(C_StoneThrowingPile) == 0xE0, "C_StoneThrowingPile size mismatch");
} // namespace SDK::wh::playermodule
