#pragma once

#include "c_poi.h"
#include "c_rpg_location.h"
#include "s_location_id.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * Every location and point of interest of the level, and their discovery.
     * C_RPGModule +0xB8; constructor 0x180E0F960, primary vtable 0x183AB6400
     * (two more bases at +0x08 and +0x10 carry their own).
     *
     * Only the named slots are proven; the rest are declared for numbering.
     */
    class C_RPGLocationManager {
      public:
        /**
         * `DiscoverLocation(const S_LocationId *, bool unused, bool positionOnly)`,
         * 0x180B5F39C: raises a location's status (0 -> 1 or 2, 1 -> 2) and fans
         * out to the location listeners. Referenced only from this vtable, so
         * the slot sees every location discovery.
         */
        static constexpr std::size_t kDiscoverLocationSlot = 32;

        virtual void Opaque00()            = 0;
        virtual void Opaque01()            = 0;
        virtual bool IsDiscoveryDisabled() = 0; // +0x10
        virtual void Opaque03()            = 0;
        virtual void Opaque04()            = 0;
        virtual void Opaque05()            = 0;
        virtual void Opaque06()            = 0;
        virtual void Opaque07()            = 0;
        virtual void Opaque08()            = 0;
        virtual void Opaque09()            = 0;

        /** 0x180468278. The location's discovery base (+0x10 of its C_RPGLocation), borrowed, or null. */
        virtual I_RPGLocationDiscovery *FindLocationById(const S_LocationId &id) = 0; // +0x50

        virtual void Opaque11() = 0;
        virtual void Opaque12() = 0;
        virtual void Opaque13() = 0;

        /** 0x182CAD9C4. The POI's I_POI base (+0x10 of its C_POI), borrowed, or null. */
        virtual I_POI *FindPOIById(const S_LocationId &id) = 0; // +0x70
    };
} // namespace SDK::wh::rpgmodule
