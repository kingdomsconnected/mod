#pragma once

#include <cstdint>

namespace KCDC::Scripting {
    // Puts this feature's events in the client catalog the TypeScript contract is generated from. A function of its own because these are events rather than anything on the `World` global, which world_query_scripting.h publishes.
    void RegisterWorldMetadata();

    // What this feature tells loaded resources happened on this machine. Nothing here mentions V8, so WorldService can report a discovery without depending on the scripting layer's classes.
    //
    // Local and unreplicated throughout: what the server decides arrives as an RPC instead.
    namespace Events {
        /**
         * A POI that became discovered; does not await script promises.
         *
         * The four arguments are the four fields of the game's own 16-byte
         * location id, spelled out rather than taken as the engine struct: a
         * binding must not name an SDK type, so WorldService does the one
         * conversion at the call site. They reach the resource as the same
         * four keys on one object.
         */
        void POIDiscovered(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]);

        /** A location -- a town, a district -- that became discovered; the id as `POIDiscovered` carries it. */
        void LocationDiscovered(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]);
    } // namespace Events
} // namespace KCDC::Scripting
