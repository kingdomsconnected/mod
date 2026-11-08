#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "features/pickpocket/pickpocket_module.h"

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/playermodule/c_minigame.h"
#include "sdk/wh/playermodule/c_pickpocketing.h"
#include "sdk/wh/rpgmodule/c_steal_items_effect.h"

#include <cstdint>

namespace KCDC::Features::Pickpocket {
    namespace {
        using Start_t       = void(__fastcall *)(SDK::wh::playermodule::C_Pickpocketing *, SDK::EntityId, bool);
        using OpenLootUI_t  = void(__fastcall *)(SDK::wh::playermodule::C_Pickpocketing *);
        using Resolve_t     = void(__fastcall *)(SDK::wh::playermodule::C_Pickpocketing *, bool, const SDK::StackVector<SDK::wh::framework::WUID, 11> *, std::uint8_t);
        using SendMessage_t = bool(__fastcall *)(SDK::wh::playermodule::C_Minigame *, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &);
        using StealApply_t  = bool(__fastcall *)(SDK::wh::rpgmodule::C_StealItemsEffect *);
        using LootGroup_t   = std::uint8_t(__fastcall *)(const SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_Actor *);

        Start_t g_startOriginal             = nullptr;
        OpenLootUI_t g_openLootUIOriginal   = nullptr;
        Resolve_t g_resolveOriginal         = nullptr;
        SendMessage_t g_sendMessageOriginal = nullptr;
        StealApply_t g_stealApplyOriginal   = nullptr;
        LootGroup_t g_lootGroupOriginal     = nullptr;

        /** `GetPickpocketLootGroup`'s own answer for an item the grid leaves out. */
        constexpr std::uint8_t kLootExcluded = 2;

        /**
         * The charge began. Reported after the original, which is what binds
         * the victim as the interaction target the module reads it back from.
         */
        void __fastcall Start_Hook(SDK::wh::playermodule::C_Pickpocketing *self, SDK::EntityId victimEntityId, bool bagFlag) {
            g_startOriginal(self, victimEntityId, bagFlag);
            if (KCDC::Core::gApplication != nullptr) {
                PickpocketModule::Get().OnStarted(self);
            }
        }

        /**
         * The charge is full and the grid is about to be built out of the
         * victim soul's inventory. Against a remote player that inventory is
         * only right once the server's stock is in it, so until then this
         * returns without building anything -- and `UpdateCharge`, which sits
         * at full charge calling this every frame until the state moves on,
         * simply asks again next frame.
         */
        void __fastcall OpenLootUI_Hook(SDK::wh::playermodule::C_Pickpocketing *self) {
            if (KCDC::Core::gApplication == nullptr) {
                g_openLootUIOriginal(self);
                return;
            }
            PickpocketModule &module = PickpocketModule::Get();
            if (!module.MayOpenLoot(self)) {
                return;
            }
            module.BeginLootGrid(self);
            g_openLootUIOriginal(self);
            module.EndLootGrid();
        }

        /**
         * The game's own loot filter: which of the victim's items the grid may
         * show, and in which tile set. While a remote body's grid is being
         * built it leaves out everything the stock did not create -- the
         * shadow items the copy holds for its hands and its bow.
         *
         * The engine also calls it when an item is added to any human, to keep
         * the pouch and satchel flags at `C_Human+0xA08` that `CanBeRobbed`
         * reads current. Outside a grid build the module's flag is down and
         * this is the original.
         */
        std::uint8_t __fastcall LootGroup_Hook(const SDK::wh::entitymodule::C_Item *item, SDK::wh::entitymodule::C_Actor *actor) {
            if (KCDC::Core::gApplication != nullptr && PickpocketModule::Get().HidesFromLootGrid(item)) {
                return kLootExcluded;
            }
            return g_lootGroupOriginal(item, actor);
        }

        /**
         * Every ending of an attempt: the loot screen closing, the charge
         * failing its threshold or losing the victim, an interruption. Read
         * before the original runs, while every item the grid took is still
         * where it was.
         */
        void __fastcall Resolve_Hook(SDK::wh::playermodule::C_Pickpocketing *self, bool success, const SDK::StackVector<SDK::wh::framework::WUID, 11> *taken, std::uint8_t reason) {
            if (KCDC::Core::gApplication != nullptr) {
                PickpocketModule::Get().OnResolving(self, success, taken, reason);
            }
            g_resolveOriginal(self, success, taken, reason);
        }

        /**
         * A minigame telling its target what happened. Pickpocketing sends
         * `minigame:pickpocket` here, and on an NPC that is what runs
         * `switch_handlePickpocket` and so the crime self-help dialogue. A
         * remote player's body is not an NPC and must not answer for its
         * owner; false is what the native returns when there is no target.
         */
        bool __fastcall SendMessage_Hook(SDK::wh::playermodule::C_Minigame *self, const SDK::CryStringT<char> &type, const SDK::CryStringT<char> &payload) {
            if (KCDC::Core::gApplication != nullptr && PickpocketModule::Get().RefusesMessage(self)) {
                return false;
            }
            return g_sendMessageOriginal(self, type, payload);
        }

        /**
         * The single place a successful pickpocket moves items to the thief.
         * Out of a remote player's body nothing moves here: the server takes
         * from the victim and grants the thief what really came out. True is
         * what the native answers for a list it found nothing in.
         */
        bool __fastcall StealApply_Hook(SDK::wh::rpgmodule::C_StealItemsEffect *self) {
            if (KCDC::Core::gApplication != nullptr && PickpocketModule::Get().RefusesTransfer(self)) {
                return true;
            }
            return g_stealApplyOriginal(self);
        }
    } // namespace

    void PickpocketModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Pickpocketing__Start), reinterpret_cast<LPVOID>(&Start_Hook), reinterpret_cast<LPVOID *>(&g_startOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Pickpocketing__OpenLootUI), reinterpret_cast<LPVOID>(&OpenLootUI_Hook), reinterpret_cast<LPVOID *>(&g_openLootUIOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Pickpocketing__ResolvePickpocketingOutcome), reinterpret_cast<LPVOID>(&Resolve_Hook), reinterpret_cast<LPVOID *>(&g_resolveOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Minigame__SendMessageToInteractionTarget), reinterpret_cast<LPVOID>(&SendMessage_Hook), reinterpret_cast<LPVOID *>(&g_sendMessageOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StealItemsEffect__Apply), reinterpret_cast<LPVOID>(&StealApply_Hook), reinterpret_cast<LPVOID *>(&g_stealApplyOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.entitymodule__GetPickpocketLootGroup), reinterpret_cast<LPVOID>(&LootGroup_Hook), reinterpret_cast<LPVOID *>(&g_lootGroupOriginal));
    }
} // namespace KCDC::Features::Pickpocket

static InitFunction init(
    []() {
        KCDC::Features::Pickpocket::PickpocketModule::Install();
    },
    "Pickpocket");
