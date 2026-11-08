#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct IEntityClass;
} // namespace SDK

namespace SDK::wh::entitymodule {
    /**
     * One row of `C_ActorClassDatabase` -- what the engine remembers about an
     * entity class that can be an actor.
     *
     * Built once per class by `C_ActorClassDatabase::AddClass` (0x181EB2680)
     * from the class's own script table, and looked up by the case-insensitive
     * CRC32 of the class name. Every actor caches its row at `C_Actor + 0x758`.
     *
     * Only classes that declare an `ActionController`, an `AnimDatabase1P/3P`
     * and a `Properties.fileModel` get a row at all -- the builder skips the
     * rest -- so a row existing already says the class is animatable.
     *
     * The strings are the engine's own copy-on-write `CryStringT`s. Reading
     * one is free; writing one has to go through `CryStringT::Assign`, which
     * is the engine's `operator=` and keeps the buffer on the game's heap.
     */
    struct S_ActorClass {
        /** Case-insensitive CRC32 of the entity class name; the lookup key. */
        std::uint32_t m_classNameHash; // 0x00
        std::byte m_padding0004[0x4];  // 0x04

        /** The entity class name the hash above is taken of; AddClass (0x181EB2680) copies it into the row. */
        CryStringT<char> m_className;  // 0x08

        IEntityClass *m_pEntityClass;  // 0x10

        /** `ActionController` -- the Mannequin controller definition. */
        CryStringT<char> m_actionController; // 0x18

        /** `AnimDatabase1P` or `AnimDatabase3P`, and its own hash. */
        CryStringT<char> m_animDatabase;     // 0x20
        std::uint32_t m_animDatabaseHash;    // 0x28
        std::byte m_padding002C[0x4];        // 0x2C

        /** `Properties.fileModel` -- the character this class wears. */
        CryStringT<char> m_fileModel;        // 0x30

        /** `OpponentMnTag`, and its own hash. */
        CryStringT<char> m_opponentMnTag;    // 0x38
        std::uint32_t m_opponentMnTagHash;   // 0x40
        std::byte m_padding0044[0x4];        // 0x44

        /**
         * `CombatOpponentMnTag` -- and the gate on this class ever fighting.
         *
         * `C_Actor::GetOrCreateCombatActor` refuses to build the combat side
         * of a body whose class leaves this empty, so such a class has no
         * guard, no swing and no combat state of any kind -- every combat call
         * against it is a silent no-op rather than an error.
         *
         * `player.lua` and `NPC.lua` declare `"oppMale"`, `NPC_Female`
         * `"oppFemale"`, the animals their own. **`NPC_NAI` declares none**,
         * which is what makes the brainless humanoid unable to fight.
         */
        CryStringT<char> m_combatOpponentMnTag; // 0x48

        /** The animation database's hash again. */
        std::uint32_t m_animDatabaseHash2;      // 0x50
        std::byte m_padding0054[0x4];           // 0x54

        /** Whether the string is the engine's shared empty rather than text. */
        static bool IsEmpty(const CryStringT<char> &value) {
            // A CryStringT keeps its length in the twelve-byte header in front
            // of the payload, at -8. The engine's own gate reads exactly this.
            return value.m_pData == nullptr || *reinterpret_cast<const std::int32_t *>(value.m_pData - 8) == 0;
        }

        bool CanFight() const {
            return !IsEmpty(m_combatOpponentMnTag);
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_ActorClass, m_className) == 0x08, "S_ActorClass::m_className offset mismatch");
            static_assert(offsetof(S_ActorClass, m_pEntityClass) == 0x10, "S_ActorClass::m_pEntityClass offset mismatch");
            static_assert(offsetof(S_ActorClass, m_actionController) == 0x18, "S_ActorClass::m_actionController offset mismatch");
            static_assert(offsetof(S_ActorClass, m_animDatabase) == 0x20, "S_ActorClass::m_animDatabase offset mismatch");
            static_assert(offsetof(S_ActorClass, m_fileModel) == 0x30, "S_ActorClass::m_fileModel offset mismatch");
            static_assert(offsetof(S_ActorClass, m_opponentMnTag) == 0x38, "S_ActorClass::m_opponentMnTag offset mismatch");
            static_assert(offsetof(S_ActorClass, m_combatOpponentMnTag) == 0x48, "S_ActorClass::m_combatOpponentMnTag offset mismatch");
        }
    };

    // The database divides its byte span by this to count its rows.
    static_assert(sizeof(S_ActorClass) == 0x58, "S_ActorClass size mismatch");
} // namespace SDK::wh::entitymodule
