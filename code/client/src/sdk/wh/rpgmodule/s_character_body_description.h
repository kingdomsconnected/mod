#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * How a character looks, as five nodes of the game's character-component
     * tree (`Libs/Tables/Character/CharacterComponent.xml`).
     *
     * Members and offsets from the RTTR registration at `0x181AC6B00`.
     *
     * Borrowed, always: every string is on the game's heap, and
     * `SDK::CryStringT` has no destructor, so copying this shares the buffers
     * without touching their refcounts. A description filled by
     * `C_Soul::GetBodyDescription` must be freed by the engine's own
     * destructor, which is why nothing here owns one.
     */
    struct S_CharacterBodyDescription {
        // Part indices, as the C_Soul slots take them. Plain bytes rather
        // than an enum because the natives declare them `unsigned char`.
        static constexpr std::uint8_t kBody      = 0;
        static constexpr std::uint8_t kHead      = 1;
        static constexpr std::uint8_t kHair      = 2;
        static constexpr std::uint8_t kBeard     = 3;
        static constexpr std::uint8_t kUnderwear = 4;
        static constexpr std::uint8_t kPartCount = 5;

        /**
         * A soul holds three of these. A read takes the first layer whose
         * string for that part is non-empty, look-as first; both overrides
         * are lazily allocated, so an absent one falls through as an empty
         * string would.
         *
         * `kLayerBase` overwrites the soul's own authored description in
         * place. `kLayerLookAs` is where `C_Actor::MakeLookAsSoul` writes.
         */
        static constexpr std::uint32_t kLayerBase   = 0;
        static constexpr std::uint32_t kLayerBarber = 1;
        static constexpr std::uint32_t kLayerLookAs = 2;

        CryStringT<char> body;      // 0x00
        CryStringT<char> head;      // 0x08
        CryStringT<char> hair;      // 0x10
        CryStringT<char> beard;     // 0x18
        CryStringT<char> underwear; // 0x20, a Clothing node rather than one of its own kind

        static void AssertLayout() {
            static_assert(offsetof(S_CharacterBodyDescription, body) == 0x00, "S_CharacterBodyDescription::body offset mismatch");
            static_assert(offsetof(S_CharacterBodyDescription, head) == 0x08, "S_CharacterBodyDescription::head offset mismatch");
            static_assert(offsetof(S_CharacterBodyDescription, hair) == 0x10, "S_CharacterBodyDescription::hair offset mismatch");
            static_assert(offsetof(S_CharacterBodyDescription, beard) == 0x18, "S_CharacterBodyDescription::beard offset mismatch");
            static_assert(offsetof(S_CharacterBodyDescription, underwear) == 0x20, "S_CharacterBodyDescription::underwear offset mismatch");
        }
    };

    static_assert(sizeof(S_CharacterBodyDescription) == 0x28, "S_CharacterBodyDescription size mismatch");
} // namespace SDK::wh::rpgmodule
