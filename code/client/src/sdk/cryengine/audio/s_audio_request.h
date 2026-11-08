#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** `EATLDataScope`: 1 global, 2 level-specific, 3 all. */
    enum class EATLDataScope : std::int32_t {
        Global        = 1,
        LevelSpecific = 2,
        All           = 3,
    };

    /**
     * What `IAudioSystem::PushRequest` (slot 3, 0x180763660) takes. The request data is copied into
     * an internal request on the caller's thread before PushRequest returns
     * (`CAudioRequestInternal`, 0x1807637E4), so both may be temporaries.
     */
    struct SAudioRequest {
        // 2 high priority, 4 blocking (waits until the audio thread has processed it), 8 sync
        // callback, 0x40 thread-safe queue. The engine pushes its control parsing with 6.
        static constexpr std::uint32_t kHighPriorityBlocking = 6;

        std::uint32_t flags        = 0;       // 0x00
        std::uint32_t objectId     = 0;       // 0x04
        void *owner                = nullptr; // 0x08
        void *userData             = nullptr; // 0x10
        void *userDataOwner        = nullptr; // 0x18
        const void *data           = nullptr; // 0x20

        static void AssertLayout() {
            static_assert(offsetof(SAudioRequest, flags) == 0x00, "SAudioRequest::flags offset mismatch");
            static_assert(offsetof(SAudioRequest, objectId) == 0x04, "SAudioRequest::objectId offset mismatch");
            static_assert(offsetof(SAudioRequest, owner) == 0x08, "SAudioRequest::owner offset mismatch");
            static_assert(offsetof(SAudioRequest, data) == 0x20, "SAudioRequest::data offset mismatch");
        }
    };

    static_assert(sizeof(SAudioRequest) == 0x28, "SAudioRequest size mismatch");

    /**
     * `SAudioManagerRequestData<0x200>`, PARSE_CONTROLS_DATA (vtable 0x183B20910): parses every
     * `*.xml` directly in `folder` as ATL controls, at a scope. `CLevelSystem::LoadLevel` pushes one
     * for the level folder at scope 2 and `UnLoadLevel` clears scope 2, so controls parsed at that
     * scope live exactly as long as the level. A name already declared is dropped.
     */
    class SAudioParseControlsRequest {
      public:
        SAudioParseControlsRequest(const char *folder, EATLDataScope scope) {
            hook::this_call<void>(gPatterns.SAudioManagerRequestData512__Ctor, this, folder, scope);
        }

      private:
        void *m_pVTable;             // 0x00
        std::int32_t m_category;     // 0x08, 1 = audio manager
        std::uint32_t m_type;        // 0x10 (padded), 0x200
        std::byte m_opaque0014[0x4]; // 0x14
        const char *m_folder;        // 0x18
        EATLDataScope m_scope;       // 0x20

        static void AssertLayout() {
            static_assert(offsetof(SAudioParseControlsRequest, m_folder) == 0x18, "SAudioParseControlsRequest::m_folder offset mismatch");
            static_assert(offsetof(SAudioParseControlsRequest, m_scope) == 0x20, "SAudioParseControlsRequest::m_scope offset mismatch");
        }
    };

    static_assert(sizeof(SAudioParseControlsRequest) == 0x28, "SAudioParseControlsRequest size mismatch");
} // namespace SDK
