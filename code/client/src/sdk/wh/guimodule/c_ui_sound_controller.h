#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /** UI audio cues resolved by the game's own GUI sound controller. */
    enum class E_UISound : std::uint8_t {
        ModalConfirm = 1,
        ModalCancel  = 2,
        MenuOpen     = 18,
    };

    struct S_UISoundIdEntry {
        E_UISound sound; // 0x00
        std::byte m_padding0001[0x3];
        std::uint32_t audioTriggerId; // 0x04
    };

    static_assert(offsetof(S_UISoundIdEntry, sound) == 0x0, "S_UISoundIdEntry::sound offset mismatch");
    static_assert(offsetof(S_UISoundIdEntry, audioTriggerId) == 0x4, "S_UISoundIdEntry::audioTriggerId offset mismatch");
    static_assert(sizeof(S_UISoundIdEntry) == 0x8, "S_UISoundIdEntry size mismatch");

    struct S_UISoundIdMap {
        S_UISoundIdEntry *entries; // 0x00
        std::size_t size; // 0x08
        std::size_t capacity; // 0x10
    };

    static_assert(offsetof(S_UISoundIdMap, entries) == 0x0, "S_UISoundIdMap::entries offset mismatch");
    static_assert(offsetof(S_UISoundIdMap, size) == 0x8, "S_UISoundIdMap::size offset mismatch");
    static_assert(offsetof(S_UISoundIdMap, capacity) == 0x10, "S_UISoundIdMap::capacity offset mismatch");
    static_assert(sizeof(S_UISoundIdMap) == 0x18, "S_UISoundIdMap size mismatch");

    struct S_UIAudioControlIdEntry {
        SDK::CryStringT<char> name; // Native borrowed storage.
        std::uint32_t controlId;
        std::byte m_padding000C[0x4];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_UIAudioControlIdEntry, name) == 0x0, "S_UIAudioControlIdEntry::name offset mismatch");
            static_assert(offsetof(S_UIAudioControlIdEntry, controlId) == 0x8, "S_UIAudioControlIdEntry::controlId offset mismatch");
        }
    };
    static_assert(sizeof(S_UIAudioControlIdEntry) == 0x10, "S_UIAudioControlIdEntry size mismatch");

    struct S_UIAudioControlIdMap {
        S_UIAudioControlIdEntry *entries;
        std::size_t size;
        std::byte m_opaque0010[0x8];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_UIAudioControlIdMap, entries) == 0x0, "S_UIAudioControlIdMap::entries offset mismatch");
            static_assert(offsetof(S_UIAudioControlIdMap, size) == 0x8, "S_UIAudioControlIdMap::size offset mismatch");
        }
    };
    static_assert(sizeof(S_UIAudioControlIdMap) == 0x18, "S_UIAudioControlIdMap size mismatch");

    struct S_UITriggerPlaybackEntry {
        std::uint32_t triggerId;
        std::byte m_padding0004[0x4];
        std::uint64_t timestamp;
        std::int32_t repeatCount;
        std::byte m_padding0014[0x4];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_UITriggerPlaybackEntry, triggerId) == 0x0, "S_UITriggerPlaybackEntry::triggerId offset mismatch");
            static_assert(offsetof(S_UITriggerPlaybackEntry, timestamp) == 0x8, "S_UITriggerPlaybackEntry::timestamp offset mismatch");
            static_assert(offsetof(S_UITriggerPlaybackEntry, repeatCount) == 0x10, "S_UITriggerPlaybackEntry::repeatCount offset mismatch");
        }
    };
    static_assert(sizeof(S_UITriggerPlaybackEntry) == 0x18, "S_UITriggerPlaybackEntry size mismatch");

    struct S_UITriggerPlaybackMap {
        S_UITriggerPlaybackEntry *entries;
        std::size_t size;
        std::byte m_opaque0010[0x8];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_UITriggerPlaybackMap, entries) == 0x0, "S_UITriggerPlaybackMap::entries offset mismatch");
            static_assert(offsetof(S_UITriggerPlaybackMap, size) == 0x8, "S_UITriggerPlaybackMap::size offset mismatch");
        }
    };
    static_assert(sizeof(S_UITriggerPlaybackMap) == 0x18, "S_UITriggerPlaybackMap size mismatch");

    /**
     * Plain 0x48-byte UI sound owner stored at C_GUIModule +0x80.
     *
     * The game's concrete source type is not RTTI-exposed. The address suffix
     * keeps this descriptive name distinct from an asserted original name.
     */
    class C_UISoundController180559ED0 final {
      public:
        void Play(E_UISound sound) {
            hook::this_call<void>(gPatterns.C_UISoundController180559ED0__Play, this, static_cast<std::uint8_t>(sound));
        }

      private:
        S_UIAudioControlIdMap m_controlIds; // 0x00, sorted by name.
        S_UISoundIdMap m_soundIds; // 0x18
        S_UITriggerPlaybackMap m_triggerPlayback; // 0x30, sorted by trigger ID.

        static void AssertLayout() {
            static_assert(offsetof(C_UISoundController180559ED0, m_controlIds) == 0x0, "C_UISoundController180559ED0::m_controlIds offset mismatch");
            static_assert(offsetof(C_UISoundController180559ED0, m_soundIds) == 0x18, "C_UISoundController180559ED0::m_soundIds offset mismatch");
            static_assert(offsetof(C_UISoundController180559ED0, m_triggerPlayback) == 0x30, "C_UISoundController180559ED0::m_triggerPlayback offset mismatch");
        }
    };

    static_assert(sizeof(C_UISoundController180559ED0) == 0x48, "C_UISoundController180559ED0 size mismatch");
} // namespace SDK::wh::guimodule
