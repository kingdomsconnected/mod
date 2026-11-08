#pragma once

#include "../../types.h"
#include "c_perk.h"
#include "c_perk_learning_state.h"
#include "i_event_listener.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    // Declared, not included, for the reason `c_soul_state_storage.h` gives:
    // the soul embeds the storage that embeds this list.
    class C_Soul;

    /**
     * A soul's perks: the ones it owns, the ones it could learn next, and the
     * point banks that pay for them.
     *
     * A member of the soul state storage at +0x1F8 -- so `C_Soul+0x758` for
     * the active copy -- written by that storage's constructor at 0x18070F3E8,
     * which also zeroes the learning-state pointer and the flag byte. The
     * RTTI gives it exactly one base, `I_EventListener`, and its one slot
     * (`OnEvent1809DB228`) is where a level gained on an XP effect becomes
     * perk points.
     *
     * Every add goes through `AddPerk` and every ordinary removal through
     * `RemovePerk`; `ClearAllPerks` and `ClearActivePerks` bypass the latter.
     */
    class C_PerkList: public I_EventListener {
      public:
        // Declared, never defined: the list is embedded in the soul storage by
        // value, so it has to be concrete, and only the game's vtable ever
        // dispatches it.
        void OnEvent(C_Event *event) override; // slot 0, OnEvent1809DB228

        C_Soul *GetSoul() const {
            return m_pSoul;
        }

        /** Null on a soul that never learns perks. */
        C_PerkLearningState *GetLearningState() const {
            return m_pLearningState;
        }

        /** Owned perks; `C_Soul::HasPerk` walks exactly this vector. Borrowed for the current frame. */
        C_Perk *const *ActivePerksBegin() const {
            return m_activePerks;
        }

        C_Perk *const *ActivePerksEnd() const {
            return m_activePerksEnd;
        }

        /**
         * The add choke point, 0x18046B4A4. Refuses a perk already owned or
         * blocked (`CanAddPerk18046B5EC`), moves it from available to active,
         * applies it unless flag bit 1 is set, then refreshes what is
         * available. `sourcePerk` is recorded on a buff perk's buff.
         */
        bool AddPerk(const CryGUID &perk, C_Perk *sourcePerk) {
            return hook::this_call<bool>(gPatterns.C_PerkList__AddPerk, this, &perk, sourcePerk);
        }

        /** 0x180A2043C. False for a perk not owned. */
        bool RemovePerk(const CryGUID &perk) {
            return hook::this_call<bool>(gPatterns.C_PerkList__RemovePerk, this, &perk);
        }

        /**
         * The respec a respec buff runs, 0x1811217D8: removes the learnable
         * perks, clears what is available, rebuilds every bank from the
         * soul's current levels and refreshes.
         */
        bool Respec() {
            return hook::this_call<bool>(gPatterns.C_PerkList__Respec, this);
        }

        /** 0x1804688C0: re-derives which perks are learnable from levels and parents. */
        bool RefreshAvailablePerks() {
            return hook::this_call<bool>(gPatterns.C_PerkList__RefreshAvailablePerks, this);
        }

      private:
        C_Soul *m_pSoul;                       // 0x08
        void *m_explicitDefinitions;           // 0x10; explicit definition vector begin, element type unproven (ctor 0x18070F3E8 zeroes all three)
        void *m_explicitDefinitionsEnd;        // 0x18
        void *m_explicitDefinitionsCapacity;   // 0x20
        C_Perk **m_availablePerks;             // 0x28; available C_Perk * vector
        C_Perk **m_availablePerksEnd;          // 0x30
        C_Perk **m_availablePerksCapacity;     // 0x38
        C_Perk **m_activePerks;                // 0x40
        C_Perk **m_activePerksEnd;             // 0x48
        C_Perk **m_activePerksCapacity;        // 0x50
        C_PerkLearningState *m_pLearningState; // 0x58
        std::uint8_t m_flags;                  // 0x60: bit 0 suppresses refresh, bit 1 apply, bit 2 set while registered as an event listener (EnableLearning180C3F09C)
        std::byte m_padding0061[0x7];
        std::uint32_t m_value0068;             // 0x68; ctor 0x18070F3E8 writes 0.
        std::byte m_padding006C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_PerkList, m_pSoul) == 0x08, "C_PerkList::m_pSoul offset mismatch");
            static_assert(offsetof(C_PerkList, m_activePerks) == 0x40, "C_PerkList::m_activePerks offset mismatch");
            static_assert(offsetof(C_PerkList, m_pLearningState) == 0x58, "C_PerkList::m_pLearningState offset mismatch");
            static_assert(offsetof(C_PerkList, m_flags) == 0x60, "C_PerkList::m_flags offset mismatch");
            static_assert(offsetof(C_PerkList, m_explicitDefinitions) == 0x10, "C_PerkList::m_explicitDefinitions offset mismatch");
            static_assert(offsetof(C_PerkList, m_availablePerks) == 0x28, "C_PerkList::m_availablePerks offset mismatch");
            static_assert(offsetof(C_PerkList, m_value0068) == 0x68, "C_PerkList::m_value0068 offset mismatch");
        }
    };

    static_assert(sizeof(C_PerkList) == 0x70, "C_PerkList size mismatch");
} // namespace SDK::wh::rpgmodule
