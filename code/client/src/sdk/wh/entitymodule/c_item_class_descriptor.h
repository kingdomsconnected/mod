#pragma once

#include "../../patterns.h"

#include "c_item_property_descriptor.h"
#include "s_item_class_guid.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    /** Concrete class descriptor referenced by inventory item instances. */
    class C_ItemClassDescriptor: public C_ItemPropertyDescriptor {
      public:
        ~C_ItemClassDescriptor() override = default;
        std::vector<S_ItemWithAmount> unk_fn0001(const std::vector<C_Item *> &inputItems, bool nativeFlag) override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;

        const S_ItemClassGUID &GetClassGUID() const {
            return m_classGuid;
        }
        bool GetState0048Raw() const {
            return m_state0048;
        }

      private:
        S_ItemClassGUID m_classGuid;  // 0x38
        bool m_state0048;             // 0x48
        std::byte m_padding0049[0x7]; // 0x49
    };

    static_assert(sizeof(C_ItemClassDescriptor) == 0x50, "C_ItemClassDescriptor size mismatch");

    /**
     * Creates items in the active player's inventory and returns the native
     * item-descriptor result.
     *
     * RTTR registers the exact namespace-level `CreatePlayerReward` name,
     * parameter names, and defaults. The implementation resolves the active
     * player inventory, creates the requested items, and optionally emits the
     * native UI notification.
     */
    inline std::shared_ptr<C_ItemClassDescriptor> CreatePlayerReward(const S_ItemClassGUID &itemClass, std::uint32_t amount = 1, bool showUINotification = true, std::uint32_t quality = 0, float health = -1.0F, float condition = -1.0F) {
        std::shared_ptr<C_ItemClassDescriptor> result;
        hook::call<void>(gPatterns.CreatePlayerReward, &result, itemClass, amount, showUINotification, quality, health, condition);
        return result;
    }

    /** SDK compatibility alias for the function's former provisional name. */
    inline std::shared_ptr<C_ItemClassDescriptor> InvokeCreateItemClassDescriptorRaw(const S_ItemClassGUID &itemClass, std::uint32_t amount, bool showUINotification, std::uint32_t quality, float health, float condition) {
        return CreatePlayerReward(itemClass, amount, showUINotification, quality, health, condition);
    }
} // namespace SDK::wh::entitymodule
