#pragma once

#include "i_action.h"

#include "../../patterns.h"
#include "../shared/c_signal.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    /** Secondary interface present at +0x10 in every proven C_Action. */
    class I_ActionImpl {
      public:
        virtual ~I_ActionImpl() = default;
    };

    static_assert(sizeof(I_ActionImpl) == 0x08, "I_ActionImpl size mismatch");

    /** Shared native action implementation with its proven state member. */
    template <typename TInterface>
    class C_Action
        : public TInterface
        , public I_ActionImpl {
      public:
        std::uint32_t GetState() const {
            return m_state;
        }

        /**
         * Slot 6 (0x181A7D930): true once the action exited with reason 1,
         * which `C_ActionActor::ExitReplacedAction` (0x18068008C) passes for
         * a running action another one replaced. Written by `OnExit`
         * (0x1806801C0) ahead of the action's own `Exit`.
         */
        bool WasCancelled() const {
            return m_cancelled;
        }

        /**
         * Whether an action waits to follow this one: what slot 13
         * (0x180AB965C) stores and what `I_ActionImpl` slot 6 (0x180D4BD4C)
         * hands back. An attack keeps a combo's next strike here
         * (`C_CombatActorActionAttack`'s helper-owner slot 5, 0x18164A5E0,
         * which stores one only while this is empty).
         */
        bool HasNextAction() const {
            return m_pNextAction != nullptr;
        }

      protected:
        void SetState(std::uint32_t state) {
            hook::this_call<void>(gPatterns.C_Action__SetState, this, state);
        }

      private:
        // Typed from C_Action<I_CombatActorActionPrivate>'s ctor 0x18090D31C and dtor 0x18090D288.
        SDK::wh::shared::C_Signal m_signal0018; // 0x18, the dispatch differs per instantiation
        std::uint16_t m_state0028;              // 0x28, ctor sets 0
        bool m_cancelled;                       // 0x2A
        std::byte m_padding002B[0x5];           // 0x2B
        void *m_pNextAction;                    // 0x30, ctor sets null; the dtor releases it through vtable slot 2
        std::uint32_t m_state;                  // 0x38, ctor sets -1
        std::int32_t m_state003C;               // 0x3C, ctor sets -1
        void *m_pOwner0040;                     // 0x40, the ctor's pointer argument
        std::uint32_t m_state0048;              // 0x48, ctor sets 0
        std::byte m_padding004C[0x4];           // 0x4C

        static void AssertLayout() {
            static_assert(offsetof(C_Action, m_signal0018) == 0x18, "C_Action::m_signal0018 offset mismatch");
            static_assert(offsetof(C_Action, m_pNextAction) == 0x30, "C_Action::m_pNextAction offset mismatch");
            static_assert(offsetof(C_Action, m_pOwner0040) == 0x40, "C_Action::m_pOwner0040 offset mismatch");
            static_assert(offsetof(C_Action, m_cancelled) == 0x2A, "C_Action::m_cancelled offset mismatch");
            static_assert(offsetof(C_Action, m_state) == 0x38, "C_Action::m_state offset mismatch");
        }
    };

    /** Native parameter-bearing action: parameters are composed at +0x50. */
    template <typename TParams, typename TAction>
    class C_ParamAction: public TAction {
      protected:
        TParams &GetParams() {
            return m_params;
        }
        const TParams &GetParams() const {
            return m_params;
        }

      private:
        TParams m_params; // 0x50

        static void AssertLayout() {
            static_assert(offsetof(C_ParamAction, m_params) == 0x50, "C_ParamAction::m_params offset mismatch");
        }
    };
} // namespace SDK::wh::framework
