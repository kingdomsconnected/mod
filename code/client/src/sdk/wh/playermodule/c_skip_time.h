#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Which of the game's skip-time definitions C_SkipTime::BeginSkipTime is
     * opening. The value indexes the global definition registry and picks the
     * action map the screen pushes, so it is also the only thing that tells
     * the three ways into the screen apart once it is up.
     */
    enum class E_SkipTimeMode : std::uint32_t {
        /**
         * The wait dial, and the T key. Its only producer is the
         * `open_skiptime` action C_UISkiptime registers, so cutting this mode
         * cuts the whole feature and nothing else.
         */
        Wait = 1,

        /** Sleeping in a bed or bed roll, which carries the rest to a time. */
        Sleep = 2,

        /** A timed activity -- a workbench, a campfire, a repair. */
        Activity = 4,

        /**
         * The automatic skip a local knockout opens until unconsciousness
         * ends (0x182AC2F54 / 0x181F26E10 -> BeginAutomaticSkipTime
         * 0x182ECFC6C, deadline INT64_MAX).
         */
        Unconscious = 6,
    };

    /**
     * The game's one skip-time controller (magic static at 0x1854BFF50,
     * `GetInstance` 0x180471F74). `StartTimeAcceleration` (0x180F48DF8)
     * speeds the calendar up and sets a deadline of world time plus the
     * chosen hours; `Update` (0x180534220) stops it once the calendar's world
     * time reaches the deadline; `StopTimeAcceleration` (0x180F49024) puts the
     * calendar's ratio back and hands the end to the RPG module.
     */
    class C_SkipTime {
      public:
        /** The dial is up, the hours not yet confirmed. */
        static constexpr std::uint32_t kOpen = 1;
        /** The calendar runs fast towards the deadline. */
        static constexpr std::uint32_t kAccelerating = 2;

        /** 0 idle, then kOpen, kAccelerating, 4 while the screen comes back after the stop. */
        std::uint32_t GetState() const {
            return m_state;
        }

        /** What the screen was opened for. */
        E_SkipTimeMode GetMode() const {
            return m_mode;
        }

      private:
        std::byte m_opaque0000[0x48];
        std::uint32_t m_state; // 0x48
        E_SkipTimeMode m_mode; // 0x4C

        static void AssertLayout() {
            static_assert(offsetof(C_SkipTime, m_state) == 0x48, "C_SkipTime::m_state offset mismatch");
            static_assert(offsetof(C_SkipTime, m_mode) == 0x4C, "C_SkipTime::m_mode offset mismatch");
        }
    };
} // namespace SDK::wh::playermodule
