#pragma once

namespace SDK {
    // Native base vtable: 0x183F0E9F8. Slots 1-3 remain unidentified.
    struct ITimeOfDayUpdateCallback {
        virtual ~ITimeOfDayUpdateCallback()     = default;
        virtual void unk_fn0001()               = 0;
        virtual void unk_fn0002()               = 0;
        virtual void unk_fn0003()               = 0;
        virtual bool Update(float frameSeconds) = 0; // CTimeOfDay::Tick +0x20
    };

    static_assert(sizeof(ITimeOfDayUpdateCallback) == 0x8, "ITimeOfDayUpdateCallback size mismatch");
} // namespace SDK
