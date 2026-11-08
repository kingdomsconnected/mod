#pragma once

#include "c_poi.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * C_RPGLocation's second base, at +0x10. Vtable 0x183A7B310; slot 0 is not
     * a destructor, the primary vtable carries that as its only entry.
     *
     * What `C_RPGLocationManager::FindLocationById` returns.
     */
    class I_RPGLocationDiscovery {
      public:
        /**
         * What DiscoverLocation reads before deciding whether to raise a
         * status, and what the map asks.
         *
         * Replace the slot, never the bytes: the implementation is
         * `return *(unsigned int *)(this + 0x48)`, which the linker folded
         * across fifty-three unrelated classes. A vtable slot is per-class and
         * folding cannot reach it.
         */
        static constexpr std::size_t kGetDiscoveryStatusSlot = 10;

        virtual void Opaque00() = 0;
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
        virtual void Opaque07() = 0;
        virtual void Opaque08() = 0;
        virtual void Opaque09() = 0;
        virtual E_DiscoveryStatus GetDiscoveryStatus() = 0; // +0x50

        /**
         * `mov [rcx+30h], edx`: the field slot 10 reads, written raw. Silent --
         * no listener, toast or XP; `DiscoverLocation` is what notifies.
         */
        virtual void SetDiscoveryStatus(E_DiscoveryStatus status) = 0; // +0x58
    };
} // namespace SDK::wh::rpgmodule
