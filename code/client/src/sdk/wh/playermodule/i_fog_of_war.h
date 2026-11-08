#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * The per-level fog of war, one instance per level name. Vtable at
     * 0x183A88B30; C_NullFogOfWar overrides every slot with a nullsub, so calls
     * are inert rather than unsafe when no level fog is installed.
     */
    class I_FogOfWar {
      public:
        /**
         * Deserialises a saved fog. C_FOWManager::Load calls it on a fog it has
         * just interned, so it is the only thing that can undo a reveal.
         */
        static constexpr std::size_t kLoadSlot = 12;

        virtual ~I_FogOfWar() = default;
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
        virtual void Opaque07() = 0;
        virtual void Opaque08() = 0;

        /**
         * Clears the fog over the whole level -- the whole of the game's own
         * `player_revealFow`. Idempotent and allocation-free: visibility is a
         * quad tree whose root pointer is tagged in its low two bits, and this
         * drops the tree and leaves the bare all-revealed tag in its place.
         */
        virtual void RevealAll() = 0; // +0x48

        /**
         * Clears the fog around a world X/Y by the reveal radius the game last
         * set (slot 6, from `C_FOWManager::Update`) and tells the map overlay;
         * with `reveal` false it only notifies. 0x180B581F8 on C_FogOfWar<3,4>.
         */
        virtual void Reveal(SDK::Vec2 position, bool reveal) = 0; // +0x50

        virtual void Opaque11() = 0;
        virtual void Opaque12() = 0; // +0x60, see kLoadSlot
    };
} // namespace SDK::wh::playermodule
