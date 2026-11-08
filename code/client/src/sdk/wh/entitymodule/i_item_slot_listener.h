#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    class C_ItemSlot;

    /**
     * Item-slot lifecycle listener.
     *
     * The complete native table has three default no-op callbacks followed
     * by the deleting destructor. Concrete C_ItemSlotTrigger overrides prove
     * that every callback receives one C_ItemSlot pointer; their stripped
     * source names remain opaque.
     */
    class I_ItemSlotListener {
      public:
        virtual void unk_fn0000(C_ItemSlot *) {}
        virtual void unk_fn0001(C_ItemSlot *) {}
        virtual void unk_fn0002(C_ItemSlot *) {}
        virtual ~I_ItemSlotListener() = default;
    };

    static_assert(sizeof(I_ItemSlotListener) == 0x8,
                  "I_ItemSlotListener size mismatch");
} // namespace SDK::wh::entitymodule
