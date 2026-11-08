#pragma once

namespace SDK::wh::framework {
    /** Listener notified when a native game-log event is ready for display. */
    class I_EventLogListener {
      public:
        virtual void EventLogOpaque00() = 0;
    };

    static_assert(sizeof(I_EventLogListener) == 0x8,
                  "I_EventLogListener size mismatch");
} // namespace SDK::wh::framework
