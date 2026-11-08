#pragma once

#include "features/vendor/vendor_service.h"

#include <cstdint>
#include <v8.h>
#include <vector>

namespace KCDC::Scripting {
    /**
     * The global `Vendor`: price lists and purses the server owns, traded on
     * the game's own shop screen.
     *
     * A vendor is attached to nothing, for the same reason a conversation is
     * not: where the counter stands and who is behind it belong to the
     * gamemode, which opens the screen from whatever it decides is the
     * counter -- an NPC's use key, a dialogue option, a command.
     */
    void RegisterVendorBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What this feature tells loaded resources happened at a vendor.
    namespace Events {
        /** A deal settled. `balance` is what the player came out with in money: positive when they were paid. */
        void VendorTrade(std::uint32_t vendorId, std::uint64_t playerNetworkId, const std::vector<Features::Vendor::TradeLine> &bought, const std::vector<Features::Vendor::TradeLine> &sold, std::int64_t balance);

        /** A trading session ended, whoever ended it. */
        void VendorClosed(std::uint32_t vendorId, std::uint64_t playerNetworkId, std::uint8_t reason);
    } // namespace Events
} // namespace KCDC::Scripting
