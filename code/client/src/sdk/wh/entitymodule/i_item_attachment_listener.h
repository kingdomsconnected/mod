#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Listener for item-attachment changes.
     *
     * The standalone table at 0x183A3C570 and C_HumanHandHelper's secondary
     * table at 0x183A3B228 both prove an exact three-slot contract: a virtual
     * destructor followed by two stripped callbacks.
     */
    class I_ItemAttachmentListener {
      public:
        virtual ~I_ItemAttachmentListener() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
    };

    static_assert(sizeof(I_ItemAttachmentListener) == 0x8,
                  "I_ItemAttachmentListener size mismatch");
} // namespace SDK::wh::entitymodule
