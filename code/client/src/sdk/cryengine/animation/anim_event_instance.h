#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CAnimEventData;

    /** Event payload cached by ISkeletonAnim and delivered to game callbacks. */
    struct AnimEventInstance {
        float m_time;    // 0x00
        float m_endTime; // 0x04
        // CryEngine 3's AnimEventInstance order, which puts the name CRC
        // exactly at +0x1C where the readers find it.
        std::uint32_t m_nAnimNumberInQueue; // 0x08
        float m_fAnimPriority;              // 0x0C
        const char *m_AnimPathName;         // 0x10
        std::int32_t m_AnimID;              // 0x18
        std::uint32_t m_EventNameLowercaseCRC32; // 0x1C
        std::byte m_opaque0020[0x8];
        const char *m_EventName;       // 0x28
        const char *m_CustomParameter; // 0x30
        const char *m_BonePathName;    // 0x38
        Vec3 m_vOffset;                // 0x40
        std::byte m_opaque004C[0x4];
        const CAnimEventData *m_pEventData; // 0x50
    };

    static_assert(sizeof(AnimEventInstance) == 0x58,
                  "AnimEventInstance size mismatch");
    static_assert(offsetof(AnimEventInstance, m_EventName) == 0x28,
                  "AnimEventInstance event-name offset mismatch");
    static_assert(offsetof(AnimEventInstance, m_vOffset) == 0x40,
                  "AnimEventInstance offset-vector mismatch");
    static_assert(offsetof(AnimEventInstance, m_pEventData) == 0x50,
                  "AnimEventInstance source-data offset mismatch");
} // namespace SDK
