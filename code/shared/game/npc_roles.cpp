#include "npc_roles.h"

#include <algorithm>

namespace KCDC::Shared::Game {
    // Rows are (name, soul_id, entity class, clothing preset, skald_character_name). The soul ids
    // and character names are the game's own, read out of the catalog mined by
    // tools/generate_soul_catalog.py; the names on the left are this mod's.
    //
    // No clothing preset is named yet: each of these souls brings the outfit its own class table
    // dresses it in, which is the right default -- a guard looks like a guard without anyone
    // choosing. `Npc.create({ outfit })` overrides it per spawn.
    const std::vector<NpcRoleEntry> g_NpcRoles = {
        {"guard", "63daa80b-d1f4-46f5-8222-c22a541cae07", "NPC", "", "char_GENERIC_MAN_GUARD_15"},
        {"guard_alt", "2e96ee86-7c95-4b57-91f6-6d3abce75d22", "NPC", "", "char_GENERIC_MAN_GUARD_16"},
        {"knight", "fc4536b2-4964-4f79-af47-c6e7a3e5ec29", "NPC", "", "char_GENERIC_MAN_GUARD_05"},
        {"bandit", "a081e02a-6bbf-4a17-bf26-e6a31aea10af", "NPC", "", "char_GENERIC_MAN_ENEMY_BANDIT_02"},
        {"bandit_alt", "02c59661-b14a-4f3c-9239-f31de1e2be57", "NPC", "", "char_GENERIC_MAN_ENEMY_BANDIT_14"},
        {"cuman", "47e6faf8-0387-367e-4c70-0003bf03dfa8", "NPC", "", "char_GENERIC_MAN_CUMAN_04"},
        {"townsman", "51ea9c51-48b0-4f44-bedb-081f252b6ba4", "NPC", "", "char_GENERIC_MAN_COMMONER_02"},
        {"townsman_alt", "f61ce06c-93ca-4157-af2f-fc286b5ad83f", "NPC", "", "char_GENERIC_MAN_COMMONER_03"},
        {"townswoman", "f80c07aa-d033-4e94-a61e-ef4a24c10bc2", "NPC_Female", "", "char_GENERIC_WOMAN_COMMONER_10"},
        {"townswoman_alt", "41c12ac6-ba41-4ffa-87a0-69574c80b633", "NPC_Female", "", "char_GENERIC_WOMAN_COMMONER_08"},
    };

    const NpcRoleEntry *FindNpcRole(std::string_view name) {
        const auto it = std::find_if(g_NpcRoles.begin(), g_NpcRoles.end(), [&](const NpcRoleEntry &entry) {
            return entry.name == name;
        });
        return it != g_NpcRoles.end() ? &*it : nullptr;
    }
} // namespace KCDC::Shared::Game
