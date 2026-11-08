#pragma once

#include "e_system_event.h"
#include "i_system_event_listener.h"

#include <cstdint>

namespace SDK {
    /**
     * ISystemEventDispatcher, as `CSystemEventDispatcher` (vtable 0x183A6CAB0)
     * lays it out.
     */
    class ISystemEventDispatcher {
      public:
        virtual ~ISystemEventDispatcher() = default; // +0x00

        /**
         * Adds a listener once; false for null or one already registered.
         * The native body reads neither `name` nor `unused`;
         * CSystem passes "CSystem" and true.
         */
        virtual bool RegisterListener(ISystemEventListener *listener, const char *name, bool unused) = 0; // +0x08

        virtual bool RemoveListener(ISystemEventListener *listener) = 0; // +0x10

        /** Dispatches at once on the main thread unless `forceQueue` is set. */
        virtual void OnSystemEvent(ESystemEvent event, std::uint64_t wparam, std::uint64_t lparam, bool forceQueue) = 0; // +0x18

        /** Drains the events queued for the main thread. */
        virtual void Update() = 0; // +0x20
    };

    static_assert(sizeof(ISystemEventDispatcher) == 0x8, "ISystemEventDispatcher interface size mismatch");
} // namespace SDK
