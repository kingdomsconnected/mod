#pragma once

#include "../../cryengine/action/i_mannequin_action.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../entitymodule/c_actor.h"
#include "c_sharpening.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The actions a sharpening player plays: `C_HumanSharpeningInAction`,
     * `C_HumanSharpeningAction` (the wheel loop) and
     * `C_HumanSharpeningOutAction`, each a `TAction<SAnimationContext>`
     * underneath (RTTI). Their shared constructor (0x182EA5DF0) builds the
     * `IAction` and stores the actor at +0x90, the minigame at +0x98 and the
     * blade's suffix at +0xA0, which every one of them sets as its fragment
     * tag when it initialises (`SetSharpeningFragmentWithMaterialTag`,
     * 0x182EB0148, reading the name at +0xA0). The walk-in and walk-out
     * allocate 0xA8 bytes and add no member; the loop is larger.
     *
     * A walk-in or walk-out carries its fragment from its constructor; the
     * loop's is -1 until it initialises (0x182EAE06C) and takes the one the
     * actor's fragment table holds for `SharpeningMinigame`.
     */
    class C_HumanSharpeningActionBase: public SDK::TAction<SDK::SAnimationContext> {
      public:
        /** Whether `action` is one of the three. */
        static bool Is(const SDK::IAction &action) {
            const std::uint64_t vtable = *reinterpret_cast<const std::uint64_t *>(&action);
            return vtable == gPatterns.C_HumanSharpeningInAction__VTable || vtable == gPatterns.C_HumanSharpeningAction__VTable || vtable == gPatterns.C_HumanSharpeningOutAction__VTable;
        }

        /** Whether `action` is the wheel loop, whose fragment is not chosen until it
         * initialises. */
        static bool IsLoop(const SDK::IAction &action) {
            return *reinterpret_cast<const std::uint64_t *>(&action) == gPatterns.C_HumanSharpeningAction__VTable;
        }

        const char *GetBladeSuffix() const {
            return m_bladeSuffix.m_pData != nullptr ? m_bladeSuffix.m_pData : "";
        }

      private:
        SDK::wh::entitymodule::C_Actor *m_actor;           // 0x90
        SDK::wh::playermodule::C_Sharpening *m_sharpening; // 0x98
        SDK::CryStringT<char> m_bladeSuffix;               // 0xA0

        static void AssertLayout() {
            static_assert(offsetof(C_HumanSharpeningActionBase, m_actor) == 0x90, "C_HumanSharpeningActionBase::m_actor offset mismatch");
            static_assert(offsetof(C_HumanSharpeningActionBase, m_sharpening) == 0x98, "C_HumanSharpeningActionBase::m_sharpening offset mismatch");
            static_assert(offsetof(C_HumanSharpeningActionBase, m_bladeSuffix) == 0xA0, "C_HumanSharpeningActionBase::m_bladeSuffix offset mismatch");
        }
    };

    static_assert(sizeof(C_HumanSharpeningActionBase) == 0xA8, "C_HumanSharpeningActionBase size mismatch");
} // namespace SDK::wh::playermodule
