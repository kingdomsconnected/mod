#pragma once

#include "../../cryengine/action/i_actor.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::environmentmodule {
    /**
     * Rain game-object extension owned by the environment module.
     *
     * RTTI preserves the hierarchy through
     * CGameObjectExtensionHelper<C_Rain, IGameObjectExtension, 64>. The table
     * at 0x18471F090 implements exactly IGameObjectExtension's 29 entries and
     * adds no operations. Creator allocation and sized deletion prove the
     * complete 0x180-byte layout.
     */
    class C_Rain: public SDK::IGameObjectExtension {
      public:
        ~C_Rain() override = default;

        SDK::SRainParams *GetRainParameters() {
            return &m_parameters;
        }
        const SDK::SRainParams *GetRainParameters() const {
            return &m_parameters;
        }

        std::size_t GetOwnedEffectCount() const {
            return m_ppOwnedEffectsBegin != nullptr ? static_cast<std::size_t>(m_ppOwnedEffectsEnd - m_ppOwnedEffectsBegin) : 0;
        }

      private:
        SDK::SRainParams m_parameters; // 0x40
        std::uint8_t m_state0160;
        std::byte m_padding0161[0x7];
        void **m_ppOwnedEffectsBegin;    // 0x168
        void **m_ppOwnedEffectsEnd;      // 0x170
        void **m_ppOwnedEffectsCapacity; // 0x178
    };

    static_assert(sizeof(C_Rain) == 0x180, "C_Rain size mismatch");
} // namespace SDK::wh::environmentmodule
