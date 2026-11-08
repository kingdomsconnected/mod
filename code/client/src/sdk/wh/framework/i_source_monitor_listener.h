#pragma once

namespace SDK::wh::framework {
    /**
     * Single-callback observer used by source monitors.
     *
     * RTTI identifies this namespace-qualified type as a class. The secondary
     * table used by C_MissileWeaponPlayerController at 0x183A80E20 contains
     * exactly one callback and no virtual destructor. Its stripped source
     * contract remains opaque.
     */
    class I_SourceMonitorListener {
      public:
        virtual void unk_fn0000() = 0;
    };

    static_assert(sizeof(I_SourceMonitorListener) == 0x8,
                  "I_SourceMonitorListener size mismatch");
} // namespace SDK::wh::framework
