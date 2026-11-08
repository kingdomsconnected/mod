#pragma once

#include <cstdint>

namespace KCDC::Scripting {
    // Puts this feature's events in the client catalog the TypeScript contract is generated from. Events only: the server owns the `Vendor` global, and this side reports what the trade screen did at this machine.
    void RegisterVendorMetadata();

    // What the trade screen did on this machine. Nothing here mentions V8, so VendorModule can report without depending on the scripting layer.
    //
    // Local and unreplicated: the server's own `vendorClosed` is about the session, these are about the screen.
    namespace Events {
        /** The trade screen came up. `npcNetworkId` is 0 when nobody keeps the shop. */
        void VendorOpened(std::uint32_t sessionId, std::uint64_t npcNetworkId);

        /** The trade screen that `VendorOpened` announced went away. */
        void VendorClosed(std::uint32_t sessionId, const char *reason);
    } // namespace Events
} // namespace KCDC::Scripting
