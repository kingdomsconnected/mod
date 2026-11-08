#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One line an actor is about to say.
     *
     * The three strings must be engine-owned: Speak copies them through the
     * engine's own assignment, which reads the refcount header behind each
     * pointer. Build them with ScopedCryString.
     */
    struct S_SpeakData {
        CryStringT<char> soundEvent; // 0x00 empty skips the voice line
        CryStringT<char> text;       // 0x08 what the bubble shows
        CryStringT<char> mood;       // 0x10 "100", "inner", ...

        // Clear for raw text; the dialogue path sets it because its own lookup
        // falls back to the key "ui_undefined_response".
        bool isLocalizable; // 0x18

        std::uint8_t unk0019;    // 0x19 seeded to 1; the update refuses to draw without it
        bool isPlayerControlled; // 0x1A
        bool force;              // 0x1B bypasses the can-speak gate
        std::byte padding001C[0x4];
        void *pContext;       // 0x20
        std::uint8_t unk0028; // 0x28
        std::byte padding0029[0x3];

        // Seconds. Arms the end timer; the bubble draws only while it runs, so
        // zero here shows nothing at all.
        float duration; // 0x2C

        float unk0030;     // 0x30 the dialogue path fills this from the wav
        float maxDistance; // 0x34 negative uses the game's own bubble distance
    };

    static_assert(sizeof(S_SpeakData) == 0x38, "S_SpeakData size mismatch");
} // namespace SDK::wh::entitymodule
