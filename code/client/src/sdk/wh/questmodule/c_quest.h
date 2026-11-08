#pragma once

#include "c_objective.h"
#include "e_quest_progress.h"
#include "e_quest_type.h"

#include "../../types.h"
#include "../conceptmodule/c_module_base.h"
#include "../conceptmodule/c_port_ref.h"
#include "../framework/c_localized_string.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * Pins a progress reference to a constant port carrying `progress`.
     *
     * The variant has to be enum-typed, not the text an authored literal
     * deserializes to: readers take it through `variant::to_uint32`, which
     * converts a typed enum and answers garbage for a string.
     */
    void BindProgressPort(conceptmodule::C_PortRef &reference, E_QuestProgress progress);

    /**
     * A journal entry and the sub-graph behind it. Registered as
     * `wh::questmodule::nodes::Quest` (0x1810540D4); layout from the
     * constructor at 0x180DC4B80.
     *
     * Everything the journal shows is read straight off this object, so
     * building one is the whole of putting a quest on screen. The node name,
     * not the display name, is the identity the marker refresh and
     * `C_UIFlashListQuest::Id` use.
     */
    class C_Quest final: public conceptmodule::C_ModuleBase {
      public:
        /** Allocate, zero, construct, AddRef -- and the AddRef is what enters it into the concept resource registry. */
        static C_Quest *Create();

        /** Points `m_progress` at a constant port. Until this is called the reference has no definition and the first read faults. */
        void SetProgress(E_QuestProgress progress);

        /** The game's own read of the port, the path `CollectActiveQuests` and the journal row take. */
        E_QuestProgress GetProgress() const;

        E_QuestType GetType() const {
            return m_type;
        }

        void SetType(E_QuestType type) {
            m_type = type;
        }

        /** The title the journal row and the quest-updated popup show. */
        void SetQuestName(const char *text) {
            m_questName.SetStringName(text);
        }

        /** The body of the journal's diary page. */
        void SetQuestDescription(const char *text) {
            m_questDescription.SetStringName(text);
        }

        bool IsImportant() const {
            return m_isImportant;
        }

        void SetImportant(bool important) {
            m_isImportant = important;
        }

        const SDK::StdVector<conceptmodule::S_WeakResourceId> &GetObjectives() const {
            return m_objectives;
        }

        /** Replaces the objective list. It holds weak registry handles and takes no reference, so the caller keeps the objectives alive. */
        void SetObjectives(C_Objective *const *objectives, std::size_t count);

      private:
        SDK::CryStringT<char> m_unknown00A0; // 0xA0
        std::int16_t m_unknown00A8;          // 0xA8, constructed as -1
        std::int16_t m_unknown00AA;          // 0xAA, constructed as -1
        std::byte m_padding00AC[0x4];
        /** `C_TypedPortRef<E_QuestProgress>` in the game; the reference itself is a C_PortRef. */
        conceptmodule::C_PortRef m_progress; // 0xB0
        E_QuestType m_type;                  // 0xF0
        std::byte m_padding00F4[0x4];
        framework::C_LocalizedString m_questName;        // 0xF8
        framework::C_LocalizedString m_questDescription; // 0x108
        std::int32_t m_difficulty;                       // 0x118, constructed as 1
        bool m_isImportant;                              // 0x11C
        std::byte m_padding011D[0x3];
        // Weak handles, not pointers: the game resolves each through the
        // concept registry (0x180DC6590) before it touches an objective.
        SDK::StdVector<conceptmodule::S_WeakResourceId> m_objectives; // 0x120
        SDK::StdVector<void *> m_players;                             // 0x138

        static void AssertLayout() {
            static_assert(offsetof(C_Quest, m_unknown00A0) == 0xA0, "C_Quest::m_unknown00A0 offset mismatch");
            static_assert(offsetof(C_Quest, m_progress) == 0xB0, "C_Quest::m_progress offset mismatch");
            static_assert(offsetof(C_Quest, m_type) == 0xF0, "C_Quest::m_type offset mismatch");
            static_assert(offsetof(C_Quest, m_questName) == 0xF8, "C_Quest::m_questName offset mismatch");
            static_assert(offsetof(C_Quest, m_questDescription) == 0x108, "C_Quest::m_questDescription offset mismatch");
            static_assert(offsetof(C_Quest, m_difficulty) == 0x118, "C_Quest::m_difficulty offset mismatch");
            static_assert(offsetof(C_Quest, m_isImportant) == 0x11C, "C_Quest::m_isImportant offset mismatch");
            static_assert(offsetof(C_Quest, m_objectives) == 0x120, "C_Quest::m_objectives offset mismatch");
            static_assert(offsetof(C_Quest, m_players) == 0x138, "C_Quest::m_players offset mismatch");
        }
    };

    static_assert(sizeof(C_Quest) == 0x150, "C_Quest size mismatch");
} // namespace SDK::wh::questmodule
