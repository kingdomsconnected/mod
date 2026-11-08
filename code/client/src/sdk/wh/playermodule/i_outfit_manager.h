#pragma once

#include "../entitymodule/c_item.h"
#include "../entitymodule/i_equipment_manager_listener.h"
#include "c_qam_manager.h"
#include "e_outfit_id.h"

namespace SDK::wh::playermodule {
    /**
     * Player outfit selection and override contract.
     *
     * RTTI proves I_EquipmentManagerListener as its primary base. The
     * C_OutfitManager primary table at 0x183F74570 contains exactly the four
     * inherited entries followed by forty outfit operations before the next
     * COL. Identifiers remain opaque where stripping removed the source names;
     * signatures below are limited to ABIs proven by the concrete table.
     */
    class I_OutfitManager: public entitymodule::I_EquipmentManagerListener {
      public:
        virtual void unk_fn0004()                                                      = 0;
        virtual void unk_fn0005(E_OutfitId::Type outfitId)                             = 0;
        virtual void unk_fn0006()                                                      = 0;
        virtual void unk_fn0007()                                                      = 0;
        virtual void unk_fn0008()                                                      = 0;
        virtual bool unk_fn0009(const void *opaqueItem)                                = 0;
        virtual bool unk_fn0010(const void *opaqueItem)                                = 0;
        virtual E_OutfitId::Type unk_fn0011()                                          = 0;
        virtual void unk_fn0012()                                                      = 0;
        virtual void unk_fn0013()                                                      = 0;
        virtual void unk_fn0014(void *opaqueState)                                     = 0;
        virtual void unk_fn0015()                                                      = 0;
        virtual C_QAMManager *unk_fn0016()                                             = 0;
        virtual C_QAMManager *unk_fn0017()                                             = 0;
        virtual C_QAMManager *GetQuickAccessManagerForItem(entitymodule::C_Item *item) = 0; // 0x180561464
        virtual C_QAMManager *unk_fn0019(E_OutfitId::Type outfitId)                    = 0;
        virtual C_QAMManager *unk_fn0020(E_OutfitId::Type outfitId)                    = 0;
        virtual void unk_fn0021()                                                      = 0;
        virtual void unk_fn0022()                                                      = 0;
        virtual void *unk_fn0023()                                                     = 0;
        virtual void unk_fn0024()                                                      = 0;
        virtual void unk_fn0025()                                                      = 0;
        virtual void unk_fn0026()                                                      = 0;
        virtual void unk_fn0027()                                                      = 0;
        virtual void unk_fn0028()                                                      = 0;
        virtual void unk_fn0029()                                                      = 0;
        virtual void unk_fn0030()                                                      = 0;
        virtual void unk_fn0031()                                                      = 0;
        virtual void unk_fn0032()                                                      = 0;
        virtual void unk_fn0033()                                                      = 0;
        virtual void unk_fn0034()                                                      = 0;
        virtual void unk_fn0035()                                                      = 0;
        virtual void unk_fn0036()                                                      = 0;
        virtual void unk_fn0037()                                                      = 0;
        virtual void unk_fn0038()                                                      = 0;
        virtual void unk_fn0039()                                                      = 0;
        virtual void unk_fn0040()                                                      = 0;
        virtual void unk_fn0041()                                                      = 0;
        virtual void unk_fn0042()                                                      = 0;
        virtual void unk_fn0043()                                                      = 0;
    };

    static_assert(sizeof(I_OutfitManager) == 0x8, "I_OutfitManager size mismatch");
} // namespace SDK::wh::playermodule
