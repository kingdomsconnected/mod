#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "c_alchemy_action_base.h"

namespace SDK::wh::playermodule {
    /** Factory 0x182E0F240 allocates 0xC0; RTTI 0x183F60410 pins both bases. */
    class C_UseRetortAction
        : public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>>
        , public SDK::IActionListener {
      public:
        enum class Phase : std::uint32_t {
            Pouring,
            Idle,
            Pumping,
            Finishing
        };
        Phase GetPhase() const {
            return m_phase;
        }
        std::uint32_t GetCompletedPulses() const {
            return m_completedPulses;
        }
        void Pump() {
            hook::this_call<void>(SDK::gPatterns.C_UseRetortAction__Pump, this);
        }

      private:
        Phase m_phase; // 0x70, ctor 0x182E13450 / exit 0x182E1719C.
        std::uint32_t m_eventHashes[8];  // 0x74; lower-cased CRC32s of the Attach/Detach Flask, Retort and Pot and AnimateBellows events
        std::byte m_padding0094[0x4];
        void *m_ptrs0098[2];             // 0x98; ctor 0
        std::uint32_t m_completedPulses; // 0xA8; incremented only by pump exit.
        std::byte m_padding00AC[0x4];
        void *m_pActionHandlers;         // 0xB0; owned 0x10-byte std::map {head, size} of action callbacks
        void *m_ptr00B8;                 // 0xB8; ctor 0
        static void AssertLayout() {
            static_assert(offsetof(C_UseRetortAction, m_phase) == 0x70);
            static_assert(offsetof(C_UseRetortAction, m_completedPulses) == 0xA8);
            static_assert(offsetof(C_UseRetortAction, m_ptrs0098) == 0x98);
            static_assert(offsetof(C_UseRetortAction, m_pActionHandlers) == 0xB0);
        }
    };
    static_assert(sizeof(C_UseRetortAction) == 0xC0);
} // namespace SDK::wh::playermodule
