#pragma once

#include "../entitymodule/c_item_indexer.h"
#include "c_qam_manager.h"
#include "../entitymodule/i_item_collection_listener.h"
#include "i_outfit_manager.h"
#include "e_outfit_id.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Exact layout of one of the player's three outfit-state blocks. */
    struct OutfitStateLayout {
        entitymodule::C_ItemIndexer *m_pItemIndexer; // 0x000, owned 0x90 allocation
        C_QAMManager m_quickAccessManagers[2];       // 0x008
        void *m_ptrs0208[3];                         // 0x208; sub_180C480B0 zeroes all three

        C_QAMManager *GetQuickAccessManager(std::uint32_t index) {
            return index < 2 ? &m_quickAccessManagers[index] : nullptr;
        }

        const C_QAMManager *GetQuickAccessManager(std::uint32_t index) const {
            return index < 2 ? &m_quickAccessManagers[index] : nullptr;
        }
    };

    static_assert(sizeof(OutfitStateLayout) == 0x220,
                  "OutfitStateLayout size mismatch");

    /**
     * Concrete player outfit manager embedded in C_PlayerInventorySoul.
     *
     * RTTI proves I_OutfitManager at +0x00 and
     * entitymodule::I_ItemCollectionListener at +0x08. Its primary and
     * secondary tables at 0x183F74570 and 0x183F74538 add no operations
     * beyond those contracts. The containing soul places this subobject at
     * +0x58; its 0x748-byte allocation proves the exact 0x6F0-byte size. The
     * constructor establishes seven 0x10-byte handle records followed by
     * three separately initialized 0x220-byte outfit-state blocks and stores
     * the owning context at +0x6E8. RTTR metadata proves E_OutfitId has the
     * byte-sized values A, B, and C; virtual slot 11 returns the active ID at
     * +0x6E0, while other operations use it to select one of those blocks.
     */
    class C_OutfitManager : public I_OutfitManager,
                            public entitymodule::I_ItemCollectionListener {
      public:
        ~C_OutfitManager() override = default;

        // SDK convenience accessors; these are not recovered virtual names.
        E_OutfitId::Type GetCurrentOutfitId() const { return m_currentOutfitId; }

        OutfitStateLayout *GetOutfitState(E_OutfitId::Type outfitId) {
            const auto index = static_cast<std::uint8_t>(outfitId);
            return index < 3 ? &m_outfitStates[index] : nullptr;
        }

        const OutfitStateLayout *GetOutfitState(E_OutfitId::Type outfitId) const {
            const auto index = static_cast<std::uint8_t>(outfitId);
            return index < 3 ? &m_outfitStates[index] : nullptr;
        }

      private:
        std::byte m_handleRecords[7][0x10]; // 0x10
        OutfitStateLayout m_outfitStates[3]; // 0x80
        E_OutfitId::Type m_currentOutfitId; // 0x6E0
        std::byte m_padding06E1[0x7]; // ctor sub_180C47E2C zeroes +0x6E0 as a word
        void *m_pOwnerContext;              // 0x6E8
    };

    static_assert(sizeof(C_OutfitManager) == 0x6F0,
                  "C_OutfitManager size mismatch");
} // namespace SDK::wh::playermodule
