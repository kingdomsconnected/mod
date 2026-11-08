#pragma once

#include "c_human.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * ABI overlay for C_NPCActor's inventory-driven accumulator. Its precise
     * gameplay meaning remains stripped.
     */
    struct NPCInventoryAccumulatorLayout {
        float m_value;        // 0x00
        float m_pendingDelta; // 0x04, summed non-excluded item stack amounts
        float m_decayRate;    // 0x08, elapsed-game-time decay coefficient
        float m_threshold;    // 0x0C, compared against the decayed value
        std::uint64_t m_lastChangeTime; // 0x10

        float GetValue() const { return m_value; }
        float GetPendingDelta() const { return m_pendingDelta; }
        float GetDecayRate() const { return m_decayRate; }
        float GetThreshold() const { return m_threshold; }
        std::uint64_t GetLastChangeTime() const { return m_lastChangeTime; }
    };

    static_assert(sizeof(NPCInventoryAccumulatorLayout) == 0x18,
                  "NPCInventoryAccumulatorLayout size mismatch");

    /**
     * Concrete non-player humanoid actor.
     *
     * C_NPCActor's primary table at 0x183A3BC90 contains exactly the 283
     * entries inherited from C_Human and adds no primary virtual methods.
     * Its factory allocates exactly 0xA78 bytes. The runtime actor-class name
     * returned through IActor is "C_NPC". Inventory callbacks operate on the
     * 0x18-byte derived tail at +0xA60 as an accumulated value, pending delta,
     * elapsed-game-time decay rate, threshold, and last-change timestamp.
     */
    class C_NPCActor : public C_Human {
      public:
        static constexpr const char *ActorClassName = "C_NPC";
        static constexpr ActorClass ActorClassId = ActorClass_NPC;

        ~C_NPCActor() override = default;

        NPCInventoryAccumulatorLayout *GetInventoryAccumulator() {
            return &m_inventoryAccumulator;
        }
        const NPCInventoryAccumulatorLayout *GetInventoryAccumulator() const {
            return &m_inventoryAccumulator;
        }

      private:
        NPCInventoryAccumulatorLayout m_inventoryAccumulator; // 0xA60
    };

    static_assert(sizeof(C_NPCActor) == 0xA78, "C_NPCActor size mismatch");
} // namespace SDK::wh::entitymodule
