#pragma once

#include "wuid.h"

#include <cstddef>

namespace SDK::wh::framework {
    /**
     * What `I_UIApse::Show` takes beside the view, as the `open_apse_*`
     * handlers build it on their stack (0x181F4D8F0 via 0x181003BC0). The
     * name is the mod's: the game's own type has no RTTI record.
     *
     * `Show` keeps a copy of both on the Apse when it opens, and destroys the
     * callable here itself, refused or not. Every key handler passes the
     * invalid WUID and an empty callable, which is what `Make` builds; what
     * either means when set is not established.
     */
    struct S_UIApseShowParams {
        WUID m_focus;

        /** Erased callable protocol used by the game's MSVC std::function. */
        struct CallbackImplementation {
            virtual void Opaque0() = 0;
            virtual void Opaque1() = 0;
            virtual void Opaque2() = 0;
            virtual void Opaque3() = 0;
            // std::_Func_class destructor at 0x1803E23C0 calls slot 4.
            virtual void DeleteThis(bool deallocate) = 0;
        };

        alignas(8) std::byte m_callbackStorage[0x38];
        CallbackImplementation *m_callback;

        /** Same destruction as Show's refusal path, always using the native allocator. */
        void ReleaseCallback() {
            if (m_callback != nullptr) {
                m_callback->DeleteThis(static_cast<void *>(m_callback) != static_cast<void *>(m_callbackStorage));
                m_callback = nullptr;
            }
        }

        static S_UIApseShowParams Make() {
            S_UIApseShowParams params {};
            params.m_focus.value = WUID::kInvalid;
            return params;
        }
    };

    static_assert(sizeof(S_UIApseShowParams) == 0x48, "S_UIApseShowParams size mismatch");
    static_assert(offsetof(S_UIApseShowParams, m_callbackStorage) == 0x8);
    static_assert(offsetof(S_UIApseShowParams, m_callback) == 0x40);
} // namespace SDK::wh::framework
