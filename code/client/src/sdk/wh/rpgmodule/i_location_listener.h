#pragma once

namespace SDK::wh::rpgmodule {
    /** Native location-state listener implemented by HUD consumers. */
    class I_LocationListener {
      public:
        virtual void LocationOpaque00() = 0;
        virtual void LocationOpaque01() = 0;
        virtual void LocationOpaque02() = 0;
        virtual void LocationOpaque03() = 0;
        virtual void LocationOpaque04() = 0;
        virtual void LocationOpaque05() = 0;
        virtual void LocationOpaque06() = 0;
        virtual void LocationOpaque07() = 0;
        virtual void LocationOpaque08() = 0;
        virtual void LocationOpaque09() = 0;
    };

    static_assert(sizeof(I_LocationListener) == 0x8,
                  "I_LocationListener size mismatch");
} // namespace SDK::wh::rpgmodule
