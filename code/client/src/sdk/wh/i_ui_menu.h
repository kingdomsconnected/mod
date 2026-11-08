#pragma once

#include <cstdint>

namespace SDK::wh {
    /**
     * The menu state machine. `C_UIMenu` implements it on a sub-object at
     * +0x58 (vtable 0x183EF5958); the game finds it by rttr-casting the GUI
     * module's pages (0x180ED4918) and there is only ever one.
     *
     * The state is stored in `C_UIMenu::m_context`. `OnPictureHided` ends a
     * death by entering state 4.
     */
    class I_UIMenu {
      public:
        virtual ~I_UIMenu() = default; // slot 0

        /** 0x180C04B38. Front end 1 and 4, in-game 2 and 3, photo mode 5. */
        virtual void EnterState(std::uint8_t state) = 0; // slot 1

        /**
         * 0x180C04780. A no-op unless a state is up, and not a full inverse:
         * it leaves `E_UIState::Menu` blocked and the `menu` action map
         * enabled, because the front end is only ever left by a level load.
         */
        virtual void LeaveState() = 0; // slot 2

        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;

        /** Zero when no state is up. */
        virtual std::uint8_t GetState() const = 0; // slot 9
    };

    static_assert(sizeof(I_UIMenu) == 0x8, "I_UIMenu size mismatch");
} // namespace SDK::wh
