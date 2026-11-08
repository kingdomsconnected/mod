#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "../../patterns.h"
#include "../entitymodule/c_actor.h"
#include "e_minigame_type.h"
#include "i_minigame.h"
#include "i_minigame_camera.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * RTTI fixes the three bases at 0, 8 and 0x10. Constructor 0x1809F0F30
     * and derived construction fix the data prefix through 0x68. Native
     * station binding writes +0x28; Update reads the active byte at +0x2C.
     */
    class C_Minigame
        : public SDK::wh::playermodule::I_Minigame
        , public SDK::wh::playermodule::I_MinigameCamera
        , public SDK::IActionListener {
      public:
        static C_Minigame *Of(SDK::wh::entitymodule::C_Actor *actor, E_MinigameType::Type type) {
            return hook::this_call<C_Minigame *>(SDK::gPatterns.C_Actor__GetMinigame, actor, type, false, false);
        }
        SDK::wh::entitymodule::C_Actor *GetActor() const {
            return m_actor;
        }
        SDK::EntityId GetStationEntityId() const {
            return m_interactionEntityId;
        }
        // Shared interaction target: a station for crafting, a victim for
        // pickpocketing. Both accessors refer to the proven base member.
        SDK::EntityId GetInteractionEntityId() const {
            return m_interactionEntityId;
        }
        bool IsActive() const {
            return m_active;
        }

      private:
        SDK::wh::entitymodule::C_Actor *m_actor; // 0x18
        void *m_actorObject0020; // 0x20; ctor 0x1809F0F30 stores the actor's vtable +0x6E8 result.
        SDK::EntityId m_interactionEntityId; // 0x28
        bool m_active;                       // 0x2C
        std::byte m_padding002D[0x3];
        std::uint64_t m_handle0030;           // 0x30; ctor seeds sub_1804FD80C() + 12.
        void *m_ptr0038;                      // 0x38; ctor 0.
        std::uint64_t m_values0040[2];        // 0x40; ctor fills both with qword_185322AA0.
        bool m_flag0050;                      // 0x50; ctor 0.
        std::byte m_padding0051[0x7];
        void *m_ptr0058;                      // 0x58; ctor 0.
        std::uint16_t m_value0060;            // 0x60; ctor 0.
        std::byte m_padding0062[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_Minigame, m_actor) == 0x18);
            static_assert(offsetof(C_Minigame, m_interactionEntityId) == 0x28);
            static_assert(offsetof(C_Minigame, m_active) == 0x2C);
            static_assert(offsetof(C_Minigame, m_handle0030) == 0x30);
            static_assert(offsetof(C_Minigame, m_values0040) == 0x40);
            static_assert(offsetof(C_Minigame, m_ptr0058) == 0x58);
            static_assert(offsetof(C_Minigame, m_value0060) == 0x60);
        }
    };

    static_assert(sizeof(C_Minigame) == 0x68);
} // namespace SDK::wh::playermodule
