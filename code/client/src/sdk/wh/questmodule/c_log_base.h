#pragma once

#include "e_log_type.h"

#include "../../types.h"
#include "../framework/c_localized_string.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * One line of a quest's diary, and what an objective is sorted by.
     *
     * `Create` builds a `C_Log`, not the base: `C_LogBase` is abstract and the
     * journal calls both its pure slots while drawing a row, so a base-vtabled
     * log aborts the process. `C_Log` adds no members -- its factory at
     * 0x180F6F91C runs the base constructor and swaps the vtable, and that is
     * the whole derived class.
     *
     * Nothing here owns anything, so `Destroy` is a free.
     */
    class C_LogBase {
      public:
        static C_LogBase *Create();
        static void Destroy(C_LogBase *log);

        /** Only `Started` and `Updated` render: 0x181F5C560 drops an objective whose log reads `None`, and 0x181F5C5A0 builds a row for nothing else. */
        void SetLogType(E_LogType logType) {
            m_logType = logType;
        }

        E_LogType GetLogType() const {
            return m_logType;
        }

        std::int64_t GetOrder() const {
            return m_order;
        }

        /** The objective's sort key is the smallest order across its logs, read without an empty check (0x180DC653C). Zero sorts last. */
        void SetOrder(std::int64_t order) {
            m_order = order;
        }

      private:
        void *m_vtable;      // 0x00
        E_LogType m_logType; // 0x08, constructed as Completed
        std::byte m_padding000C[0x4];
        SDK::CryStringT<char> m_unknown0010; // 0x10
        framework::C_LocalizedString m_text; // 0x18
        std::int64_t m_order;                // 0x28

        static void AssertLayout() {
            static_assert(offsetof(C_LogBase, m_logType) == 0x08, "C_LogBase::m_logType offset mismatch");
            static_assert(offsetof(C_LogBase, m_unknown0010) == 0x10, "C_LogBase::m_unknown0010 offset mismatch");
            static_assert(offsetof(C_LogBase, m_text) == 0x18, "C_LogBase::m_text offset mismatch");
            static_assert(offsetof(C_LogBase, m_order) == 0x28, "C_LogBase::m_order offset mismatch");
        }
    };

    static_assert(sizeof(C_LogBase) == 0x30, "C_LogBase size mismatch");

    /** One log-list entry: the port name it is found by, and a `shared_ptr<C_LogBase>` left non-owning with a null control block. */
    struct S_ObjectiveLog {
        SDK::CryStringT<char> portName;
        C_LogBase *log;
        void *sharedControl;
    };

    static_assert(sizeof(S_ObjectiveLog) == 0x18, "S_ObjectiveLog size mismatch");
} // namespace SDK::wh::questmodule
