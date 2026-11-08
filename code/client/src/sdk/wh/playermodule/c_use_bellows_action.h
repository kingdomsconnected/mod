#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "c_alchemy_action_base.h"

namespace SDK::wh::playermodule {
    /** Factory 0x18161FB78 allocates 0x98; RTTI 0x183F60260 pins both bases. */
    class C_UseBellowsAction
        : public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>>
        , public SDK::IActionListener {
      public:
        enum class Phase : std::uint32_t {
            Picking,
            Idle,
            Pumping,
            Returning
        };
        Phase GetPhase() const {
            return m_phase;
        }
        void Pump() {
            hook::this_call<void>(SDK::gPatterns.C_UseBellowsAction__Pump, this);
        }
        void ReturnBellows() {
            hook::this_call<void>(SDK::gPatterns.C_UseBellowsAction__ReturnBellows, this);
        }

      private:
        std::uint32_t m_attachEventHash; // 0x70; lower-cased CRC32 of "Attach"
        std::uint32_t m_detachEventHash; // 0x74; lower-cased CRC32 of "Detach"
        Phase m_phase; // 0x78, ctor 0x18161FC24 / exit 0x1806C4B30.
        std::byte m_padding007C[0x4];
        void *m_pActionHandlers;         // 0x80; owned 0x10-byte std::map {head, size} of action callbacks
        void *m_ptr0088;                 // 0x88; ctor 0
        std::byte m_opaque0090[0x8];     // 0x90; not written by the constructor
        static void AssertLayout() {
            static_assert(offsetof(C_UseBellowsAction, m_phase) == 0x78);
            static_assert(offsetof(C_UseBellowsAction, m_attachEventHash) == 0x70);
            static_assert(offsetof(C_UseBellowsAction, m_pActionHandlers) == 0x80);
        }
    };
    static_assert(sizeof(C_UseBellowsAction) == 0x98);
} // namespace SDK::wh::playermodule
