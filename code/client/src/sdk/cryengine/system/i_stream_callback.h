#pragma once

namespace SDK {
    /**
     * Abstract engine streaming callback interface.
     *
     * RTTI identifies this global-namespace type as a class. Its table at
     * 0x183A39AC8 contains the virtual destructor followed by exactly three
     * pure callbacks. Their stripped source contracts remain opaque.
     */
    class IStreamCallback {
      public:
        virtual ~IStreamCallback() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
    };

    static_assert(sizeof(IStreamCallback) == 0x8,
                  "IStreamCallback size mismatch");
} // namespace SDK
