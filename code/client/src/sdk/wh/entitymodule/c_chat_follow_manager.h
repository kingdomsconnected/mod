#pragma once

#include "../framework/i_source_monitor_listener.h"
#include "../game/i_entity_side_effect_callback.h"
#include "i_chat_follow_manager.h"

#include "../../patterns.h"
#include "../../types.h"
#include <utils/hooking/hooking.h>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player chat/follow coordination implementation.
     *
     * RTTI proves I_ChatFollowManager at +0x00,
     * game::I_EntitySideEffectCallback at +0x08, and
     * framework::I_SourceMonitorListener at +0x10. Its three tables at
     * 0x183A80ED0, 0x183A80EB8, and 0x183A80EA8 add no operations beyond
     * those contracts. Construction stores C_Player at +0x18, and allocation
     * plus scalar destruction prove the exact 0x78-byte layout.
     */
    class C_ChatFollowManager : public I_ChatFollowManager,
                                public SDK::wh::game::I_EntitySideEffectCallback,
                                public SDK::wh::framework::I_SourceMonitorListener {
      public:
        ~C_ChatFollowManager() override = default;

        /** Player, soul, target-state and mount checks; no distance. Asks `ClassifyFollowTarget`. */
        bool IsFollowTargetEligible(EntityId target) {
            return hook::this_call<bool>(gPatterns.C_ChatFollowManager__IsFollowTargetEligible, this, target);
        }

        /** The start distance for this mount pairing; conversation partners pass regardless. */
        bool MeetsFollowDistanceRequirement(EntityId target) {
            return hook::this_call<bool>(gPatterns.C_ChatFollowManager__MeetsFollowDistanceRequirement, this, target);
        }

        /** Rechecks eligibility and queues `C_ActorActionFollow`; reports nothing back. */
        void StartFollowing(EntityId target) {
            hook::this_call<void>(gPatterns.C_ChatFollowManager__StartFollowing, this, target);
        }

        C_Player *GetPlayer() const {
            return m_pPlayer;
        }

      private:
        C_Player *m_pPlayer; // 0x18
        // Factory 0x180BC7050 zeroes 0x20-0x40.
        std::uint64_t m_value0020;    // 0x20
        std::uint64_t m_value0028;    // 0x28
        std::uint64_t m_value0030;    // 0x30
        std::uint64_t m_value0038;    // 0x38
        std::uint64_t m_value0040;    // 0x40
        std::int32_t m_value0048;     // 0x48, factory sets -1
        std::byte m_padding004C[0x4];
        std::byte m_opaque0050[0x10]; // 0x50, not written by the factory
        bool m_flag0060;              // 0x60, factory clears it
        std::byte m_padding0061[0x7];
        bool m_flag0068;              // 0x68, factory clears it
        std::byte m_padding0069[0x7];
        std::int64_t m_time0070;      // 0x70, factory sets -100000, the engine's "never" timestamp

        static void AssertLayout() {
            static_assert(offsetof(C_ChatFollowManager, m_pPlayer) == 0x18, "C_ChatFollowManager::m_pPlayer offset mismatch");
            static_assert(offsetof(C_ChatFollowManager, m_value0048) == 0x48, "C_ChatFollowManager::m_value0048 offset mismatch");
            static_assert(offsetof(C_ChatFollowManager, m_flag0060) == 0x60, "C_ChatFollowManager::m_flag0060 offset mismatch");
            static_assert(offsetof(C_ChatFollowManager, m_time0070) == 0x70, "C_ChatFollowManager::m_time0070 offset mismatch");
        }
    };

    static_assert(sizeof(C_ChatFollowManager) == 0x78,
                  "C_ChatFollowManager size mismatch");
} // namespace SDK::wh::entitymodule
