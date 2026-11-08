#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "book_service.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_document_item.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/playermodule/c_reading.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/i_rpg_minigames.h"

namespace KCDC::Features::Book {
    namespace {
        using PushContentToUI_t     = void(__fastcall *)(SDK::wh::entitymodule::C_DocumentItem *);
        using ReadingFinish_t       = void(__fastcall *)(SDK::wh::playermodule::C_Reading *);
        using ReturnToInventory_t   = void(__fastcall *)(SDK::wh::playermodule::C_Reading *);
        using ApplyReadingStarted_t = void(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::rpgmodule::I_Soul *, SDK::wh::entitymodule::C_Item *);

        PushContentToUI_t g_pushContentToUIOriginal         = nullptr;
        ReadingFinish_t g_readingFinishOriginal             = nullptr;
        ReturnToInventory_t g_returnToInventoryOriginal     = nullptr;
        ApplyReadingStarted_t g_applyReadingStartedOriginal = nullptr;

        /**
         * The one point every readable document hands its content to the
         * book screen. A carrier gets the open book's pages instead of the
         * vanilla text its class carries.
         */
        void __fastcall PushContentToUI_Hook(SDK::wh::entitymodule::C_DocumentItem *document) {
            if (BookService::Get().PushContent(document)) {
                return;
            }
            g_pushContentToUIOriginal(document);
        }

        /** Every end of a reading session -- the player's exit, a script, the item vanishing -- passes here. */
        void __fastcall ReadingFinish_Hook(SDK::wh::playermodule::C_Reading *reading) {
            g_readingFinishOriginal(reading);
            BookService::Get().ReadingFinished(reading);
        }

        /**
         * After a spawned book closes through minigame_exit, the game reopens
         * the APSE inventory on it -- right for a book read from the
         * inventory, wrong for a carrier the player never had there.
         */
        void __fastcall ReturnToInventory_Hook(SDK::wh::playermodule::C_Reading *reading) {
            if (BookService::Get().IsReadingCarrier(reading)) {
                return;
            }
            g_returnToInventoryOriginal(reading);
        }

        /**
         * First-read XP and the read marker, keyed by the item's class. A
         * carrier is a vanilla class, so letting this run would hand out XP
         * for a script's book and mark the real one as read.
         */
        void __fastcall ApplyReadingStarted_Hook(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::rpgmodule::I_Soul *soul, SDK::wh::entitymodule::C_Item *item) {
            if (BookService::Get().IsCarrier(item)) {
                return;
            }
            g_applyReadingStartedOriginal(minigames, soul, item);
        }
    } // namespace
} // namespace KCDC::Features::Book

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DocumentItem__PushContentToUI), reinterpret_cast<LPVOID>(&KCDC::Features::Book::PushContentToUI_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Book::g_pushContentToUIOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Reading__Finish), reinterpret_cast<LPVOID>(&KCDC::Features::Book::ReadingFinish_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Book::g_readingFinishOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Reading__ReturnToInventory), reinterpret_cast<LPVOID>(&KCDC::Features::Book::ReturnToInventory_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Book::g_returnToInventoryOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__ApplyReadingStarted), reinterpret_cast<LPVOID>(&KCDC::Features::Book::ApplyReadingStarted_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Book::g_applyReadingStartedOriginal));
    },
    "Book");
