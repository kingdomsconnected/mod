#pragma once

#include "../../types.h"

namespace SDK::wh::playermodule {
    /** Game-facing action-hint presentation interface. */
    class I_UIActionHintManager {
      public:
        virtual void Opaque00() = 0;
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0; // GetButtonId(CryStringT map, CryStringT action)

        /**
         * Slot 4 (0x181519B88): the label key the game shows for a CryInput
         * key name (`"q"`, `"tab"`, `"mwheel_up"`). A letter or digit gives
         * `@ui_control_uc_<keyId>`, which the string table registers per
         * keyboard layout from the key's character -- so the key CryInput
         * calls `q` reads `A` on AZERTY -- and any other key gives
         * `@ui_key_<name>`. A name CryInput does not know comes back as it
         * went in. Resolve the result with `ILocalizationManager::LocalizeLabel`.
         * MSVC passes the returned string through the hidden slot.
         */
        virtual CryStringT<char> *GetKeyLabel(CryStringT<char> *result, const char *inputName) = 0;

        virtual ~I_UIActionHintManager() = default;
    };

    static_assert(sizeof(I_UIActionHintManager) == 0x8, "I_UIActionHintManager size mismatch");
} // namespace SDK::wh::playermodule
