#pragma once

#include <cstddef>

namespace SDK::wh::framework {
    /** Four-slot reflected-object contract used as C_Item's primary base. */
    class I_BindableObject {
      public:
        virtual ~I_BindableObject() = default;
        virtual void unk_bindable_fn0001() = 0;
        virtual void unk_bindable_fn0002() = 0;
        virtual void unk_bindable_fn0003() = 0;
    };

    static_assert(sizeof(I_BindableObject) == 0x8,
                  "I_BindableObject size mismatch");
} // namespace SDK::wh::framework
