#pragma once

#include "../entitymodule/i_item_collection_listener.h"
#include "c_ui_base.h"
#include "../shared/c_signal.h"
#include "c_ui_modal_dialog_amount.h"
#include "i_ui_item_context.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * Common native inventory UI. RTTI at primary table 0x183EDE8A8 proves
     * C_UIBase at 0, I_ItemCollectionListener at 0x10, and I_UIItemContext
     * at 0x18. Constructor 0x180BC8224 builds the amount dialog at 0x318;
     * deleting destructor 0x182B114F0 passes size 0x5D0 to sized delete.
     */
    class C_UIInventoryBase
        : public C_UIBase
        , public SDK::wh::entitymodule::I_ItemCollectionListener
        , public I_UIItemContext {
      public:
        ~C_UIInventoryBase() override = default;

        C_UIModalDialogAmount &GetAmountDialog() {
            return m_amountDialog;
        }
        const C_UIModalDialogAmount &GetAmountDialog() const {
            return m_amountDialog;
        }

      protected:
        // Primary slots 0..9 belong to C_UIBase. These reserve the remaining
        // proven table extent; unknown signatures must not be called.
        virtual void unk_inventory_fn0010() = 0;
        virtual void unk_inventory_fn0011() = 0;
        virtual void unk_inventory_fn0012() = 0;
        virtual void unk_inventory_fn0013() = 0;
        virtual void unk_inventory_fn0014() = 0;
        virtual void unk_inventory_fn0015() = 0;
        virtual void unk_inventory_fn0016() = 0;
        virtual void unk_inventory_fn0017() = 0;
        virtual void unk_inventory_fn0018() = 0;
        virtual void unk_inventory_fn0019() = 0;
        virtual void unk_inventory_fn0020() = 0;
        virtual void unk_inventory_fn0021() = 0;
        virtual void unk_inventory_fn0022() = 0;
        virtual void unk_inventory_fn0023() = 0;
        virtual void unk_inventory_fn0024() = 0;
        virtual void unk_inventory_fn0025() = 0;

      private:
        // Constructor 0x180BC8224 (owner, extended-info argument, flash
        // element name, list argument).
        SDK::wh::shared::C_Signal m_signal0020;      // 0x20; dispatch unk_185667208
        std::byte m_object0030[0x10];                // 0x30; sub_180C48214
        std::byte m_object0040[0x10];                // 0x40; sub_180C48214
        SDK::wh::shared::C_Signal m_signal0050;      // 0x50; dispatch unk_185665430
        std::byte m_itemList[0x1E0];                 // 0x60; flash list (sub_180BC8484)
        std::byte m_extendedInfo[0xD8];              // 0x240; C_UIExtendedInfo (sub_180BC8628)
        C_UIModalDialogAmount m_amountDialog; // 0x318
        std::byte m_questionDialog[0xE8]; // C_UIModalDialogQuestion (its sizeof is asserted in c_ui_modal_dialog_question.h), 0x400
        std::byte m_infoDialog[0xA8]; // C_UIModalDialogInfo (its sizeof is asserted in c_ui_modal_dialog_info.h), 0x4E8
        void *m_ptrs0590[2];                         // 0x590; ctor 0
        std::uint32_t m_listArgument;                // 0x5A0; the constructor's last argument, also handed to the list
        std::byte m_padding05A4[0x4];
        void *m_ptr05A8;                             // 0x5A8; ctor 0
        bool m_flag05B0;                             // 0x5B0; ctor 0
        std::byte m_padding05B1[0x7];
        void *m_ptrs05B8[3];                         // 0x5B8; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_UIInventoryBase, m_signal0020) == 0x20, "C_UIInventoryBase::m_signal0020 offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_itemList) == 0x60, "C_UIInventoryBase::m_itemList offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_extendedInfo) == 0x240, "C_UIInventoryBase::m_extendedInfo offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_amountDialog) == 0x318, "C_UIInventoryBase::m_amountDialog offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_questionDialog) == 0x400, "C_UIInventoryBase::m_questionDialog offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_infoDialog) == 0x4E8, "C_UIInventoryBase::m_infoDialog offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_listArgument) == 0x5A0, "C_UIInventoryBase::m_listArgument offset mismatch");
            static_assert(offsetof(C_UIInventoryBase, m_ptrs05B8) == 0x5B8, "C_UIInventoryBase::m_ptrs05B8 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIInventoryBase) == 0x5D0, "C_UIInventoryBase size mismatch");
} // namespace SDK::wh::guimodule
