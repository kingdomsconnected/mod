#pragma once

namespace SDK {
    /**
     * Abstract entity-component event distributor, vtable 0x184073170.
     *
     * KCD2 emits exactly two entries: one pure event operation followed by
     * the virtual destructor. The event contract's exact source signature is
     * not recoverable from the binary and therefore remains opaque.
     */
    class IComponentEventDistributer {
      public:
        virtual void unk_fn0000() = 0;             // +0x00
        virtual ~IComponentEventDistributer() = default; // +0x08
    };

    static_assert(sizeof(IComponentEventDistributer) == 0x8,
                  "IComponentEventDistributer size mismatch");
} // namespace SDK
