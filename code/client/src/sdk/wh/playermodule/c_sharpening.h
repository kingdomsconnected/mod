#pragma once

#include "../../types.h"
#include "c_minigame.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The grindstone minigame. `C_PlayerManager::CreateMinigame` (0x1809F0234)
     * allocates 0x258 bytes for it and runs the constructor at 0x182EA6198,
     * which delegates to `C_Minigame` and adds a further interface at +0x68
     * (table 0x183F74D78, the receiver of `SetWheelSpeed`). Its primary slot 0
     * answers kind 1.
     *
     * The proven members:
     *
     * - +0x8C and +0x90, the blade's position along the wheel and its angle
     *   to it: `UpdateSwordMotion` (0x182EA7F8C) clamps them to [-0.5, 0.5]
     *   and [0, 1], and `RefreshSwordMotionParameters` hands them to
     *   `SetSwordPositionMotionParameter` (0x182EB0AA8, the actor's motion
     *   parameter 10) and `SetSwordRotationMotionParameter` (0x182EB0B30,
     *   parameter 11 on the actor and the wheel);
     * - +0xAC, the wheel's speed, which `UpdateWheelSpeed` decays and each
     *   step (`DoStepOnGrindstone`, 0x182EA9088) raises, clamped at 1;
     * - +0xB8, the grindstone's `fSpeedMultiplier` and +0xE0 its entity,
     *   both stored by `SetGrindstone` (0x182EB0204);
     * - +0xF0, the blade's animation suffix, which `ConfigureSharpenedItem`
     *   (0x182EB0744) stores from `GetSharpeningAnimationSuffix`;
     * - +0x18C, the minigame's state: 1 approaching, 2 walking in, 3 and 4 at
     *   the wheel, 5 walking out, 6 done (`OnEntryAnimationFinished`,
     *   `OnSharpeningActionInitialized`, `OnExitAnimationFinished`).
     */
    class C_Sharpening: public SDK::wh::playermodule::C_Minigame {
      public:
        float GetSwordPosition() const {
            return m_swordPosition;
        }
        float GetSwordRotation() const {
            return m_swordRotation;
        }
        float GetWheelSpeed() const {
            return m_wheelSpeed;
        }
        float GetSpeedMultiplier() const {
            return m_speedMultiplier;
        }
        SDK::EntityId GetGrindstoneEntityId() const {
            return m_grindstoneEntityId;
        }
        const char *GetBladeSuffix() const {
            return m_bladeSuffix.m_pData != nullptr ? m_bladeSuffix.m_pData : "";
        }
        std::uint32_t GetState() const {
            return m_state;
        }

      private:
        std::byte m_opaque0068[0x24];
        float m_swordPosition; // 0x8C
        float m_swordRotation; // 0x90
        std::byte m_opaque0094[0x18];
        float m_wheelSpeed; // 0xAC
        std::byte m_opaque00B0[0x8];
        float m_speedMultiplier; // 0xB8
        std::byte m_opaque00BC[0x24];
        SDK::EntityId m_grindstoneEntityId; // 0xE0
        std::byte m_opaque00E4[0xC];
        SDK::CryStringT<char> m_bladeSuffix; // 0xF0
        std::byte m_opaque00F8[0x94];
        std::uint32_t m_state; // 0x18C
        std::byte m_opaque0190[0xC8];

        static void AssertLayout() {
            static_assert(offsetof(C_Sharpening, m_swordPosition) == 0x8C, "C_Sharpening::m_swordPosition offset mismatch");
            static_assert(offsetof(C_Sharpening, m_swordRotation) == 0x90, "C_Sharpening::m_swordRotation offset mismatch");
            static_assert(offsetof(C_Sharpening, m_wheelSpeed) == 0xAC, "C_Sharpening::m_wheelSpeed offset mismatch");
            static_assert(offsetof(C_Sharpening, m_speedMultiplier) == 0xB8, "C_Sharpening::m_speedMultiplier offset mismatch");
            static_assert(offsetof(C_Sharpening, m_grindstoneEntityId) == 0xE0, "C_Sharpening::m_grindstoneEntityId offset mismatch");
            static_assert(offsetof(C_Sharpening, m_bladeSuffix) == 0xF0, "C_Sharpening::m_bladeSuffix offset mismatch");
            static_assert(offsetof(C_Sharpening, m_state) == 0x18C, "C_Sharpening::m_state offset mismatch");
        }
    };

    static_assert(sizeof(C_Sharpening) == 0x258, "C_Sharpening size mismatch");
} // namespace SDK::wh::playermodule
