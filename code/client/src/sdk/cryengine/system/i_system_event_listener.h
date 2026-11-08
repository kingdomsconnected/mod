#pragma once

#include "e_system_event.h"

#include <cstdint>

namespace SDK {
    /**
     * ISystemEventListener, vtable 0x183DBA488.
     *
     * KCD2 emits three slots, including an any-thread handler that most
     * listeners leave as a nullsub.
     */
    class ISystemEventListener {
      public:
        virtual ~ISystemEventListener() = default; // +0x00

        /**
         * Called from every dispatch, on whichever thread raised the event and
         * whether or not it is also queued for the main thread
         * (`CSystemEventDispatcher::NotifyListenersAnyThread` 0x180A59150).
         */
        virtual void OnSystemEventAnyThread(ESystemEvent event, std::uint64_t wparam, std::uint64_t lparam) = 0; // +0x08

        /**
         * Called on the main thread only, under the engine lock
         * (0x181DDE9E0): immediately when the event is raised there, otherwise
         * when the dispatcher's queue is drained.
         */
        virtual void OnSystemEvent(ESystemEvent event, std::uint64_t wparam, std::uint64_t lparam) = 0; // +0x10
    };

    static_assert(sizeof(ISystemEventListener) == 0x8, "ISystemEventListener size mismatch");
} // namespace SDK
