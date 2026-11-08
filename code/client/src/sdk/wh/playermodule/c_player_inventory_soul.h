#pragma once

#include "../framework/c_localized_string.h"
#include "c_outfit_manager.h"
#include "i_player_inventory_soul.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class C_InventorySoul;
}

namespace SDK::wh::playermodule {
    struct PlayerInventorySharedOwnerLayout {
        void *m_pObject;
        void *m_pReferenceCount;
    };

    /** Stripped 0x10-byte value stored in each player-inventory entry. */
    struct PlayerInventoryEntryValueLayout {
        std::byte m_storage[0x10];
    };

    /** Stripped 0x58-byte object embedded three times in each entry. */
    struct PlayerInventoryEntryObjectLayout {
        std::byte m_storage[0x58];
    };

    /**
     * Exact native boundary of a persisted player-inventory entry.
     *
     * Construction, copy, update, and destruction paths prove every field
     * boundary below. Names remain offset-based where stripped symbols do not
     * establish the value's source-level meaning.
     */
    struct PlayerInventoryEntryLayout {
        void *m_pState0000;                                // 0x000
        PlayerInventoryEntryValueLayout *m_pValuesBegin;   // 0x008
        PlayerInventoryEntryValueLayout *m_pValuesEnd;     // 0x010
        PlayerInventoryEntryValueLayout *m_pCapacityEnd;   // 0x018
        PlayerInventoryEntryObjectLayout m_objects[3];     // 0x020
        std::byte m_state0128[0x8];                        // 0x128
        std::byte m_value0130[0x10];                       // 0x130
        std::byte m_value0140[0x10];                       // 0x140
        framework::C_LocalizedString m_localizedString0150; // 0x150
        std::uint8_t m_state0160;                          // 0x160
        std::byte m_padding0161[0x7];                      // 0x161

        std::size_t GetValueCount() const {
            return m_pValuesBegin != nullptr
                       ? static_cast<std::size_t>(m_pValuesEnd - m_pValuesBegin)
                       : 0;
        }

        PlayerInventoryEntryValueLayout *GetValuesBegin() {
            return m_pValuesBegin;
        }

        const PlayerInventoryEntryValueLayout *GetValuesBegin() const {
            return m_pValuesBegin;
        }

        PlayerInventoryEntryValueLayout *GetValuesEnd() { return m_pValuesEnd; }

        const PlayerInventoryEntryValueLayout *GetValuesEnd() const {
            return m_pValuesEnd;
        }

        const framework::C_LocalizedString &GetLocalizedString0150() const {
            return m_localizedString0150;
        }

        std::uint8_t GetState0160() const { return m_state0160; }
    };

    static_assert(sizeof(PlayerInventorySharedOwnerLayout) == 0x10,
                  "player inventory shared-owner size mismatch");
    static_assert(sizeof(PlayerInventoryEntryValueLayout) == 0x10,
                  "player inventory entry-value size mismatch");
    static_assert(sizeof(PlayerInventoryEntryObjectLayout) == 0x58,
                  "player inventory entry-object size mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_pValuesBegin) == 0x08,
                  "player inventory entry values offset mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_objects) == 0x20,
                  "player inventory entry objects offset mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_value0130) == 0x130,
                  "player inventory entry value 0130 offset mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_value0140) == 0x140,
                  "player inventory entry value 0140 offset mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_localizedString0150) ==
                      0x150,
                  "player inventory localized-string offset mismatch");
    static_assert(offsetof(PlayerInventoryEntryLayout, m_state0160) == 0x160,
                  "player inventory entry state offset mismatch");
    static_assert(sizeof(PlayerInventoryEntryLayout) == 0x168,
                  "player inventory entry size mismatch");

    /**
     * Player inventory owner and outfit-manager container.
     *
     * RTTI proves I_PlayerInventorySoul as its primary base. Both interface
     * and concrete tables contain exactly sixteen slots: the virtual
     * destructor followed by fifteen stripped operations. The factory
     * allocates 0x748 bytes and construction proves two shared-owner records,
     * a vector of 0x168-byte inventory records, an owner pointer at +0x50,
     * and an inline C_OutfitManager at +0x58.
     */
    class C_PlayerInventorySoul : public I_PlayerInventorySoul {
      public:
        ~C_PlayerInventorySoul() override = default;

        std::size_t GetEntryCount() const {
            return m_pEntriesBegin != nullptr
                       ? static_cast<std::size_t>(m_pEntriesEnd - m_pEntriesBegin)
                       : 0;
        }

        PlayerInventoryEntryLayout *GetEntriesBegin() { return m_pEntriesBegin; }
        const PlayerInventoryEntryLayout *GetEntriesBegin() const {
            return m_pEntriesBegin;
        }

        PlayerInventoryEntryLayout *GetEntriesEnd() { return m_pEntriesEnd; }

        const PlayerInventoryEntryLayout *GetEntriesEnd() const {
            return m_pEntriesEnd;
        }

        PlayerInventoryEntryLayout *GetEntryAt(std::size_t index) {
            return index < GetEntryCount() ? m_pEntriesBegin + index : nullptr;
        }

        const PlayerInventoryEntryLayout *GetEntryAt(std::size_t index) const {
            return index < GetEntryCount() ? m_pEntriesBegin + index : nullptr;
        }

        rpgmodule::C_InventorySoul *GetOwner() const { return m_pOwner; }

        // SDK convenience accessor; construction proves the inline boundary.
        C_OutfitManager *GetOutfitManager() {
            return reinterpret_cast<C_OutfitManager *>(m_outfitManager);
        }

        const C_OutfitManager *GetOutfitManager() const {
            return reinterpret_cast<const C_OutfitManager *>(m_outfitManager);
        }

      private:
        PlayerInventorySharedOwnerLayout m_primaryOwner;   // 0x08
        std::uint8_t m_primaryState;                       // 0x18
        std::byte m_padding0019[0x7];                      // 0x19
        PlayerInventorySharedOwnerLayout m_secondaryOwner; // 0x20
        std::uint8_t m_secondaryState;                     // 0x30
        std::byte m_padding0031[0x7];                      // 0x31
        PlayerInventoryEntryLayout *m_pEntriesBegin;       // 0x38
        PlayerInventoryEntryLayout *m_pEntriesEnd;         // 0x40
        PlayerInventoryEntryLayout *m_pEntriesCapacityEnd; // 0x48
        rpgmodule::C_InventorySoul *m_pOwner;               // 0x50
        alignas(C_OutfitManager) std::byte m_outfitManager[0x6F0]; // 0x58
    };

    static_assert(sizeof(C_PlayerInventorySoul) == 0x748,
                  "C_PlayerInventorySoul size mismatch");
} // namespace SDK::wh::playermodule
