#pragma once

#include "../framework/wuid.h"
#include "c_event.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * The effect a successful pickpocket applies: it moves the taken items
     * into the thief's inventory and stamps them stolen. 0xD0 bytes, a direct
     * `C_Effect` subclass by its RTTI.
     *
     * Built by `CreateStealItemsEffect` (0x182CD4A6C) from
     * `C_RPGMinigames::ApplyPickpocketingFinished` (0x182CFA134) with the
     * thief's soul WUID, the victim's and the taken span; the constructor
     * (0x182CA7FF4) stores them at +0x10, +0x18 and into a small-buffer vector
     * whose begin/end sit at +0xB8/+0xC0 -- the pair `Apply` (0x182CA9AFC)
     * walks, resolving each WUID in the item registry and skipping the ones it
     * no longer finds.
     */
    class C_StealItemsEffect: public C_Effect {
      public:
        /** The soul the items go to. */
        framework::WUID GetReceiverSoulWuid() const {
            return m_receiverSoulWuid;
        }

        /** The soul they are taken from. */
        framework::WUID GetVictimSoulWuid() const {
            return m_victimSoulWuid;
        }

      private:
        framework::WUID m_receiverSoulWuid;   // 0x10
        framework::WUID m_victimSoulWuid;     // 0x18
        // Constructor 0x182CA7FF4: a small-buffer vector of item WUIDs.
        framework::WUID m_inlineItems[16];    // 0x20; inline storage
        bool m_inlineInUse;                   // 0xA0; ctor 0
        std::byte m_padding00A1[0x7];
        framework::WUID *m_pArenaCursor;      // 0xA8; ctor points it at the inline storage
        framework::WUID *m_pArena;            // 0xB0; ctor points it at the inline storage; the vector's allocator
        const framework::WUID *m_pItemsBegin; // 0xB8
        const framework::WUID *m_pItemsEnd;   // 0xC0
        const framework::WUID *m_pItemsCapacity; // 0xC8

        static void AssertLayout() {
            static_assert(offsetof(C_StealItemsEffect, m_receiverSoulWuid) == 0x10, "C_StealItemsEffect::m_receiverSoulWuid offset mismatch");
            static_assert(offsetof(C_StealItemsEffect, m_victimSoulWuid) == 0x18, "C_StealItemsEffect::m_victimSoulWuid offset mismatch");
            static_assert(offsetof(C_StealItemsEffect, m_pItemsBegin) == 0xB8, "C_StealItemsEffect::m_pItemsBegin offset mismatch");
            static_assert(offsetof(C_StealItemsEffect, m_pItemsEnd) == 0xC0, "C_StealItemsEffect::m_pItemsEnd offset mismatch");
            static_assert(offsetof(C_StealItemsEffect, m_inlineInUse) == 0xA0, "C_StealItemsEffect::m_inlineInUse offset mismatch");
            static_assert(offsetof(C_StealItemsEffect, m_pArena) == 0xB0, "C_StealItemsEffect::m_pArena offset mismatch");
        }
    };

    static_assert(sizeof(C_StealItemsEffect) == 0xD0, "C_StealItemsEffect size mismatch");
} // namespace SDK::wh::rpgmodule
