#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * A resolved ATL control: a lowercased CRC32 of the control's name, 0 when
     * unresolved. Names are authored in `Libs/GameAudio/*.xml`; resolve them
     * through `IAudioSystem`, never from a compiled-in list.
     */
    using AudioControlId = std::uint32_t;
    using AudioSwitchStateId = std::uint32_t;
    using AudioEnvironmentId = std::uint32_t;

    /** Which of an entity's aux audio proxies a call addresses. */
    using AudioProxyId = std::uint32_t;

    inline constexpr AudioControlId kInvalidAudioControlId = 0;

    /** The aux proxy `CEntityAudioProxy` creates with itself, and the one every game call site passes. */
    inline constexpr AudioProxyId kDefaultAudioProxyId = 1;

    /** A wildcard rather than an id: addresses every aux proxy on the entity. */
    inline constexpr AudioProxyId kAllAudioProxyIds = 0;

    /** Only `None` is proven: it is what every call site in the game passes. */
    enum class ELipSyncMethod : std::int32_t {
        None = 0,
    };

    /** Indexes the switch-state table at 0x18549CFF0, filled by 0x181174A20. Index 0 is never written. */
    enum class EAudioOcclusionType : std::uint32_t {
        None = 0,
        Ignore = 1,
        SingleRay = 2,
        MultiRay = 3,
    };

    /** Passed by const reference to every `ExecuteTrigger`. `Empty()` is a copy of the game's own magic static at 0x185482C88. */
    struct SAudioCallBackInfo {
        const void *pObjectToNotify; // 0x00
        void *pUserData; // 0x08
        void *pUserDataOwner; // 0x10
        std::uint32_t requestFlags; // 0x18, 1 == normal priority
        std::uint32_t m_opaque001C;

        static const SAudioCallBackInfo &Empty() {
            static const SAudioCallBackInfo empty {nullptr, nullptr, nullptr, 1, 0};
            return empty;
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(SAudioCallBackInfo, pObjectToNotify) == 0x00, "SAudioCallBackInfo::pObjectToNotify offset mismatch");
            static_assert(offsetof(SAudioCallBackInfo, pUserData) == 0x08, "SAudioCallBackInfo::pUserData offset mismatch");
            static_assert(offsetof(SAudioCallBackInfo, pUserDataOwner) == 0x10, "SAudioCallBackInfo::pUserDataOwner offset mismatch");
            static_assert(offsetof(SAudioCallBackInfo, requestFlags) == 0x18, "SAudioCallBackInfo::requestFlags offset mismatch");
        }
    };

    static_assert(sizeof(SAudioCallBackInfo) == 0x20, "SAudioCallBackInfo size mismatch");
} // namespace SDK
