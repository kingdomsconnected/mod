#pragma once

namespace SDK::wh {
    // RTTI-proven C_NPCManager secondary base at +0x20.
    class I_XGenAINPCManager {
      public:
        virtual void Opaque00() = 0;
    };
    static_assert(sizeof(I_XGenAINPCManager) == 0x8, "I_XGenAINPCManager size mismatch");
} // namespace SDK::wh
