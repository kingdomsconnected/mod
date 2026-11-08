#pragma once

#include "../../patterns.h"

#include "../framework/c_localized_string.h"
#include "c_item_holder.h"
#include "c_item_indexer.h"
#include "i_item_descriptor.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace SDK::wh::entitymodule {
    /** RTTR-proven delivery-mode type; the enumerator labels are stripped. */
    namespace E_ItemDeliveryMode {
        enum class Type : std::int32_t;
    }

    /**
     * Native batched item-delivery state.
     *
     * Standalone RTTI proves no base classes and exactly four virtual slots.
     * Allocation and construction prove the 0x110-byte extent, descriptor and
     * filter vectors, delivery inventory, inline item indexer, delivery mode,
     * labels, and the two reflected policy flags.
     */
    class C_ItemDelivery {
      public:
        virtual ~C_ItemDelivery() = default;
        virtual void unk_fn0001();
        virtual void unk_fn0002();
        virtual void unk_fn0003();

        const std::vector<std::shared_ptr<I_ItemDescriptor>> &GetDeliveryDescriptors() const {
            return m_deliveryDescriptors;
        }

        C_ItemHolder *GetDeliveryInventory() const {
            return m_pDeliveryInventory;
        }

        const std::vector<std::shared_ptr<I_ItemDescriptor>> &GetPlayerInventoryFilters() const {
            return m_playerInventoryFilters;
        }

        C_ItemIndexer *GetDeliveredItemIndexer() {
            return &m_itemIndexer;
        }
        const C_ItemIndexer *GetDeliveredItemIndexer() const {
            return &m_itemIndexer;
        }

        std::uint8_t GetDeliveryModeRaw() const {
            return m_deliveryMode;
        }
        const std::vector<framework::C_LocalizedString> &GetLabels() const {
            return m_labels;
        }
        bool GetAmountsAsGroschen() const {
            return m_amountsAsGroschen;
        }
        bool GetLegalize() const {
            return m_legalize;
        }

      private:
        std::byte m_state0008[0x10];                                             // 0x008
        std::vector<std::shared_ptr<I_ItemDescriptor>> m_deliveryDescriptors;    // 0x018
        C_ItemHolder *m_pDeliveryInventory;                                      // 0x030
        std::vector<std::shared_ptr<I_ItemDescriptor>> m_playerInventoryFilters; // 0x038
        std::uint8_t m_state0050;                                                // 0x050
        std::uint8_t m_state0051;                                                // 0x051
        std::byte m_padding0052[0x6];                                            // 0x052
        C_ItemIndexer m_itemIndexer;                                             // 0x058
        std::uint16_t m_state00E8;                                               // 0x0E8
        std::uint8_t m_deliveryMode;                                             // 0x0EA
        std::byte m_padding00EB[0x5];                                            // 0x0EB
        std::vector<framework::C_LocalizedString> m_labels;                      // 0x0F0
        bool m_amountsAsGroschen;                                                // 0x108
        bool m_legalize;                                                         // 0x109
        std::byte m_padding010A[0x6];                                            // 0x10A
    };

    static_assert(sizeof(C_ItemDelivery) == 0x110, "C_ItemDelivery size mismatch");

    /** Invokes the exact reflected namespace-level item-delivery factory. */
    inline std::shared_ptr<C_ItemDelivery> CreateItemDelivery(const std::vector<std::shared_ptr<I_ItemDescriptor>> &deliveryDescriptors, C_ItemHolder *deliveryInventory, const std::vector<std::shared_ptr<I_ItemDescriptor>> &playerInventoryFilters,
        E_ItemDeliveryMode::Type deliveryMode, std::vector<framework::C_LocalizedString> labels, bool amountsAsGroschen, bool legalize) {
        std::shared_ptr<C_ItemDelivery> result;
        hook::call<void>(gPatterns.CreateItemDelivery, &result, deliveryDescriptors, deliveryInventory, playerInventoryFilters, deliveryMode, std::move(labels), amountsAsGroschen, legalize);
        return result;
    }
} // namespace SDK::wh::entitymodule
