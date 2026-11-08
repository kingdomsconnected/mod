#pragma once

#include "../patterns.h"
#include "../types.h"

#include "c_readiness_service.h"

#include <cstddef>
#include <cstdint>

#include <utils/hooking/hooking.h>

namespace SDK::wh {
    /**
     * The game's stock readiness task: a named counter in one category,
     * ready while the counter is zero. Its owners store the counter directly
     * (the FMOD bank load writes 1 while a bank streams), and the engine's
     * streaming systems embed one each ("TextureStreaming", category 1;
     * "EntityStreaming", 0x800; "HLodStreaming", 0x200).
     *
     * Built and torn down through the game's own constructor and destructor,
     * in place: the constructor registers with the readiness service it reads
     * from ISystem slot 99, the destructor unregisters and releases the name.
     * The service never frees a task, so a mod-owned instance is safe.
     *
     * Vtable 0x183A982B8 (wh::I_ReadinessTask): slot 2 IsReady(mask) reads
     * `m_pending == 0`, slot 3 GetCategory returns `m_category`, slot 4
     * GetName returns `&m_name`, slot 5 is the deleting destructor.
     */
    class C_ReadinessProgress final {
      public:
        C_ReadinessProgress(const CryStringT<char> &name, std::uint32_t category) {
            hook::this_call<void>(gPatterns.C_ReadinessProgress__Constructor, this, &name, category);
        }

        ~C_ReadinessProgress() {
            hook::this_call<void>(gPatterns.C_ReadinessProgress__Destructor, this);
        }

        C_ReadinessProgress(const C_ReadinessProgress &)            = delete;
        C_ReadinessProgress &operator=(const C_ReadinessProgress &) = delete;

        /** What the observers wait on: zero is ready. Main thread only, like the service's own reads. */
        void SetPending(std::int32_t pending) {
            m_pending = pending;
        }

        bool IsReady() const {
            return m_pending == 0;
        }

        std::uint32_t GetCategory() const {
            return m_category;
        }

      private:
        void *m_pVtable;                // 0x00, I_ReadinessTask
        C_ReadinessService *m_pService; // 0x08
        CryStringT<char> m_name;        // 0x10
        std::uint32_t m_category;       // 0x18
        std::int32_t m_pending;         // 0x1C

        static void AssertLayout() {
            static_assert(offsetof(C_ReadinessProgress, m_pService) == 0x08, "C_ReadinessProgress::m_pService offset mismatch");
            static_assert(offsetof(C_ReadinessProgress, m_name) == 0x10, "C_ReadinessProgress::m_name offset mismatch");
            static_assert(offsetof(C_ReadinessProgress, m_category) == 0x18, "C_ReadinessProgress::m_category offset mismatch");
            static_assert(offsetof(C_ReadinessProgress, m_pending) == 0x1C, "C_ReadinessProgress::m_pending offset mismatch");
        }
    };

    static_assert(sizeof(C_ReadinessProgress) == 0x20, "C_ReadinessProgress size mismatch");
} // namespace SDK::wh
