#pragma once

#include "../../patterns.h"

#include "../../cryengine/action/c_game_object.h"
#include "../../cryengine/action/i_actor.h"
#include "../../cryengine/action/i_game_object.h"
#include "../../cryengine/entitysystem/i_entity.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * One place along a battlement or window where a body stands to lean out
     * or shoot. The slots belong to their `C_Battlement` and live as long as
     * it does.
     */
    class C_BattlementSlot {
      public:
        /** The `Battlement` entity this slot belongs to (`GetBattlement`, 0x182E586B8, reads it). */
        EntityId GetBattlementEntityId() const {
            return m_battlementEntityId;
        }

      private:
        std::byte m_opaque0000[0x2C];
        EntityId m_battlementEntityId; // 0x2C
        // 0x30 follows: the actor using the slot, set by C_BattleArchery::Start
        // and cleared by its Finish; GetUnoccupiedSlot refuses a slot with one.

        static void AssertLayout() {
            static_assert(offsetof(C_BattlementSlot, m_battlementEntityId) == 0x2C, "C_BattlementSlot::m_battlementEntityId offset mismatch");
        }
    };

    /**
     * The game object extension of a `Battlement` entity
     * (`Scripts/Entities/WH/Minigames/Battlement.lua`, a two-point shape):
     * a window or a stretch of wall cut into the slots a body can lean out of
     * or shoot from, held in the vector at +0x68 that `GetUnoccupiedSlot` and
     * `TryReserveSlot` (0x182E5BAF4) index.
     */
    class C_Battlement: public SDK::IGameObjectExtension {
      public:
        /** The extension of `entity`, or null when it is no `Battlement`: what `GetBattlementFromEntity` (0x182E586D8) queries by the same name. */
        static C_Battlement *Find(IEntity *entity) {
            return static_cast<C_Battlement *>(CGameObject::QueryExtension(entity, "Battlement"));
        }

        /** Slot `index` whoever uses it, or null when there is none. */
        C_BattlementSlot *GetSlot(std::int32_t index) const {
            if (index < 0 || index >= m_slotsEnd - m_slotsBegin) {
                return nullptr;
            }
            return m_slotsBegin[index];
        }

        /** Slot `index`, or null when there is none or another actor uses it (0x182E58B6C). */
        C_BattlementSlot *GetUnoccupiedSlot(std::int32_t index) const {
            return hook::this_call<C_BattlementSlot *>(gPatterns.C_Battlement__GetUnoccupiedSlot, this, index);
        }

        /** Where `slot` is in this battlement's list, or -1 when it is not one of its slots. */
        std::int32_t IndexOf(const C_BattlementSlot *slot) const {
            for (C_BattlementSlot *const *it = m_slotsBegin; it != m_slotsEnd; ++it) {
                if (*it == slot) {
                    return static_cast<std::int32_t>(it - m_slotsBegin);
                }
            }
            return -1;
        }

      private:
        std::byte m_opaque0040[0x28];
        C_BattlementSlot **m_slotsBegin; // 0x68
        C_BattlementSlot **m_slotsEnd;   // 0x70

        static void AssertLayout() {
            static_assert(offsetof(C_Battlement, m_slotsBegin) == 0x68, "C_Battlement::m_slotsBegin offset mismatch");
            static_assert(offsetof(C_Battlement, m_slotsEnd) == 0x70, "C_Battlement::m_slotsEnd offset mismatch");
        }
    };
} // namespace SDK::wh::playermodule
