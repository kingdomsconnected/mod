#pragma once

#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Combat-actor object with registered update behavior.
     *
     * RTTI proves the hierarchy through C_CombatActorObject. Its table at
     * 0x183A60E98 contains the six inherited entries followed by four emitted
     * update operations. Construction stores its integral registration kind
     * at +0x10 and conditionally registers nonzero kinds with the owning
     * combat actor. The source enum/type name remains stripped. Construction
     * and sized deletion prove the exact 0x18-byte layout.
     */
    class C_CombatActorUpdatedObject : public C_CombatActorObject {
      public:
        ~C_CombatActorUpdatedObject() override = default;

        virtual void unk_fn0006() {} // emitted operation
        virtual void unk_fn0007() {} // emitted operation
        virtual void unk_fn0008() {} // emitted operation
        virtual void unk_fn0009() {} // emitted operation

        std::uint32_t GetUpdateKind0010Raw() const { return m_updateKind0010; }

      private:
        std::uint32_t m_updateKind0010; // 0x10
        std::byte m_padding0014[0x4];   // 0x14
    };

    static_assert(sizeof(C_CombatActorUpdatedObject) == 0x18,
                  "C_CombatActorUpdatedObject size mismatch");
} // namespace SDK::wh::combatmodule
