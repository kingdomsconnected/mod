#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * Warhorse's recursive exclusive lock: an SRWLOCK followed by the depth
     * (+0x08) and the owning thread id (+0x0C). Acquire (0x1803D902C) only
     * bumps the depth when the calling thread already owns it; Release
     * (0x18042DC54) unwinds the depth before it lets the SRWLOCK go.
     */
    class C_RecursiveSRWLock {
      public:
        void Acquire() {
            hook::call<void>(gPatterns.RecursiveSRWLock__Acquire, this);
        }

        void Release() {
            hook::call<void>(gPatterns.RecursiveSRWLock__Release, this);
        }

      private:
        void *m_srwLock;          // 0x00, SRWLOCK
        std::uint32_t m_depth;    // 0x08
        std::uint32_t m_threadId; // 0x0C

        static void AssertLayout() {
            static_assert(offsetof(C_RecursiveSRWLock, m_depth) == 0x08, "C_RecursiveSRWLock::m_depth offset mismatch");
            static_assert(offsetof(C_RecursiveSRWLock, m_threadId) == 0x0C, "C_RecursiveSRWLock::m_threadId offset mismatch");
        }
    };

    static_assert(sizeof(C_RecursiveSRWLock) == 0x10, "C_RecursiveSRWLock size mismatch");

    /** Holds a C_RecursiveSRWLock for one scope, as the engine's own guard does. */
    class C_ScopedRecursiveSRWLock final {
      public:
        explicit C_ScopedRecursiveSRWLock(C_RecursiveSRWLock *lock): _lock(lock) {
            if (_lock != nullptr) {
                _lock->Acquire();
            }
        }

        ~C_ScopedRecursiveSRWLock() {
            if (_lock != nullptr) {
                _lock->Release();
            }
        }

        C_ScopedRecursiveSRWLock(const C_ScopedRecursiveSRWLock &)            = delete;
        C_ScopedRecursiveSRWLock &operator=(const C_ScopedRecursiveSRWLock &) = delete;

      private:
        C_RecursiveSRWLock *_lock;
    };
} // namespace SDK::wh::shared
