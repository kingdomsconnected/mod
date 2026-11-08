#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** One named kind of NPC a script can ask for without knowing a GUID. */
    struct NpcRoleEntry {
        /** What a script names it: `guard`, `bandit`, `townswoman`. */
        std::string_view name;

        /** `soul_id` of the soul the body is spawned against. */
        std::string_view soulGuid;

        /** Entity class the body is spawned as: `NPC` or `NPC_Female`. */
        std::string_view actorClass;

        /** Which of the game's own outfits it wears, or empty for whatever its soul brings. */
        std::string_view clothingPresetGuid;

        /** What the soul looks like in the game's own data, for anyone matching one up. */
        std::string_view character;
    };

    /**
     * The NPC roles this build ships.
     *
     * The soul catalog itself is 8199 rows and stays a file beside the client
     * (`files/kcdc/souls.tsv`): it is a browser, and shipping it in both
     * binaries would put half a megabyte of strings in each. What a *server*
     * needs is different and much smaller -- a handful of names a gamemode can
     * spawn without holding a GUID -- so that lives here, in code, on both
     * peers, and an unknown role is simply a GUID the caller passes through.
     *
     * Every row below is a real soul from `Libs/Tables/rpg/soul*.xml`, picked
     * out of the catalog `tools/generate_soul_catalog.py` mines: the
     * `extraGuards_dummy_*` souls are the game's own spare-guard pool, and the
     * rest are generic-character souls whose `skald_character_name` names one
     * of the shipped GENERIC_MAN/WOMAN heads rather than a named character.
     */
    extern const std::vector<NpcRoleEntry> g_NpcRoles;

    /** The role with this name, or null when nothing is called that. */
    const NpcRoleEntry *FindNpcRole(std::string_view name);
} // namespace KCDC::Shared::Game
