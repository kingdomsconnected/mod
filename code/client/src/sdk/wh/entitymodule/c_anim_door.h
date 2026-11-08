#pragma once

#include "c_lock_base.h"

#include "../../cryengine/action/c_game_object.h"
#include "../../cryengine/action/i_game_object.h"
#include "../../cryengine/entitysystem/i_entity.h"
#include "../../cryengine/entitysystem/i_entity_proxy.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Native extension behind the shipped Lua `AnimDoor` entity.
     *
     * Its constructor first builds C_LockBase, then initializes the opened,
     * closed, manipulation-start and release signals below. The factory
     * allocates exactly 0xC0 bytes. Door direction, active animation and lock
     * state do not live here: AnimDoor.lua owns `nDirection`, `inUse`,
     * `lastAnim` and `bLocked` on the entity script table.
     */
    class C_AnimDoor: public C_LockBase {
      public:
        ~C_AnimDoor() override = default;

        SignalLayout *GetOnOpenedSignal() {
            return &m_onOpened;
        }

        SignalLayout *GetOnClosedSignal() {
            return &m_onClosed;
        }

        SignalLayout *GetOnManipulationStartSignal() {
            return &m_onManipulationStart;
        }

        /** Broadcast synchronously by Release before the deleting destructor. */
        SignalLayout *GetOnReleasedSignal() {
            return &m_onReleased;
        }

      private:
        SignalLayout m_onOpened;            // 0x70
        SignalLayout m_onClosed;            // 0x80
        SignalLayout m_onManipulationStart; // 0x90
        SignalLayout m_onReleased;          // 0xA0
        std::uint32_t m_nonInteractive;     // 0xB0, ctor 0x181163C18 zeroes; animDoor:IsInteractive() (0x1804CFA9C) is false while non-zero
        bool m_materialInitialized;         // 0xB4
        std::byte m_padding00B5[0x3];       // 0xB5
        std::uint32_t m_state00B8;          // 0xB8, ctor zeroes
        std::byte m_padding00BC[0x4];       // 0xBC

        static void AssertLayout() {
            static_assert(offsetof(C_AnimDoor, m_onOpened) == 0x70, "C_AnimDoor::m_onOpened offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_onClosed) == 0x80, "C_AnimDoor::m_onClosed offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_onManipulationStart) == 0x90, "C_AnimDoor::m_onManipulationStart offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_onReleased) == 0xA0, "C_AnimDoor::m_onReleased offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_nonInteractive) == 0xB0, "C_AnimDoor::m_nonInteractive offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_materialInitialized) == 0xB4, "C_AnimDoor::m_materialInitialized offset mismatch");
            static_assert(offsetof(C_AnimDoor, m_state00B8) == 0xB8, "C_AnimDoor::m_state00B8 offset mismatch");
        }
    };

    static_assert(sizeof(C_AnimDoor) == 0xC0, "C_AnimDoor size mismatch");

    /**
     * The `AnimDoor` extension on `entity`'s game object, or null: what
     * `C_ScriptBindAnimDoor::FindAnimDoorExtension` looks up by the same name,
     * which `C_AnimDoor`'s vtable slot 29 returns (0x1816BF8A0).
     */
    inline C_AnimDoor *FindAnimDoorExtension(IEntity *entity) {
        return static_cast<C_AnimDoor *>(CGameObject::QueryExtension(entity, "AnimDoor"));
    }
} // namespace SDK::wh::entitymodule
