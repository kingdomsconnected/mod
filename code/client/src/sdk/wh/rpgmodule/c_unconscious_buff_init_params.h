#pragma once

#include "../framework/wuid.h"
#include "i_buff_init_params.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * What a knockout hands the game's unconscious buff (`Cpp:Unconscious`).
     *
     * The game builds one for a blow's knockout (0x182D2277C, from
     * `AddUnconsciousnessBuffEffect`, 0x182D25AE4) with the depth the hit
     * calculation chose and cause 1. `C_UnconsciousSoulBuffInstance::Init`
     * (0x182BDAD9C) reads the depth (index 0), the cause (index 1) and the
     * world-time flag (index 2). With cause 0 -- what a null `initParams`
     * gives -- the buff never ends by itself; with any other the depth drains
     * by the soul's `UnconsciousnessFadeoutSpeed` (derived stat 56) per
     * second (`UpdateUnconsciousness`, 0x182BDD7B8), real seconds with the
     * flag clear, and the buff ends once it reaches zero. Cause 1 also adds
     * the stat modifier the blow's knockout carries (`CreateStatModifier`
     * with 137, 7 and 1.0).
     *
     * The answers below are the game's own (0x181F985A0, 0x182BDA964,
     * 0x181A82500); this one is built here, so its table is the mod's. The
     * buff reads it only during `Init`.
     */
    class C_UnconsciousBuffInitParams final: public I_BuffInitParams {
      public:
        /** As the game's factory builds one: the world-time flag clear. */
        C_UnconsciousBuffInitParams(float depth, std::uint32_t cause): m_depth(depth), m_cause(cause) {}

        float GetFloat(std::int32_t index) const override {
            return index == 0 ? m_depth : 0.0F;
        }

        std::uint32_t GetInt(std::int32_t index) const override {
            return index == 1 ? m_cause : index == 2 ? static_cast<std::uint32_t>(m_worldTime) : 0U;
        }

        framework::WUID GetWuid(std::int32_t) const override {
            return {framework::WUID::kInvalid};
        }

      private:
        float m_depth;            // 0x08
        std::uint32_t m_cause;    // 0x0C
        bool m_worldTime = false; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_UnconsciousBuffInitParams, m_depth) == 0x08, "C_UnconsciousBuffInitParams::m_depth offset mismatch");
            static_assert(offsetof(C_UnconsciousBuffInitParams, m_cause) == 0x0C, "C_UnconsciousBuffInitParams::m_cause offset mismatch");
            static_assert(offsetof(C_UnconsciousBuffInitParams, m_worldTime) == 0x10, "C_UnconsciousBuffInitParams::m_worldTime offset mismatch");
        }
    };

    static_assert(sizeof(C_UnconsciousBuffInitParams) == 0x18, "C_UnconsciousBuffInitParams size mismatch");
} // namespace SDK::wh::rpgmodule
