#pragma once

#include "s_pickable_item_class.h"

namespace SDK::wh::entitymodule {
    /** RTTR 0x180FC9FDC; constructor 0x180754BD8. */
    struct S_PlayerItemClass: S_PickableItemClass {
        bool IsQuestItem() const {
            return m_questItem;
        }
        const char *GetIconId() const {
            return m_iconId.m_pData;
        }

      private:
        CryStringT<char> m_iconId;
        CryStringT<char> m_uiInfo;
        CryStringT<char> m_uiName;
        bool m_questItem;
        std::byte m_opaque00a9[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_PlayerItemClass, m_questItem) == 0xA8);
            static_assert(offsetof(S_PlayerItemClass, m_iconId) == 0x90);
            static_assert(offsetof(S_PlayerItemClass, m_uiInfo) == 0x98);
            static_assert(offsetof(S_PlayerItemClass, m_uiName) == 0xA0);
        }
    };
    static_assert(sizeof(S_PlayerItemClass) == 0xB0);
} // namespace SDK::wh::entitymodule
