#pragma once

#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Combat-side weapon solver associated with an actor.
     *
     * Destruction proves C_CombatActorObject as the direct base. The table at
     * 0x183B91338 contains exactly the six inherited entries and adds no
     * operations; the following COL proves its boundary. Construction clears
     * the complete tail field-by-field. Destruction releases the reference
     * control block at +0x20 and the owned 0x20-byte object at +0x28, while
     * scalar deletion proves the exact 0x50-byte layout.
     */
    class C_WeaponSolver : public C_CombatActorObject {
      public:
        ~C_WeaponSolver() override = default;

        std::uint16_t GetState0010Raw() const { return m_state0010; }
        void *GetReferencedObject0018Raw() const {
            return m_pReferencedObject0018;
        }
        void *GetReferenceControlBlock0020Raw() const {
            return m_pReferenceControlBlock0020;
        }
        void *GetOwnedState0028Raw() const { return m_pOwnedState0028; }
        bool GetState004CRaw() const { return m_state004C; }

      private:
        std::uint16_t m_state0010;
        std::byte m_padding0012[0x6];
        void *m_pReferencedObject0018;
        void *m_pReferenceControlBlock0020;
        void *m_pOwnedState0028;
        std::byte m_state0030[0x1C];
        bool m_state004C;
        std::byte m_padding004D[0x3];
    };

    static_assert(sizeof(C_WeaponSolver) == 0x50,
                  "C_WeaponSolver size mismatch");
} // namespace SDK::wh::combatmodule
