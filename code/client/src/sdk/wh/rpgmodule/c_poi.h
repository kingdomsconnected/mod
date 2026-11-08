#pragma once

#include "s_location_id.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /** Discovery status as the game stores it. Raised, never lowered. */
    enum class E_DiscoveryStatus : std::uint32_t {
        Undiscovered      = 0,
        KnownUndiscovered = 1, // position shown, not counted as visited
        Discovered        = 2,
    };

    /**
     * C_POI's second base, at +0x10. Vtable 0x183A64998; slot 0 is not a
     * destructor, the primary vtable carries that as its only entry.
     */
    class I_POI {
      public:
        /**
         * IsDiscovered/IsUndiscovered use this slot; DiscoverPOIObject reads
         * stored status directly. The getter body is shared with unrelated
         * classes through COMDAT folding, so override only the vtable slot.
         */
        static constexpr std::size_t kGetDiscoveryStatusSlot = 8;

        // Slot 0 returns the constructor's POI ID by reference. Its body is
        // folded with unrelated getters; call the slot, never detour the body.
        virtual const S_LocationId &GetId() const = 0; // +0x00
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
        virtual void Opaque07() = 0;
        virtual E_DiscoveryStatus GetDiscoveryStatus() = 0; // +0x40
        virtual bool SetDiscoveryStatusAndNotify(std::uint32_t status, bool notify) = 0; // +0x48
    };
} // namespace SDK::wh::rpgmodule
