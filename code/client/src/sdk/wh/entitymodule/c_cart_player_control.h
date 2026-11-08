#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The local player's side of riding in a cart, 32 bytes, owned by
     * C_Player at +0xD38 (stored by `C_Player::Init`, 0x180A4C1DC).
     *
     * Lua's `Cart.MountPlayer` reaches it straight through
     * `C_ScriptBind_Cart::MountPlayer` (0x182ABF19C), which is how a
     * `WHCartMountPoint` prompt seats the player. The prompt offers itself
     * only while `Cart.CanPlayerMount` answers, and its `OnMount` then calls
     * `Cart.MountPlayer` once (`WHCartMountPoint.lua`). Once seated it
     * registers the dismount key and its hint, swaps the `cart_action` action
     * map for `cart`, and holds the camera to the seat (`OnSeated`).
     */
    class C_CartPlayerControl {
      public:
        /**
         * The seat is taken (0x1828F8E0C): `SetCartActorState` calls it as a
         * stay leaves `Mounting`, and the owner's actor model fires it on its
         * event 7 (`OnActorModelEvent`, 0x1819EC130) -- for every C_Player,
         * remote copies included. Registers `cart_dismount`, turns the `cart`
         * map on unless the dismount is disabled, and holds the camera.
         */
        virtual void OnSeated() = 0; // slot 0

        /**
         * The climb down has begun (0x1828F8D1C): `SetCartActorState` calls it
         * on `Dismounting` or `FastDismounting`, and the listener on the
         * player's actions calls it again when the cart action ends. Undoes
         * `OnSeated`.
         */
        virtual void OnUnseated() = 0; // slot 1

        /**
         * The gate the mount prompt asks before it is offered (0x1828F1B50):
         * a cart behind the id, actor state 0 active on the player's model,
         * the cart's player-seat enable (or `wh_cart_ForceEnablePlayerSeat`),
         * the back seat's neighbours unreserved, `C_Cart::CanOccupyPosition`
         * for the seat `frontSeat` names, and the cart standing still.
         */
        bool CanPlayerMount(EntityId cartEntityId, bool frontSeat) {
            return hook::this_call<bool>(gPatterns.C_CartPlayerControl__CanPlayerMount, this, cartEntityId, frontSeat);
        }

        /**
         * Seats the player: the co-driver's bench when `frontSeat`, else the
         * right rail -- the two positions `C_Cart::CanOccupyPosition` lets a
         * player-controlled actor take. Walks there first and climbs in.
         *
         * Returns nothing: it queues a `C_ActorActionCart` and the value left
         * in AL on the accepted path is a destructor's. The queue inserts
         * synchronously, so whether it was accepted is whether the player now
         * holds a cart action.
         */
        void MountPlayer(EntityId cartEntityId, bool frontSeat) {
            hook::this_call<void>(gPatterns.C_CartPlayerControl__MountPlayer, this, cartEntityId, frontSeat);
        }

      private:
        void *m_pScriptBind;              // 0x08, the owned C_ScriptBind_Cart the ctor 0x180A4C650 builds; released through its scalar deleting destructor
        std::uint16_t m_value0010;        // 0x10, ctor zeroes it
        std::uint8_t m_value0012;         // 0x12, ctor zeroes it
        std::byte m_padding0013[0x1];
        std::uint32_t m_dismountDisabled; // 0x14, a counter: the dismount key is refused while non-zero
        void *m_pPlayer;                  // 0x18, the C_Player that owns it

      public:
        void *GetPlayer() const {
            return m_pPlayer;
        }

      private:

        static void AssertLayout() {
            static_assert(offsetof(C_CartPlayerControl, m_pScriptBind) == 0x08, "C_CartPlayerControl::m_pScriptBind offset mismatch");
            static_assert(offsetof(C_CartPlayerControl, m_value0010) == 0x10, "C_CartPlayerControl::m_value0010 offset mismatch");
            static_assert(offsetof(C_CartPlayerControl, m_dismountDisabled) == 0x14, "C_CartPlayerControl::m_dismountDisabled offset mismatch");
            static_assert(offsetof(C_CartPlayerControl, m_pPlayer) == 0x18, "C_CartPlayerControl::m_pPlayer offset mismatch");
        }
    };

    static_assert(sizeof(C_CartPlayerControl) == 0x20, "C_CartPlayerControl size mismatch");
} // namespace SDK::wh::entitymodule
