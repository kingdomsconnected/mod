// Supported buff aliases and their native modifier names.
#include "shared/game/custom_buff_modifiers.h"

namespace KCDC::Shared::Game {
    const std::vector<CustomBuffModifierName> g_CustomBuffModifiers = {
        {"charisma", "charisma"},
        {"stamina", "mst"},
        {"maxStamina", "mst"},
        {"carryCapacity", "cap"},
        {"runSpeed", "rsb"},
        {"armorRating", "arr"},
        {"bodyArmor", "bba"},
        {"hungerDrain", "dig"},
        {"energyDrain", "exh"},
        {"staminaRegeneration", "srg"},
        {"weaponAttack", "wat"},
        {"attackSpeed", "asp"},
        {"weaponDefense", "wde"},
        {"moveSpeed", "rms"},
        {"healthRegeneration", "health"},
        {"maxHealth", ""},
        {"courage", "courage"},
    };
} // namespace KCDC::Shared::Game
