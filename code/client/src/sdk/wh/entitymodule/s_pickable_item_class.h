#pragma once

#include "s_item_class.h"

namespace SDK::wh::entitymodule {
    /** RTTR 0x180FCD148; constructor 0x180754C3C. */
    struct S_PickableItemClass: S_ItemClass {
        const char *GetModel() const {
            return m_model.m_pData;
        }
        const char *GetMaterial() const {
            return m_material.m_pData;
        }
        float GetWeight() const {
            return m_weight;
        }
        std::int32_t GetPrice() const {
            return m_price;
        }
        const char *GetEntityScript() const {
            return m_entityScript.m_pData;
        }

      private:
        CryStringT<char> m_model;
        CryStringT<char> m_material;
        CryStringT<char> m_entityScript;
        std::byte m_opaque0050[0x8];
        float m_weight;
        std::int32_t m_price;
        std::byte m_opaque0060[0x30];

        static void AssertLayout() {
            static_assert(offsetof(S_PickableItemClass, m_weight) == 0x58);
            static_assert(offsetof(S_PickableItemClass, m_price) == 0x5C);
            static_assert(offsetof(S_PickableItemClass, m_model) == 0x38);
            static_assert(offsetof(S_PickableItemClass, m_material) == 0x40);
            static_assert(offsetof(S_PickableItemClass, m_entityScript) == 0x48);
        }
    };
    static_assert(sizeof(S_PickableItemClass) == 0x90);
} // namespace SDK::wh::entitymodule
