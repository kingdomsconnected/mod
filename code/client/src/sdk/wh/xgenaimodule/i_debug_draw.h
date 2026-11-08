#pragma once

namespace SDK::wh::xgenaimodule {
    // RTTI-proven C_NPCManager secondary base at +0x10.
    class I_DebugDraw {
      public:
        virtual void Opaque00() = 0;
    };
    static_assert(sizeof(I_DebugDraw) == 0x8, "I_DebugDraw size mismatch");
} // namespace SDK::wh::xgenaimodule
