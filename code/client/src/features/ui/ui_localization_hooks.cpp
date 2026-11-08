#include "features/buff/custom_buff_module.h"
#include "features/crafting/custom_recipe_module.h"
#include "features/items/custom_item_module.h"
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/book/book_service.h"
#include "features/npc/npc_service.h"
#include "ui_localization.h"

#include "core/ui/native_menu_labels.h"
#include "core/application.h"
#include "core/input/native_keybinds.h"
#include "core/states/join_deadlines.h"

#include "features/horse/horse_service.h"
#include "features/quest/quest_service.h"
#include "features/ui/ui_service.h"

#include "sdk/patterns.h"
#include "sdk/types.h"

#include "shared/network_config.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace KCDC::Features::UI {
    namespace {
        using LocalizeLabel_t = bool(__fastcall *)(void *, const char *, SDK::CryStringT<char> *, bool);
        LocalizeLabel_t g_localizeLabelOriginal = nullptr;

        constexpr char kHintPrefix[]         = "@ui_loading_hint_";
        constexpr std::size_t kHintPrefixLen = sizeof(kHintPrefix) - 1;

        // The loading screen places its bottom rule off the text field's
        // measured height, so the second line moves the rule down with it.
        const std::string kHintText = std::string(KCDC::Shared::Network::ModDisplayName) + " is a massively multiplayer mod for KCD2.\nIt was created by the MafiaHub Team";

        // The keys PublishText hands out. The ring they resolve out of belongs
        // to KCDC::Features::UI::UiService; this is only the spelling of the key.
        constexpr char kPublishedPrefix[]         = "@kcdc_";
        constexpr std::size_t kPublishedPrefixLen = sizeof(kPublishedPrefix) - 1;

        // The key carries a horse's NetworkID rather than its name, so there is
        // no table of names to keep or invalidate.
        constexpr char kHorsePrefix[]         = "@kcdc_horse_";
        constexpr std::size_t kHorsePrefixLen = sizeof(kHorsePrefix) - 1;

        // The soul stores the bare key; the UI adds '@' before this lookup.
        constexpr char kNpcPrefix[]         = "@kcdc_npc_";
        constexpr std::size_t kNpcPrefixLen = sizeof(kNpcPrefix) - 1;

        /** The NetworkID after `prefix`, or 0 when `label` does not carry one. */
        std::uint64_t NetworkIdKey(const char *label, const char *prefix, std::size_t prefixLen) {
            if (std::strncmp(label, prefix, prefixLen) != 0) {
                return 0;
            }

            std::uint64_t networkId = 0;
            const char *digits      = label + prefixLen;
            if (*digits == '\0') {
                return 0;
            }

            for (const char *character = digits; *character != '\0'; ++character) {
                if (*character < '0' || *character > '9') {
                    return 0;
                }
                networkId = networkId * 10U + static_cast<std::uint64_t>(*character - '0');
            }

            return networkId;
        }

        /** The number after `prefix`, or -1 when `label` is not that key. */
        int IndexedKey(const char *label, const char *prefix, std::size_t prefixLength) {
            if (std::strncmp(label, prefix, prefixLength) != 0) {
                return -1;
            }

            const char *digits = label + prefixLength;
            if (*digits == '\0') {
                return -1;
            }

            for (const char *character = digits; *character != '\0'; ++character) {
                if (*character < '0' || *character > '9') {
                    return -1;
                }
            }

            return std::atoi(digits);
        }

        /**
         * Resolves our own keys, and answers every loading-screen hint with our
         * own sentence.
         *
         * This is the one choke point for '@' keys in the game: Scaleform routes
         * any text field starting with '@' into CryGFxTranslator::Translate,
         * which calls LocalizeString, which splits on '@' and resolves each token
         * here -- and the native UI reaches it the same way, after prefixing '@'
         * onto each word of what it was given. Owning it is what lets the mod put
         * its own text on screen without a localization file.
         *
         * The loading hints cannot be reached any other way: the two native
         * senders pass a level id, because the hint set is a hard-coded array
         * inside LoadingScreen.gfx and Menu.gfx.
         */
        bool __fastcall LocalizeLabel_Hook(void *self, const char *label, SDK::CryStringT<char> *out, bool english) {
            if (label != nullptr && out != nullptr) {
                if (KCDC::Features::Buff::CustomBuffModule::Get().ResolveLabel(label, *out)) {
                    return true;
                }
                if (KCDC::Features::Items::CustomItemModule::Get().ResolveLabel(label, *out)) {
                    return true;
                }
                if (KCDC::Features::Crafting::CustomRecipeModule::Get().ResolveLabel(label, *out)) {
                    return true;
                }
                if (IndexedKey(label, kHintPrefix, kHintPrefixLen) >= 0) {
                    if (KCDC::Core::gApplication != nullptr) {
                        if (const Framework::Integrations::Client::AssetDownloadStatus &download = KCDC::Core::gApplication->GetAssetDownloadStatus(); download.downloading && download.bytesTotal > 0) {
                            out->Assign(KCDC::Core::States::DownloadHintText(download.bytesDownloaded, download.bytesTotal, download.bytesPerSecond, download.waitingForSlot, download.queuePosition).c_str());
                            return true;
                        }
                    }
                    out->Assign(kHintText.c_str());
                    return true;
                }

                if (const std::uint64_t horseNetworkId = NetworkIdKey(label, kHorsePrefix, kHorsePrefixLen); horseNetworkId != 0) {
                    if (const char *horseName = KCDC::Features::Horse::HorseService::ResolveName(horseNetworkId)) {
                        out->Assign(horseName);
                        return true;
                    }

                    // Gone, or un-named: let the engine answer rather than
                    // painting the key over whatever it would have said.
                    return g_localizeLabelOriginal(self, label, out, english);
                }

                // A server-owned NPC's name, wherever the game itself names an
                // actor: the focus prompt, the dialogue header, a loot window.
                // The key carries the NetworkID, so nothing has to be published
                // in advance and a rename needs no second write -- this resolves
                // the replica's current name every time the UI asks.
                if (const std::uint64_t npcNetworkId = NetworkIdKey(label, kNpcPrefix, kNpcPrefixLen); npcNetworkId != 0) {
                    if (const char *npcName = KCDC::Features::Npc::NpcService::ResolveName(npcNetworkId)) {
                        out->Assign(npcName);
                        return true;
                    }
                    return g_localizeLabelOriginal(self, label, out, english);
                }

                // The controls menu asking what the mod's own keybind rows
                // are called. Answered from a table rather than the ring
                // above: these are the same six words for the life of the
                // process, and the menu asks for them again every time it
                // redraws a row.
                if (KCDC::Core::NativeKeybinds::ResolveLabel(label, *out)) {
                    return true;
                }

                // The mod's rows on the game's settings pages: one key per text, since the
                // row natives turn plain text into a key per word.
                if (KCDC::Core::NativeMenuLabels::Get().Resolve(label, *out)) {
                    return true;
                }

                // A blip's name on the map screen. The key carries the blip's
                // registry id and answers its current label.
                if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetMapBlipService().ResolveMapLabel(label, *out)) {
                    return true;
                }

                // A server quest's title, description and objectives. The
                // key carries the quest's NetworkID, so it answers from the
                // replica and needs nothing published in advance.
                if (KCDC::Features::Quest::QuestService::ResolveLabel(label, *out)) {
                    return true;
                }

                // A page of a book a resource opened. The key carries the
                // book and page, and a closed book's keys answer blank.
                if (KCDC::Features::Book::BookService::Get().ResolveLabel(label, *out)) {
                    return true;
                }

                // An item class the server renamed. The key is the game's own
                // `ui_nm_...`, so every screen that names the item asks here.
                if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetUiService().ResolveItemName(label, *out)) {
                    return true;
                }

                const int slot = IndexedKey(label, kPublishedPrefix, kPublishedPrefixLen);
                if (slot >= 0 && slot < static_cast<int>(KCDC::Features::UI::UiService::kPublishedSlots)) {
                    // A slot nothing has published into resolves to the empty
                    // string, and so does every slot before the application
                    // exists to hold the ring at all.
                    if (KCDC::Core::gApplication != nullptr) {
                        KCDC::Core::gApplication->GetUiService().AssignPublishedText(static_cast<std::size_t>(slot), *out);
                    }
                    else {
                        out->Assign("");
                    }
                    return true;
                }
            }

            return g_localizeLabelOriginal(self, label, out, english);
        }
    } // namespace

    namespace Localization {
        std::string PublishText(std::string text) {
            // Unguarded: the only two callers are the debug overlay, which is
            // an Application member, and the client's HUD bindings, which come
            // up only inside a session. Neither can run without KCDC::Core::gApplication.
            return "kcdc_" + std::to_string(KCDC::Core::gApplication->GetUiService().PublishText(std::move(text)));
        }
    } // namespace Localization
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CLocalizedStringsManager__LocalizeLabel), reinterpret_cast<LPVOID>(&KCDC::Features::UI::LocalizeLabel_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_localizeLabelOriginal));
    },
    "Localization");
