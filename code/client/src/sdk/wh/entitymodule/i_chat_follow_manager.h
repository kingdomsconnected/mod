#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Player-facing chat/follow coordination contract.
     *
     * RTTI places this interface at +0x00 in C_ChatFollowManager. That
     * implementation's primary table at 0x183A80ED0 contains exactly two
     * callbacks followed by the virtual destructor before the next COL.
     */
    class I_ChatFollowManager {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual ~I_ChatFollowManager() = default; // slot 2, +0x10
    };

    static_assert(sizeof(I_ChatFollowManager) == 0x8,
                  "I_ChatFollowManager size mismatch");
} // namespace SDK::wh::entitymodule
