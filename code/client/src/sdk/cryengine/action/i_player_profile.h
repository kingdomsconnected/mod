#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    /**
     * One player's profile: the `Attributes` block of
     * `%USER%/Profiles/<name>/attributes.xml`, where every option the game's
     * settings menu writes lives -- `sound_master`, `fov`, the serialized
     * `keybinds_settings`.
     *
     * The mod never dispatches through this interface itself. Attributes are
     * `TFlowInputData` variants, and building or destroying one outside the
     * game would put a variant the game frees on the mod's heap. Instead the
     * accessors below go through the game's own typed wrappers, which build
     * the variant, call `SetAttribute` (+0x38) or `GetAttribute` (+0x50) and
     * convert, all inside WHGame.dll. They are what every settings listener
     * in the game uses.
     *
     * RTTI spells it `.?AUIPlayerProfile@@`. Game thread only.
     */
    struct IPlayerProfile {
        IPlayerProfile()                                  = delete;
        IPlayerProfile(const IPlayerProfile &)            = delete;
        IPlayerProfile &operator=(const IPlayerProfile &) = delete;

        /** False when the attribute is absent, leaving `value` untouched. */
        bool GetFloatAttribute(const char *name, float &value) {
            return hook::call<bool>(gPatterns.PlayerProfile__GetFloatAttribute, this, name, &value);
        }

        bool GetIntAttribute(const char *name, int &value) {
            return hook::call<bool>(gPatterns.PlayerProfile__GetIntAttribute, this, name, &value);
        }

        /** `value` is assigned through the engine's operator=; its owner releases it. */
        bool GetStringAttribute(const char *name, ScopedCryString &value) {
            return hook::call<bool>(gPatterns.PlayerProfile__GetStringAttribute, this, name, value.Out());
        }

        bool SetFloatAttribute(const char *name, float value) {
            return hook::call<bool>(gPatterns.PlayerProfile__SetFloatAttribute, this, name, &value);
        }

        bool SetIntAttribute(const char *name, int value) {
            return hook::call<bool>(gPatterns.PlayerProfile__SetIntAttribute, this, name, &value);
        }

        bool SetStringAttribute(const char *name, const char *value) {
            const ScopedCryString text(value);
            return hook::call<bool>(gPatterns.PlayerProfile__SetStringAttribute, this, name, &text.Get());
        }
    };
} // namespace SDK
