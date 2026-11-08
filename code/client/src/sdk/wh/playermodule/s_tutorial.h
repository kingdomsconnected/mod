#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    /** Opaque scalar storage used by reflected time-span properties. */
    struct S_TimeSpan {
        std::uint64_t m_value;
    };

    static_assert(sizeof(S_TimeSpan) == 0x8, "S_TimeSpan size mismatch");
} // namespace SDK::wh::framework

namespace SDK::wh::game::E_GameMode {
    enum Type : std::uint32_t {};
} // namespace SDK::wh::game::E_GameMode

namespace SDK::wh::playermodule {
    /** Reflected base record shared by popup and overlay tutorials. */
    struct S_Tutorial {
        virtual ~S_Tutorial() = default;
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;

        CryStringT<char> m_name;                 // 0x08, RTTR: Name
        bool m_remindAfterLongTime;              // 0x10, RTTR: RemindAfterLongTime
        bool m_repeatable;                       // 0x11, RTTR: Repeatable
        std::byte m_padding0012[0x6];
        framework::S_TimeSpan m_repeatCooldown;  // 0x18, RTTR: RepeatCooldown
        game::E_GameMode::Type m_gameModeFilter; // 0x20, RTTR: GameModeFilter
        std::byte m_padding0024[0x4];
    };

    static_assert(sizeof(S_Tutorial) == 0x28, "S_Tutorial size mismatch");
} // namespace SDK::wh::playermodule
