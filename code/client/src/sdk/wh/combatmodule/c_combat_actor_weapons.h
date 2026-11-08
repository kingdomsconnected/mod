#pragma once

#include "../framework/wuid.h"
#include "c_combat_actor_updated_object.h"
#include "i_combat_actor_weapons.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Exact element layout of C_CombatActorWeapons' native weapon array. */
    struct CombatActorWeaponEntryLayout {
        std::uint32_t m_slot0000Raw;
        std::byte m_padding0004[0x4];
        framework::WUID m_itemId;
    };

    static_assert(sizeof(CombatActorWeaponEntryLayout) == 0x10,
                  "CombatActorWeaponEntryLayout size mismatch");

    /**
     * Update-aware combat object responsible for an actor's weapon state.
     *
     * RTTI proves C_CombatActorUpdatedObject at offset 0 and
     * I_CombatActorWeapons at offset 0x18. Its two tables contain exactly the
     * inherited virtual operations. Construction installs the native empty
     * array at +0x20 and clears the byte at +0x28. Its rebuild path iterates
     * 0x10-byte records containing a 32-bit slot value and item WUID; the
     * native array header stores its element count immediately before the
     * first record. Sized deletion proves the exact 0x30-byte layout.
     */
    class C_CombatActorWeapons : public C_CombatActorUpdatedObject,
                                 public I_CombatActorWeapons {
      public:
        ~C_CombatActorWeapons() override = default;

        std::size_t GetWeaponEntryCount() const {
            return m_pWeaponEntries != nullptr
                       ? reinterpret_cast<const std::uint32_t *>(
                             m_pWeaponEntries)[-1]
                       : 0;
        }

        CombatActorWeaponEntryLayout *GetWeaponEntry(std::size_t index) {
            return index < GetWeaponEntryCount() ? &m_pWeaponEntries[index]
                                                  : nullptr;
        }

        const CombatActorWeaponEntryLayout *GetWeaponEntry(
            std::size_t index) const {
            return index < GetWeaponEntryCount() ? &m_pWeaponEntries[index]
                                                  : nullptr;
        }

        bool GetState0028Raw() const { return m_state0028; }

      private:
        CombatActorWeaponEntryLayout *m_pWeaponEntries; // 0x20
        bool m_state0028;                               // 0x28
        std::byte m_padding0029[0x7];                   // 0x29
    };

    static_assert(sizeof(C_CombatActorWeapons) == 0x30,
                  "C_CombatActorWeapons size mismatch");
} // namespace SDK::wh::combatmodule
