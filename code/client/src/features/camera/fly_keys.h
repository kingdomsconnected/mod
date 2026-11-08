#pragma once

#include "anchor_camera.h"

#include <input/input.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Camera {
    /** A direction a free camera is flown in, or its fast modifier. */
    enum class FlyKey : std::uint8_t {
        Forward,
        Back,
        Left,
        Right,
        Up,
        Down,
        Boost,
        Count,
    };

    /**
     * Fly keys read from the game's own photo-mode bindings (the
     * `photomode_camera` action map), so a rebind in the controls menu
     * applies here too.
     *
     * CryEngine names keys by position (its "w" is scancode 0x11, which AZERTY
     * labels Z), and the mod reads those same positions in the US virtual-key
     * namespace.
     * The active layout only affects the displayed label, never the binding.
     * Re-read once a second. Game thread only.
     */
    class FlyKeyBindings final {
      public:
        static FlyKeyBindings &Get();

        /** Fills the movement fields of `fly`. Alt slows, having no game binding. */
        void Read(const Framework::Input::IInput &input, FlyInput &fly);

        /** The first key bound, as the layout labels it ("Z", "Space"). Cached; asks the engine nothing. */
        std::string Describe(FlyKey key) const;

      private:
        struct Key {
            bool mouse = false;
            int code   = 0; // a virtual key, or a mouse button index
        };

        void RefreshIfDue();

        /** False while the photo-mode map does not exist yet. */
        bool ReadFromGame();

        void UseDefaults();

        bool IsHeld(const Framework::Input::IInput &input, FlyKey key) const;

        std::array<std::vector<Key>, static_cast<std::size_t>(FlyKey::Count)> _keys;
        std::chrono::steady_clock::time_point _nextRefresh {};
        bool _fromGame = false;
    };
} // namespace KCDC::Features::Camera
