#pragma once

#include <utils/states/state.h>

#include <chrono>
#include <cstdint>

namespace KCDC::Core::States {
    /**
     * Offline World Builder: the menu asked for a level, and the editor owns
     * the view until the player returns to the menu. No connection is made.
     *
     * Entered from MainMenu with the page already hidden and the level and
     * project already validated; leaves for MainMenu either way.
     */
    class WorldBuilderState final: public Framework::Utils::States::IState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;

      private:
        enum class Phase : std::uint8_t {
            PreparingMods, // the project's client mods are being built and mounted
            HidingMenu, // waiting for a frame drawn without the page
            Loading,    // the level load is running
            Editing,    // the editor is open over the level
        };

        /** Ends the builder and goes back to the menu, showing `error` there when one is given. */
        void Leave(Framework::Utils::States::Machine *machine, const char *error);

        Phase _phase                = Phase::HidingMenu;
        std::uint32_t _hiddenFrames = 0;
        std::chrono::steady_clock::time_point _loadStarted {};
    };
} // namespace KCDC::Core::States
