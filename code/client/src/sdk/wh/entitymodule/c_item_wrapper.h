#pragma once

#include "c_item.h"
#include "c_item_holder.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    // Retains one item and releases it through the native lifetime path.
    class C_ItemWrapper: public C_ItemHolder {
      public:
        ~C_ItemWrapper() override = default;
        virtual void unk_fn0001() override;
        virtual void unk_fn0002() override;
        virtual void unk_fn0003() override;
        virtual void unk_fn0004() override;
        virtual void unk_fn0005() override;
        virtual void unk_fn0006() override;
        virtual void unk_fn0007() override;
        virtual void unk_fn0008() override;
        virtual void unk_fn0009() override;
        virtual void unk_fn0010() override;
        virtual void unk_fn0011() override;
        virtual void unk_fn0012() override;
        virtual void unk_fn0013() override;
        virtual void unk_fn0014() override;
        virtual void unk_fn0015() override;
        virtual void unk_fn0016() override;
        virtual void unk_fn0017() override;
        virtual void unk_fn0018() override;
        virtual void unk_fn0019() override;
        virtual void unk_fn0020() override;
        virtual void unk_fn0021() override;
        virtual void unk_fn0022() override;
        virtual void unk_fn0023() override;

        C_Item *GetItem() const {
            return m_pItem;
        }

        bool GetState0010Raw() const {
            return m_state0010;
        }

      private:
        C_Item *m_pItem;             // 0x08
        bool m_state0010;            // 0x10
        std::byte m_padding0011[0x7]; // 0x11, factory 0x1808D3DC4 writes only the byte at 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_ItemWrapper, m_pItem) == 0x8, "C_ItemWrapper::m_pItem offset mismatch");
            static_assert(offsetof(C_ItemWrapper, m_state0010) == 0x10, "C_ItemWrapper::m_state0010 offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemWrapper) == 0x18, "C_ItemWrapper size mismatch");
} // namespace SDK::wh::entitymodule
