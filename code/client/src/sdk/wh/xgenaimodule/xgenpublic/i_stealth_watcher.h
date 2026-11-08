#pragma once

namespace SDK::wh::xgenaimodule::XGenPublic {
    /** Primary interface proven by C_StealthWatcher's RTTI and three slots. */
    class I_StealthWatcher {
      public:
        virtual ~I_StealthWatcher()                     = default;
        virtual void unk_fn0001()                       = 0;
        virtual bool IsLocalPlayerInPrivateArea() const = 0;
    };
    static_assert(sizeof(I_StealthWatcher) == 0x8, "I_StealthWatcher size mismatch");
} // namespace SDK::wh::xgenaimodule::XGenPublic
