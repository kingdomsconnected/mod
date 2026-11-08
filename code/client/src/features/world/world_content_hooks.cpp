#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/smart_object/smart_object_module.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_item_slot.h"
#include "shared/game/crafting_catalog.h"

#include <algorithm>

#include <cstdint>

namespace KCDC::Features::World {
    namespace {
        // Returned through unchanged: the true return type is not established
        // and no observed caller reads it, so a 64-bit pass-through is right
        // whichever it is. `bool` would truncate a wider one.
        using LoadScriptProperties_t = std::uint64_t(__fastcall *)(SDK::wh::entitymodule::C_ItemSlot *, void *);

        LoadScriptProperties_t g_loadScriptPropertiesOriginal            = nullptr;
        LoadScriptProperties_t g_loadScriptPropertiesVirtualCallOriginal = nullptr;

        /**
         * A crafting station's own item slots are scenery the minigame uses,
         * never loot. The recipe book on an alchemy stand is lifted for the
         * recipe view: a dormant one leaves the stand empty and the player
         * reading from thin air. A smithery's tongs and hammer are handed to
         * the smith (`C_BlacksmithingSlotToolAttachment`): dormant ones leave
         * the workpiece held in a bare hand and nothing to strike with. So
         * the catalog's linked slots keep spawning.
         */
        bool IsCraftStationFixture(const SDK::wh::entitymodule::C_ItemSlot *slot) {
            const SDK::IEntity *entity = slot->GetEntity();
            if (entity == nullptr) {
                return false;
            }
            const auto guid    = entity->GetGuid();
            const auto matches = [guid](const KCDC::Shared::Game::CraftStationSlot &fixture) {
                return fixture.slotGuid == guid;
            };
            // A station a script or a world resource placed lists its own.
            return std::any_of(KCDC::Shared::Game::g_CraftStationBooks.begin(), KCDC::Shared::Game::g_CraftStationBooks.end(), matches) || std::any_of(KCDC::Shared::Game::g_CraftStationTools.begin(), KCDC::Shared::Game::g_CraftStationTools.end(), matches)
                   || KCDC::Features::SmartObject::SmartObjectModule::Get().IsFixture(guid);
        }

        /**
         * Leaves every level-placed slot dormant, so the map generates no
         * items of its own. The item half of `world_population_hooks.cpp`.
         *
         * The slot learns what it is here and has not populated yet, so this
         * is the last moment the spawn bits still decide anything.
         *
         * The placement is deliberately left alive rather than refused through
         * `PopulationSink`: level XML links other entities to slots by name --
         * `placeItemSlot`, `item_slot_pile[...]`, `spot`, `seat` -- and
         * refusing the entity would take those links with it.
         */
        std::uint64_t __fastcall LoadScriptProperties_Hook(SDK::wh::entitymodule::C_ItemSlot *slot, void *properties) {
            const std::uint64_t result = g_loadScriptPropertiesOriginal(slot, properties);
            if (slot != nullptr && !IsCraftStationFixture(slot)) {
                slot->SuppressSpawning();
            }
            return result;
        }

        /** The sibling overload at the adjacent vtable slot; see patterns.h. */
        std::uint64_t __fastcall LoadScriptPropertiesVirtualCall_Hook(SDK::wh::entitymodule::C_ItemSlot *slot, void *properties) {
            const std::uint64_t result = g_loadScriptPropertiesVirtualCallOriginal(slot, properties);
            if (slot != nullptr && !IsCraftStationFixture(slot)) {
                slot->SuppressSpawning();
            }
            return result;
        }

        using SoulScheduledUpdate_t = void(__fastcall *)(void *, int);
        using SoulFullyRestock_t    = void(__fastcall *)(void *, bool, bool);

        SoulScheduledUpdate_t g_soulScheduledUpdateOriginal = nullptr;
        SoulFullyRestock_t g_soulFullyRestockOriginal       = nullptr;

        /**
         * Keeps souls from restocking their own inventories, the soul half of
         * the stash refusal in `stash_hooks.cpp`.
         *
         * A soul's stock is rolled from its presets, so it is contents two
         * clients cannot agree on, and the restock trusts the roll never to
         * change: every item made from a preset must find its entry in today's
         * list, or it copies past the end of it. Level horses rolled their tack
         * under inputs that are no longer the ones in force, so the first
         * `/world hour` -- or the first scheduled tick after a join -- faulted
         * inside it. What an NPC carries is not replicated; restocking it is
         * the singleplayer economy the mod strips.
         */
        void __fastcall SoulScheduledUpdate_Hook(void *, int) {}

        /** The forced restock the xgen `FullyRestock` reaches, same list. */
        void __fastcall SoulFullyRestock_Hook(void *, bool, bool) {}
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemSlot__LoadScriptProperties), reinterpret_cast<LPVOID>(&KCDC::Features::World::LoadScriptProperties_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_loadScriptPropertiesOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemSlot__LoadScriptPropertiesVirtualCall), reinterpret_cast<LPVOID>(&KCDC::Features::World::LoadScriptPropertiesVirtualCall_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_loadScriptPropertiesVirtualCallOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_InventorySoul__ScheduledUpdate), reinterpret_cast<LPVOID>(&KCDC::Features::World::SoulScheduledUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_soulScheduledUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_InventorySoul__FullyRestock), reinterpret_cast<LPVOID>(&KCDC::Features::World::SoulFullyRestock_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_soulFullyRestockOriginal));
    },
    "WorldContent");
