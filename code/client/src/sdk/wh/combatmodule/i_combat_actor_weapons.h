#pragma once

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Abstract combat-side weapon-state interface for an actor.
     *
     * RTTI identifies this namespace-qualified type as a class. Its table at
     * 0x183C2C200 contains the virtual destructor followed by exactly six pure
     * operations. Their stripped source contracts remain opaque.
     */
    class I_CombatActorWeapons {
      public:
        virtual ~I_CombatActorWeapons() = default;
        virtual void *unk_fn0001() = 0;
        virtual void *unk_fn0002(std::uint32_t weaponIndex) = 0;
        virtual std::uint8_t unk_fn0003() = 0;
        virtual bool unk_fn0004() = 0;
        virtual bool unk_fn0005() = 0;
        virtual bool unk_fn0006() = 0;
    };

    static_assert(sizeof(I_CombatActorWeapons) == 0x8,
                  "I_CombatActorWeapons size mismatch");
} // namespace SDK::wh::combatmodule
