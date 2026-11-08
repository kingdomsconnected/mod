#pragma once

#include <cstdint>
#include <vector>

namespace SDK::wh::entitymodule {
    /**
     * Callback-only humanoid hand-helper contract.
     *
     * C_HumanHandHelper's secondary table at 0x183A3B2B8 contains exactly
     * sixteen operations and no virtual destructor. Their stripped parameter
     * contracts remain opaque.
     */
    class I_HumanHandHelper {
      public:
        virtual void unk_fn0000()                                                                                                         = 0;
        virtual void unk_fn0001()                                                                                                         = 0;
        virtual void unk_fn0002()                                                                                                         = 0;
        virtual std::uint64_t AddItemToHand(const std::uint64_t *itemWuid, std::uint32_t hand, const std::vector<std::uint8_t> &contexts) = 0;
        virtual void unk_fn0004()                                                                                                         = 0;
        virtual void unk_fn0005()                                                                                                         = 0;
        virtual void unk_fn0006()                                                                                                         = 0;
        virtual void unk_fn0007()                                                                                                         = 0;
        virtual bool ContainsItemInHand(std::uint32_t hand, const std::uint64_t *itemWuid)                                                = 0;
        virtual void unk_fn0009()                                                                                                         = 0;
        virtual void unk_fn0010()                                                                                                         = 0;
        virtual void unk_fn0011()                                                                                                         = 0;
        virtual std::uint64_t RemoveItemFromHands(std::uint8_t handMask, const std::uint64_t *itemWuid)                                   = 0;
        virtual void unk_fn0013()                                                                                                         = 0;
        virtual void unk_fn0014()                                                                                                         = 0;
        virtual void unk_fn0015()                                                                                                         = 0; // +0x78
    };

    static_assert(sizeof(I_HumanHandHelper) == 0x8, "I_HumanHandHelper size mismatch");
} // namespace SDK::wh::entitymodule
