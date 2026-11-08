#pragma once

#include "../shared/c_listeners.h"
#include "c_item_slot.h"
#include "c_item_slot_dynamic_spawner.h"
#include "c_save_extension_manager.h"
#include "i_item_slot_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Native ordered-tree owner used by the item-slot registry. */
    struct ItemSlotRegistryTreeLayout {
        void *m_pHead;
        std::uint64_t m_count;

        std::size_t GetCount() const {
            return static_cast<std::size_t>(m_count);
        }
    };

    static_assert(sizeof(ItemSlotRegistryTreeLayout) == 0x10,
                  "ItemSlotRegistryTreeLayout size mismatch");

    /**
     * Owns the runtime item-slot registry and its asynchronous world spawner.
     *
     * RTTI proves C_SaveExtensionManager<C_ItemSlot> as the primary base at
     * +0x00. The constructor at 0x180BE7894 then constructs a typed listener
     * collection at +0x98, an ordered registry at +0xC0, a slot-pointer vector
     * at +0xD0, and C_ItemSlotDynamicSpawner at +0xE8. Sized deletion proves
     * the complete 0x278-byte extent.
     */
    class C_ItemSlotManager : public C_SaveExtensionManager<C_ItemSlot> {
      public:
        virtual ~C_ItemSlotManager() = default;
        void unk_fn0001(C_ItemSlot *) override;
        void unk_fn0002(C_ItemSlot *) override;
        void unk_fn0003(void *) override;
        void unk_fn0004() override;
        void unk_fn0005(C_ItemSlot *) override;

        wh::shared::C_Listeners<I_ItemSlotListener, 1> &GetListeners() {
            return m_listeners;
        }
        const wh::shared::C_Listeners<I_ItemSlotListener, 1> &
        GetListeners() const {
            return m_listeners;
        }

        const ItemSlotRegistryTreeLayout &GetRegistryTree() const {
            return m_registryTree;
        }

        const DynamicSpawnVectorLayout<C_ItemSlot *> &GetSlots() const {
            return m_slots;
        }

        C_ItemSlotDynamicSpawner *GetDynamicSpawner() {
            return &m_dynamicSpawner;
        }
        const C_ItemSlotDynamicSpawner *GetDynamicSpawner() const {
            return &m_dynamicSpawner;
        }

      private:
        wh::shared::C_Listeners<I_ItemSlotListener, 1> m_listeners; // 0x98
        ItemSlotRegistryTreeLayout m_registryTree; // 0xC0
        DynamicSpawnVectorLayout<C_ItemSlot *> m_slots; // 0xD0
        C_ItemSlotDynamicSpawner m_dynamicSpawner; // 0xE8
        bool m_state0270; // initialized false
        std::byte m_padding0271[0x7];
    };

    static_assert(sizeof(C_ItemSlotManager) == 0x278,
                  "C_ItemSlotManager size mismatch");
} // namespace SDK::wh::entitymodule
