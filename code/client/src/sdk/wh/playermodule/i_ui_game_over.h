#pragma once

namespace SDK::wh::playermodule {
    /**
     * The death screen as the game-over sequence sees it. Implemented only by
     * `guimodule::C_UIGameOver`, on a sub-object at +0x58 (vtable
     * 0x183C33A48), which the page publishes into `C_GameOverSequence + 0x08`.
     */
    class I_UIGameOver {
      public:
        /**
         * 0x182BBB120. Enables the `game_over` action map, sets the movie
         * visible and sends it the picture.
         *
         * `picture` is the reason row's `CryStringT<char>` by reference, and
         * `completion` a `std::function<void()>` MSVC passes indirectly -- an
         * all-zero 0x40-byte buffer is a valid empty one.
         */
        virtual void ShowPicture(const void *picture, int pictureLength, void *completion) = 0; // slot 0

        /** 0x182BB9A78. Teardown: `Release`s the element and unloads the movie. */
        virtual void HidePicture() = 0; // slot 1

      protected:
        // Slot 0 is ShowPicture, so there is no destructor to dispatch to.
        ~I_UIGameOver() = default;
    };
} // namespace SDK::wh::playermodule
