#pragma once

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    /** Declares the client's door events. Doors have no client handle: a handler is told about the door as this machine sees it. */
    void RegisterDoorMetadata();

    // What this feature asks resources loaded on this machine. Nothing here
    // mentions V8, so DoorService can ask without depending on the scripting
    // layer.
    namespace Events {
        /** The door a player's use is about, as this machine sees it at that moment. */
        struct DoorUse {
            /** The server's door entity, as server-side `door.id`; 0 when no replica of it has arrived. */
            std::uint64_t networkId = 0;
            std::uint64_t guid      = 0;
            std::string name;
            bool open   = false;
            bool locked = false;
        };

        /**
         * The local player is about to work a door, and nothing has moved
         * yet. False when a handler refused it; the use then does nothing.
         */
        bool DoorInteract(const DoorUse &door, const char *action, bool keySide);
    } // namespace Events
} // namespace KCDC::Scripting
