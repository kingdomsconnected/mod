#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * A renderer view: the list of render items one pass fills. Only its
     * reference count is modelled, which is all a holder of one touches:
     * `_smart_ptr<IRenderView>`'s release (0x1804A64A8, shared by every
     * `_smart_ptr` of a `CMultiThreadRefCount`) decrements +8 and, at zero,
     * calls slot 1.
     */
    struct IRenderView {
        virtual void unk_fn0000() = 0;
        virtual void DeleteThis() = 0; // slot 1, what the last release calls

        /** `_smart_ptr<IRenderView>::~_smart_ptr`, as the release helper runs it. */
        void Release() {
            if (m_refCount.fetch_sub(1) == 1) {
                DeleteThis();
            }
        }

      protected:
        ~IRenderView() = default;

      private:
        std::atomic<std::int32_t> m_refCount; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(IRenderView, m_refCount) == 0x08, "IRenderView::m_refCount offset mismatch");
        }
    };
} // namespace SDK
