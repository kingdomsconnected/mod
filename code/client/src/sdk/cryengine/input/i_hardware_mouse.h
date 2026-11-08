#pragma once

namespace SDK {
    /**
     * CryEngine's reference-counted cursor owner, published as
     * gEnv->pHardwareMouse (+0x118), the field CSystem::GetIHardwareMouse
     * reads. A holder takes one increment and gives it back.
     *
     * IncrementCounter runs ShowHardwareMouse, which calls
     * gEnv->pInput->ShowCursor and SetExclusiveMode(eIDT_Mouse, false), then
     * EvaluateCursorConfinement, which drops the ClipCursor the game holds
     * while playing. Neither is reachable from Win32: KCD2 reads the mouse
     * through DirectInput and clips it to the window, so ShowCursor(TRUE)
     * alone draws a pointer the game still swallows and still traps.
     *
     * The two slots are the ones CXConsole::ShowConsole (0x1808EA368) calls.
     * KCD2's table (0x183A87808) is not stock CryEngine's declaration order,
     * so the leading slots are left unnamed rather than guessed at.
     */
    class IHardwareMouse {
      public:
        virtual ~IHardwareMouse() = default;

        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void IncrementCounter() = 0; // +0x58
        virtual void DecrementCounter() = 0; // +0x60
        virtual bool IsCursorVisible() const = 0; // +0x68, native count > 0

        static IHardwareMouse *GetInstance();
    };
} // namespace SDK
