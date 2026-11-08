#pragma once

#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * An actor's hit and death reaction player, owned at `C_Actor+0x240`.
     *
     * The actor constructor builds it through the 0x290-byte factory at
     * 0x18137E714, whose constructor (0x1803EFEB8) installs this vtable.
     */
    class C_Actor;

    class C_HitDeathReactions {
      public:
        virtual ~C_HitDeathReactions() = default;

        /**
         * Whether a hit reaction is running on the body: `PlayReaction`
         * (0x180E77DF4) stores the reaction's row when it queues one, and
         * the action's end callback (0x1806184B8) clears it. While it is
         * set, `ProcessActorHitRecord` (0x180B24600, the test at
         * 0x180B24725) does not turn a kill or a fall that has no
         * reaction of its own into a ragdoll: the body is only given its
         * dead action.
         */
        bool IsReacting() const {
            return m_pReactionRow != nullptr;
        }

      private:
        // Ctor 0x1803EFEB8, finished by 0x180904910.
        SDK::wh::shared::C_Signal m_signal0008; // 0x08, dispatch unk_1856682F0
        SDK::wh::shared::C_Signal m_signal0018; // 0x18, dispatch unk_1856682F0
        C_Actor *m_pActor;                      // 0x28, the owning actor (the ctor's argument)
        const void *m_pReactionRow;             // 0x30, the running reaction's row; null when none
        std::uint32_t m_reactionActionId;       // 0x38, the running reaction's action id (I_Action slot 17)
        std::byte m_padding003C[0x4];
        std::int64_t m_time0040;                // 0x40, -100000 (the engine's "never")
        std::byte m_opaque0048[0x8];            // 0x48, not written
        SDK::wh::shared::C_Signal m_signal0050; // 0x50, dispatch unk_185668300
        SDK::wh::shared::C_Signal m_signal0060; // 0x60, dispatch unk_185667518
        std::byte m_opaque0070[0x8];            // 0x70, not written
        std::int64_t m_time0078;                // 0x78, -100000
        std::uint64_t m_value0080;              // 0x80, zeroed
        std::uint64_t m_value0088;              // 0x88, zeroed
        std::uint64_t m_value0090;              // 0x90, zeroed
        std::uint32_t m_value0098;              // 0x98, zeroed
        std::uint32_t m_values009C[3];          // 0x9C, zeroed
        std::byte m_inlineStorage00A8[0x160];   // 0xA8, inline buffer of the small vector at 0x210
        bool m_flag0208;                        // 0x208, zeroed
        std::byte m_padding0209[0x7];
        void *m_pInlineBegin0210;               // 0x210, this+0xA8
        void *m_pInlineBegin0218;               // 0x218, this+0xA8; 0x1810B1C98 reserves 2 through it
        std::uint64_t m_value0220;              // 0x220, zeroed
        std::uint64_t m_value0228;              // 0x228, zeroed
        std::uint64_t m_value0230;              // 0x230, zeroed
        bool m_flag0238;                        // 0x238, ctor sets 1
        std::byte m_padding0239[0x7];
        float m_value0240;                      // 0x240, 0x180904910 sets FLT_MAX (the rest of the 16 bytes zero)
        std::byte m_opaque0244[0x4C];           // 0x244, 0x180904910 copies 0x240-0x280 to 0x250

        static void AssertLayout() {
            static_assert(offsetof(C_HitDeathReactions, m_signal0008) == 0x08, "C_HitDeathReactions::m_signal0008 offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_pActor) == 0x28, "C_HitDeathReactions::m_pActor offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_pReactionRow) == 0x30, "C_HitDeathReactions::m_pReactionRow offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_reactionActionId) == 0x38, "C_HitDeathReactions::m_reactionActionId offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_signal0050) == 0x50, "C_HitDeathReactions::m_signal0050 offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_time0078) == 0x78, "C_HitDeathReactions::m_time0078 offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_flag0208) == 0x208, "C_HitDeathReactions::m_flag0208 offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_flag0238) == 0x238, "C_HitDeathReactions::m_flag0238 offset mismatch");
            static_assert(offsetof(C_HitDeathReactions, m_value0240) == 0x240, "C_HitDeathReactions::m_value0240 offset mismatch");
        }
    };

    static_assert(sizeof(C_HitDeathReactions) == 0x290, "C_HitDeathReactions size mismatch");
} // namespace SDK::wh::entitymodule
