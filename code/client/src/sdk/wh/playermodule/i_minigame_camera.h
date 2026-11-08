#pragma once

namespace SDK::wh::playermodule {
    /** Six-slot secondary contract at C_Minigame+8, table 0x183A6A530. */
    class I_MinigameCamera {
      public:
        virtual void unk_minigame_camera0000() = 0;
        virtual void unk_minigame_camera0001() = 0;
        virtual void unk_minigame_camera0002() = 0;
        virtual void unk_minigame_camera0003() = 0;
        virtual void unk_minigame_camera0004() = 0;
        virtual void unk_minigame_camera0005() = 0;
    };

    static_assert(sizeof(I_MinigameCamera) == 0x8);
} // namespace SDK::wh::playermodule
