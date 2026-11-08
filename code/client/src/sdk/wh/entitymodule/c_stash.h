#pragma once

#include "../../patterns.h"

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../types.h"
#include "../framework/wuid.h"
#include "c_lock_base.h"
#include "c_save_extension_manager.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Inventory;

    /**
     * The stolen-goods bookkeeping, and the whole of a stash's saved state:
     * `C_Stash::FullSerialize` writes group "Stash" / "ValueWatcher" holding
     * exactly `RobbedValue`, `OnOpenValue` and `Timestamp`.
     *
     * `OnStashOpened` samples the inventory's value into `OnOpenValue`;
     * `OnStashClosed` diffs it and accumulates into `RobbedValue`. The two
     * rates come from the rpg config table, so they are not per-stash.
     */
    struct S_StashValueWatcher {
        std::uint32_t m_robbedValue;   // 0x00, serialized "RobbedValue"
        std::uint32_t m_onOpenValue;   // 0x04, serialized "OnOpenValue"
        float m_decayRate;             // 0x08, StashRobbedValueDecayRate
        float m_bottomThreshold;       // 0x0C, StashRobbedValueBottomThreshold
        std::int64_t m_timestamp;      // 0x10, serialized "Timestamp"
    };

    static_assert(sizeof(S_StashValueWatcher) == 0x18, "S_StashValueWatcher size mismatch");

    /**
     * A container in the world: chest, corpse, cart, nest, destructible.
     *
     * Five entity classes carry this extension -- Stash, StashCorpse, Nest,
     * CartStash, DestroStash -- all driven by `Scripts/Entities/WH/Stash/*.lua`.
     *
     * Identity is the entity's own GUID: `GetStashGuid` is
     * `CryGUID{hipart = 0, lopart = entityGuid}`, `C_StashManager` keys every
     * stash by it, and `CreateInventoryForEntity` derives the same value for
     * the inventory. `Init` fails outright on a GUID already registered.
     *
     * Three behaviours matter to anything owning a stash's contents from
     * outside: `Release` destroys the inventory, so contents never survive a
     * destream; `RestockNow` reconciles against the preset list, removing
     * everything it does not name; and `ScheduledUpdate` reaches the same
     * reconciliation without going through `RestockNow`.
     *
     * Five allocation sites request 0x128 bytes, which proves the size.
     */
    class C_Stash: public C_LockBase {
      public:
        ~C_Stash() override = default;

        /**
         * Created by `Init`, destroyed by `Release`. The engine's own reflected
         * getter is a single `mov rax, [rcx+0B8h]`.
         */
        C_Inventory *GetInventory() const {
            return m_pInventory;
        }

        /**
         * `CryGUID{hipart = 0, lopart = entity->GetGuid()}` -- both the
         * `C_StashManager` key and the GUID the inventory is created under.
         *
         * Reproduced rather than called. The native at 0x180A2F2D0 writes the
         * GUID a byte at a time, which reads like a byte swap and is not one;
         * it is also byte-identical to a helper at 0x1804572C4 that reads the
         * entity from +0x30 instead of +0x38.
         */
        CryGUID GetStashGuid() const {
            return CryGUID {0, GetEntity()->GetGuid()};
        }

        /**
         * The linked stash's inventory, or the null WUID -- the master/slave
         * mechanism that lets several entities share one inventory.
         *
         * The native returns through a hidden pointer despite a WUID being
         * eight bytes, so that pointer is spelled out as the second argument.
         */
        framework::WUID GetMasterInventoryWuid() const {
            framework::WUID result {};
            hook::this_call<void>(gPatterns.C_Stash__GetMasterInventoryWuid, this, &result);
            return result;
        }

        /**
         * Reconciles the inventory against the preset list. Additive only in
         * appearance: items the list does not name are removed.
         */
        void RestockNow() {
            hook::this_call<void>(gPatterns.C_Stash__RestockNow, this);
        }

        /** Spills the whole inventory onto the ground at `position`. */
        void DumpItemsToWorld(const Vec3 &position) {
            hook::this_call<void>(gPatterns.C_Stash__DumpItemsToWorld, this, &position);
        }

        /**
         * The native "stash UI opened" event, raised by the stash's own Lua
         * through `C_ScriptBindStash::ReportOpened`. `byPlayer` separates the
         * player from a scripted open.
         */
        void OnOpened(bool byPlayer) {
            hook::this_call<void>(gPatterns.C_Stash__OnOpened, this, byPlayer);
        }

        /** The matching close event, from `C_ScriptBindStash::ReportClosed`. */
        void OnClosed() {
            hook::this_call<void>(gPatterns.C_Stash__OnClosed, this);
        }

        SignalLayout *GetOnOpenedSignal() {
            return &m_onOpened;
        }
        SignalLayout *GetOnClosedSignal() {
            return &m_onClosed;
        }

        const S_StashValueWatcher &GetValueWatcher() const {
            return m_valueWatcher;
        }
        S_StashValueWatcher &GetValueWatcher() {
            return m_valueWatcher;
        }

        /** `Properties.Database.sInventoryPreset`, empty when unset. */
        const char *GetInventoryPreset() const {
            return m_inventoryPreset.m_pData;
        }

        std::uint16_t GetRestockPeriodDays() const {
            return m_restockPeriodDays;
        }
        std::uint16_t GetDestockPeriodDays() const {
            return m_destockPeriodDays;
        }

        /** -1 when no restock is pending; `RestockNow` clears it back. */
        std::int32_t GetRestockScheduleId() const {
            return m_restockScheduleId;
        }

        /**
         * The `I_ScheduleUpdatable` subobject at +0x70, modelled as an explicit
         * displacement rather than a second source base -- the way
         * `CGameObject` models its `IGameObject`, and for the same reason:
         * spelling out nine unproven virtuals would invent contracts.
         */
        void *GetScheduleUpdatable() {
            return &m_pScheduleUpdatableVtable;
        }

        /** The `I_InventoryListener` subobject, at +0x78. */
        void *GetInventoryListener() {
            return &m_pInventoryListenerVtable;
        }

      private:
        void *m_pScheduleUpdatableVtable;   // 0x70
        void *m_pInventoryListenerVtable;   // 0x78
        SignalLayout m_onOpened;            // 0x80
        SignalLayout m_onClosed;            // 0x90
        S_StashValueWatcher m_valueWatcher; // 0xA0
        C_Inventory *m_pInventory;          // 0xB8
        CryStringT<char> m_inventoryPreset; // 0xC0
        void *m_pRestockScheduleHandle;     // 0xC8
        std::int32_t m_restockScheduleId;   // 0xD0
        std::uint16_t m_restockPeriodDays;  // 0xD4
        std::uint16_t m_destockPeriodDays;  // 0xD6

        // 0xD8 begins a second observer subobject that the open and close
        // events drive alongside the value watcher. Its first field is the
        // owner entity id, which Init seeds with the stash's own id, and it
        // owns the MSVC unordered container at 0xE0. Only the id is named.
        EntityId m_ownerEntityId;                // 0xD8
        std::byte m_padding00DC[0x4];            // 0xDC
        SaveExtensionHashLayout m_observerState; // 0xE0
        bool m_flag0120;                         // 0x120, ctor 0x180705B24 clears it
        std::byte m_padding0121[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_Stash, m_pScheduleUpdatableVtable) == 0x70, "C_Stash::m_pScheduleUpdatableVtable offset mismatch");
            static_assert(offsetof(C_Stash, m_pInventoryListenerVtable) == 0x78, "C_Stash::m_pInventoryListenerVtable offset mismatch");
            static_assert(offsetof(C_Stash, m_onOpened) == 0x80, "C_Stash::m_onOpened offset mismatch");
            static_assert(offsetof(C_Stash, m_onClosed) == 0x90, "C_Stash::m_onClosed offset mismatch");
            static_assert(offsetof(C_Stash, m_valueWatcher) == 0xA0, "C_Stash::m_valueWatcher offset mismatch");
            static_assert(offsetof(C_Stash, m_pInventory) == 0xB8, "C_Stash::m_pInventory offset mismatch");
            static_assert(offsetof(C_Stash, m_inventoryPreset) == 0xC0, "C_Stash::m_inventoryPreset offset mismatch");
            static_assert(offsetof(C_Stash, m_restockScheduleId) == 0xD0, "C_Stash::m_restockScheduleId offset mismatch");
            static_assert(offsetof(C_Stash, m_restockPeriodDays) == 0xD4, "C_Stash::m_restockPeriodDays offset mismatch");
            static_assert(offsetof(C_Stash, m_destockPeriodDays) == 0xD6, "C_Stash::m_destockPeriodDays offset mismatch");
            static_assert(offsetof(C_Stash, m_ownerEntityId) == 0xD8, "C_Stash::m_ownerEntityId offset mismatch");
            static_assert(offsetof(C_Stash, m_observerState) == 0xE0, "C_Stash::m_observerState offset mismatch");
            static_assert(offsetof(C_Stash, m_flag0120) == 0x120, "C_Stash::m_flag0120 offset mismatch");
        }
    };

    static_assert(sizeof(C_Stash) == 0x128, "C_Stash size mismatch");
} // namespace SDK::wh::entitymodule
