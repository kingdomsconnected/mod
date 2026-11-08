#pragma once

#include "sdk/wh/playermodule/c_keybinds.h"

#include <string>

namespace NativeKeybindTests {
    class Keybinds final: public SDK::wh::playermodule::C_Keybinds {
      public:
        std::string current = "h";
        std::string saved   = "v";
        std::string applied;
        bool savedOffline       = false;
        unsigned int saveReason = 0;
        unsigned int loadReason = 0;

        void Opaque01() override {}
        void Opaque02() override {}
        void Opaque03() override {}
        void Opaque04() override {}

        void SaveToProfile(SDK::IPlayerProfile *, bool online, unsigned int reason) override {
            saved        = current;
            savedOffline = !online;
            saveReason   = reason;
        }

        void LoadFromProfile(SDK::IPlayerProfile *, bool, unsigned int reason) override {
            current    = saved;
            applied    = current;
            loadReason = reason;
        }
    };
} // namespace NativeKeybindTests

MODULE(native_keybind_replay, {
    IT("matches the game's input and profile listener base offsets", {
        NativeKeybindTests::Keybinds keybinds;
        const auto *object = reinterpret_cast<const std::byte *>(&keybinds);
        EQUALS(reinterpret_cast<const std::byte *>(static_cast<SDK::IInputEventListener *>(&keybinds)) - object, std::ptrdiff_t(0));
        EQUALS(reinterpret_cast<const std::byte *>(static_cast<SDK::IPlayerProfileListener *>(&keybinds)) - object, std::ptrdiff_t(0x8));
    });

    IT("reapplies the current default instead of rolling back to a saved V binding", {
        NativeKeybindTests::Keybinds keybinds;
        keybinds.ReapplyBindings(nullptr);
        EQUALS(keybinds.applied, std::string("h"));
        EQUALS(keybinds.savedOffline, true);
        EQUALS(keybinds.saveReason, static_cast<unsigned int>(SDK::IPlayerProfileManager::ePR_Options));
        EQUALS(keybinds.loadReason, static_cast<unsigned int>(SDK::IPlayerProfileManager::ePR_Options));
    });

    IT("preserves an unsaved custom key when an action map arrives late", {
        NativeKeybindTests::Keybinds keybinds;
        keybinds.current = "mouse4";
        keybinds.ReapplyBindings(nullptr);
        EQUALS(keybinds.applied, std::string("mouse4"));
    });

    IT("keeps a saved V preference after the game's initial profile load", {
        NativeKeybindTests::Keybinds keybinds;
        keybinds.LoadFromProfile(nullptr, false, SDK::IPlayerProfileManager::ePR_Options);
        keybinds.ReapplyBindings(nullptr);
        EQUALS(keybinds.applied, std::string("v"));
    });

    IT("preserves an explicitly unbound action during a late replay", {
        NativeKeybindTests::Keybinds keybinds;
        keybinds.current.clear();
        keybinds.ReapplyBindings(nullptr);
        EQUALS(keybinds.applied.empty(), true);
    });
});
