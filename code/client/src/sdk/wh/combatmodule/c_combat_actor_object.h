#pragma once

#include <cstddef>

namespace SDK::wh::combatmodule {
    class C_CombatActor;

    /**
     * Shared combat-side object associated with an actor.
     *
     * RTTI identifies this namespace-qualified type as a class. Its table at
     * 0x183A60E60 contains the virtual destructor, two pure operations, and
     * three emitted operations. Its shared constructor stores the owning
     * C_CombatActor at +0x08 and registers this object with that owner. Sized
     * deletion proves the exact 0x10-byte layout.
     */
    class C_CombatActorObject {
      public:
        virtual ~C_CombatActorObject() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() {} // emitted operation
        virtual void unk_fn0004() {} // emitted operation
        virtual void unk_fn0005() {} // emitted operation

        C_CombatActor *GetCombatActor() const { return m_pCombatActor; }

      private:
        C_CombatActor *m_pCombatActor; // 0x08
    };

    static_assert(sizeof(C_CombatActorObject) == 0x10,
                  "C_CombatActorObject size mismatch");
} // namespace SDK::wh::combatmodule
