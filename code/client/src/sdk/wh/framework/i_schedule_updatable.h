#pragma once

namespace SDK::wh::framework {
    /** One-slot scheduling callback mixed into RPG runtime components. */
    class I_ScheduleUpdatable {
      public:
        virtual void unk_fn0000() = 0;

      protected:
        ~I_ScheduleUpdatable() = default;
    };

    static_assert(sizeof(I_ScheduleUpdatable) == 0x8,
                  "I_ScheduleUpdatable size mismatch");
} // namespace SDK::wh::framework
