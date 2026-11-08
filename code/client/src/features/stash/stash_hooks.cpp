#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include "features/stash/stash_service.h"

#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_script_bind_entity_module.h"
#include "sdk/wh/entitymodule/c_stash.h"
#include "sdk/wh/guimodule/c_ui_inventory_base.h"

#include <cstdint>
#include <stdexcept>

namespace KCDC::Features::Stash {
    namespace {
        using InventoryTransferItems_t                            = void(__fastcall *)(SDK::wh::guimodule::C_UIInventoryBase *, SDK::wh::entitymodule::C_Inventory *, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &, std::uint32_t);
        InventoryTransferItems_t g_inventoryTransferItemsOriginal = nullptr;
        void __fastcall InventoryTransferItems_Hook(SDK::wh::guimodule::C_UIInventoryBase *ui, SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().RequestContainerTransfer(destination, items, amount)) {
                return;
            }
            g_inventoryTransferItemsOriginal(ui, destination, items, amount);
        }

        using StashOnOpened_t        = void(__fastcall *)(SDK::wh::entitymodule::C_Stash *, bool);
        using StashOnClosed_t        = void(__fastcall *)(SDK::wh::entitymodule::C_Stash *);
        using StashRestockNow_t      = void(__fastcall *)(SDK::wh::entitymodule::C_Stash *);
        using StashScheduledUpdate_t = void(__fastcall *)(void *, int);
        using StashCanUnlock_t       = bool(__fastcall *)(const SDK::wh::entitymodule::C_Stash *, const SDK::CryGUID *);

        using HasPlayerVisibleItems_t = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindEntityModule *, SDK::IFunctionHandler *, SDK::ScriptHandle);

        HasPlayerVisibleItems_t g_hasPlayerVisibleItemsOriginal = nullptr;
        StashOnOpened_t g_stashOnOpenedOriginal               = nullptr;
        StashOnClosed_t g_stashOnClosedOriginal               = nullptr;
        StashRestockNow_t g_stashRestockNowOriginal           = nullptr;
        StashScheduledUpdate_t g_stashScheduledUpdateOriginal = nullptr;
        StashCanUnlock_t g_stashCanUnlockOriginal             = nullptr;

        /**
         * The container's own Lua reported that the transfer screen opened.
         *
         * `C_ScriptBindStash::ReportOpened` is the engine's only caller, so
         * this is the native "the player is in this stash" edge, raised before
         * anything is taken.
         */
        void __fastcall StashOnOpened_Hook(SDK::wh::entitymodule::C_Stash *stash, bool byPlayer) {
            g_stashOnOpenedOriginal(stash, byPlayer);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetStashService().OnNativeOpened(stash, byPlayer);
            }
        }

        /**
         * The matching close. Read after the original so the game's own close
         * bookkeeping has already run against the inventory this reports.
         */
        void __fastcall StashOnClosed_Hook(SDK::wh::entitymodule::C_Stash *stash) {
            g_stashOnClosedOriginal(stash);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetStashService().OnNativeClosed(stash);
            }
        }

        /**
         * The engine's restock fills a container from its preset list and
         * removes every stack the list does not name. Refused for every stash
         * in the world, so the level's containers stay empty and what a player
         * finds in one is what a server script put there.
         *
         * `sInventoryPreset` is rolled rather than authored, so this is also
         * the only contents two clients could never agree on.
         */
        void __fastcall StashRestockNow_Hook(SDK::wh::entitymodule::C_Stash *) {}

        /**
         * The scheduled half of the same reconciliation, reached on the
         * I_ScheduleUpdatable subobject and not through RestockNow -- so
         * refusing that one alone is not enough.
         */
        void __fastcall StashScheduledUpdate_Hook(void *, int) {}

        /**
         * Which key opens a stash, as AnimStash.lua's `PlayerHoldsKey` asks
         * it while building the prompt. A replicated container whose key or
         * lock a server script set answers from that; every other stash keeps
         * the game's answer, dynamic home key included.
         */
        bool __fastcall StashCanUnlock_Hook(const SDK::wh::entitymodule::C_Stash *stash, const SDK::CryGUID *keyItemClass) {
            bool answer = false;
            if (KCDC::Core::gApplication != nullptr && keyItemClass != nullptr && KCDC::Core::gApplication->GetStashService().AnswerCanUnlock(stash, *keyItemClass, answer)) {
                return answer;
            }
            return g_stashCanUnlockOriginal(stash, keyItemClass);
        }

        /**
         * Whether a container's open prompt reads as empty. Only the holder is
         * shown a replicated container's rows, so for everyone else the native
         * inventory is empty; the server's `hasContents` answers instead. The
         * bind answers Lua directly, the way its own helper does.
         */
        int __fastcall HasPlayerVisibleItems_Hook(SDK::wh::entitymodule::C_ScriptBindEntityModule *scriptBind, SDK::IFunctionHandler *handler, SDK::ScriptHandle inventoryWuid) {
            bool answer = false;
            if (KCDC::Core::gApplication != nullptr && handler != nullptr && KCDC::Core::gApplication->GetStashService().AnswerHasVisibleItems(inventoryWuid, answer)) {
                return handler->EndFunctionAny(SDK::ScriptAnyValue(answer));
            }
            return g_hasPlayerVisibleItemsOriginal(scriptBind, handler, inventoryWuid);
        }

    } // namespace
} // namespace KCDC::Features::Stash

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIInventoryBase__TransferItems), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::InventoryTransferItems_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_inventoryTransferItemsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Stash__OnOpened), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::StashOnOpened_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_stashOnOpenedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Stash__OnClosed), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::StashOnClosed_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_stashOnClosedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Stash__RestockNow), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::StashRestockNow_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_stashRestockNowOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindEntityModule__HasPlayerVisibleItems), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::HasPlayerVisibleItems_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_hasPlayerVisibleItemsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Stash__ScheduledUpdate), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::StashScheduledUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_stashScheduledUpdateOriginal));

        // The key check behind a locked container's prompt. Without it a
        // server-set lock would open with the level's key, so fail loudly.
        const MH_STATUS canUnlockStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Stash__CanUnlock), reinterpret_cast<LPVOID>(&KCDC::Features::Stash::StashCanUnlock_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Stash::g_stashCanUnlockOriginal));
        if (canUnlockStatus != MH_OK) {
            throw std::runtime_error("Could not install the stash unlock check");
        }
    },
    "Stash");
