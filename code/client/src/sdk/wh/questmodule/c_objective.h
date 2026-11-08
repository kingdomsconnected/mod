#pragma once

#include "c_log_base.h"
#include "e_quest_progress.h"

#include "../../types.h"
#include "../conceptmodule/c_auto_triggerable.h"
#include "../conceptmodule/c_port_ref.h"
#include "../conceptmodule/c_templated_node.h"
#include "../framework/c_localized_string.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * One line of a quest: the text the journal and the HUD tracker show, the
     * port that says whether it is done, and the marker it points at.
     *
     * Registered as `wh::questmodule::nodes::Objective` (0x180EDC9F8);
     * layout from the constructor at 0x180ACF804.
     */
    class C_Objective final: public conceptmodule::C_AutoTriggerable<conceptmodule::C_TemplatedNode> {
      public:
        /** Allocate, construct, AddRef, as the reflected factory does. The caller owns the reference that returns. */
        static C_Objective *Create();

        /** Points `m_progress` at a constant port carrying `progress`. Nothing may read the reference before this. */
        void SetProgress(E_QuestProgress progress);

        /** The text the journal row and the HUD tracker line show. */
        void SetLocalizedName(const char *text) {
            m_localizedName.SetStringName(text);
        }

        /** Four progress values need four logs; the cap keeps the sort's scratch list on the stack. */
        static constexpr std::size_t kMaxProgressLogs = 8;

        /**
         * Gives the objective its log list, keyed by progress name.
         *
         * This is what decides whether the objective is shown at all: the
         * journal looks a log up by the objective's progress spelled out
         * ("Active", "Done", "Failed") and hides an objective whose lookup
         * misses, because a miss answers a `None`-typed default (0x181F38AC0).
         * The lookup is a binary search, so the entries are sorted here rather
         * than trusted to arrive sorted.
         *
         * `logs` must outlive the objective; the list does not own them.
         */
        bool SetProgressLogs(C_LogBase *const *logs, const char *const *progressNames, std::size_t count);

        /** The objective half of the Flash row's `Id`, `"<quest>:<this>"`. Empty on a fresh objective, which gives every row of a quest one identity. */
        void SetIdentifier(const char *identifier) {
            m_identifier.Assign(identifier);
        }

        const SDK::CryStringT<char> &GetIdentifier() const {
            return m_identifier;
        }

        std::size_t GetLogCount() const {
            return m_logCount;
        }

        bool IsOptional() const {
            return m_isOptional;
        }

        void SetOptional(bool optional) {
            m_isOptional = optional;
        }

      private:
        SDK::CryStringT<char> m_unknown0048; // 0x48
        std::int16_t m_unknown0050;          // 0x50, constructed as -1
        std::int16_t m_unknown0052;          // 0x52, constructed as -1
        std::byte m_padding0054[0x4];
        conceptmodule::C_PortRef m_progress; // 0x58
        /** `C_TypedPortRef<S_Tracker>` in the game; S_Tracker's layout is not established, and the reference itself is a plain C_PortRef. */
        conceptmodule::C_PortRef m_tracker;           // 0x98
        SDK::CryStringT<char> m_identifier;           // 0xD8
        framework::C_LocalizedString m_localizedName; // 0xE0
        // A DynArray, not a std::vector like `C_Quest::m_objectives`: data,
        // count, capacity. The destructor frees the block only when capacity
        // is non-zero, so capacity is the flag that says it owns it.
        S_ObjectiveLog *m_logs;      // 0xF0
        std::uint64_t m_logCount;    // 0xF8
        std::uint64_t m_logCapacity; // 0x100
        bool m_isOptional;           // 0x108
        std::byte m_padding0109[0x3];
        std::int32_t m_unknown010C; // 0x10C, constructed as -1

        static void AssertLayout() {
            static_assert(offsetof(C_Objective, m_unknown0048) == 0x48, "C_Objective::m_unknown0048 offset mismatch");
            static_assert(offsetof(C_Objective, m_progress) == 0x58, "C_Objective::m_progress offset mismatch");
            static_assert(offsetof(C_Objective, m_tracker) == 0x98, "C_Objective::m_tracker offset mismatch");
            static_assert(offsetof(C_Objective, m_localizedName) == 0xE0, "C_Objective::m_localizedName offset mismatch");
            static_assert(offsetof(C_Objective, m_logs) == 0xF0, "C_Objective::m_logs offset mismatch");
            static_assert(offsetof(C_Objective, m_logCount) == 0xF8, "C_Objective::m_logCount offset mismatch");
            static_assert(offsetof(C_Objective, m_logCapacity) == 0x100, "C_Objective::m_logCapacity offset mismatch");
            static_assert(offsetof(C_Objective, m_isOptional) == 0x108, "C_Objective::m_isOptional offset mismatch");
        }
    };

    static_assert(sizeof(C_Objective) == 0x110, "C_Objective size mismatch");
} // namespace SDK::wh::questmodule
