#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include <core_modules.h>
#include <utils/hooking/hooking.h>

#include <cstdint>
#include <networking/network_peer.h>

#include "features/quest/quest_service.h"

#include "shared/features/quest/quest_rpc.h"

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/guimodule/c_ui_quest_log.h"
#include "sdk/wh/guimodule/s_quest_marker_entry.h"
#include "sdk/wh/questmodule/c_quest.h"
#include "sdk/wh/questmodule/c_quest_manager.h"

namespace KCDC::Features::Quest {
    namespace {
        // Both natives answer by value through a hidden return pointer, which
        // on a member is the second argument and which MSVC also requires back
        // in RAX -- so the detours return it rather than leave RAX undefined.
        using QuestManagerCollectActiveQuests_t = SDK::StdVector<SDK::wh::questmodule::C_Quest *> *(__fastcall *)(SDK::wh::questmodule::C_QuestManager *, SDK::StdVector<SDK::wh::questmodule::C_Quest *> *);
        using QuestManagerFindQuestByName_t     = SDK::wh::questmodule::C_Quest **(__fastcall *)(SDK::wh::questmodule::C_QuestManager *, SDK::wh::questmodule::C_Quest **, const SDK::CryStringT<char> *);

        using QuestGetLevelId_t = std::int32_t(__fastcall *)(const SDK::wh::questmodule::C_Quest *);

        using QuestAssignTrackingSlot_t = std::int32_t(__fastcall *)(void *, const SDK::CryStringT<char> *, SDK::wh::questmodule::E_QuestType);
        using QuestEndTracking_t        = void(__fastcall *)(SDK::wh::guimodule::C_UIQuestLog *, SDK::wh::questmodule::C_Quest *);

        QuestManagerCollectActiveQuests_t g_collectActiveQuestsOriginal = nullptr;
        QuestManagerFindQuestByName_t g_findQuestByNameOriginal         = nullptr;
        QuestGetLevelId_t g_getLevelIdOriginal                          = nullptr;
        QuestAssignTrackingSlot_t g_assignTrackingSlotOriginal          = nullptr;
        QuestEndTracking_t g_endTrackingOriginal                        = nullptr;

        using QuestLogCollectQuestMarkers_t                         = SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> *(__fastcall *)(const SDK::wh::guimodule::C_UIQuestLog *, SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> *);
        QuestLogCollectQuestMarkers_t g_collectQuestMarkersOriginal = nullptr;

        /**
         * The map's quest markers, plus a server quest's positioned objectives.
         *
         * A shipped objective's marker comes from its log naming a game
         * object; a server objective has only a position, so its entries are
         * appended here instead, after the game's own, each reading its
         * position from a stand-in the service owns. The one caller,
         * `RefreshQuestMarkers`, diffs the result into rows.
         */
        SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> *__fastcall QuestLogCollectQuestMarkers_Hook(const SDK::wh::guimodule::C_UIQuestLog *log, SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> *entries) {
            g_collectQuestMarkersOriginal(log, entries);

            if (KCDC::Core::gApplication != nullptr && entries != nullptr) {
                KCDC::Core::gApplication->GetQuestService().AppendMarkerEntries(*entries);
            }

            return entries;
        }

        /**
         * Which region the journal files a quest under.
         *
         * The game resolves the quest's parent-node handle to answer this, and
         * a quest built at runtime has no parent -- so it answers -1, which the
         * journal renders as "other region" in red. Ours are filed where the
         * player is, which is the honest answer for a quest written for this
         * session.
         */
        std::int32_t __fastcall QuestGetLevelId_Hook(const SDK::wh::questmodule::C_Quest *quest) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetQuestService().Owns(quest)) {
                return hook::call<std::int32_t>(SDK::gPatterns.GetCurrentLevelId);
            }

            return g_getLevelIdOriginal(quest);
        }

        /**
         * Following a quest, and giving it up, reported only when the game's
         * own name table came out different -- the auto-track also claims a
         * slot for a quest that is already followed.
         *
         * A name enters the table only here, from the toggle and the
         * auto-track, and leaves it only in EndTrackingQuest, from the
         * toggle, the untrack when a quest is finished and the level sweep.
         */
        std::int32_t __fastcall QuestAssignTrackingSlot_Hook(void *table, const SDK::CryStringT<char> *name, SDK::wh::questmodule::E_QuestType type) {
            const bool before       = hook::call<std::int32_t>(SDK::gPatterns.LookupQuestTrackingId, table, name) != -1;
            const std::int32_t slot = g_assignTrackingSlotOriginal(table, name, type);

            if (!before && slot != -1 && KCDC::Core::gApplication != nullptr) {
                QuestService &quests = KCDC::Core::gApplication->GetQuestService();
                quests.NoteTracking(quests.FindByName(*name), true);
            }
            return slot;
        }

        void __fastcall QuestEndTracking_Hook(SDK::wh::guimodule::C_UIQuestLog *log, SDK::wh::questmodule::C_Quest *quest) {
            if (log == nullptr || quest == nullptr) {
                g_endTrackingOriginal(log, quest);
                return;
            }

            const bool before = log->IsTracking(quest->GetName());
            g_endTrackingOriginal(log, quest);

            if (before && !log->IsTracking(quest->GetName()) && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetQuestService().NoteTracking(quest, false);
            }
        }

        /**
         * The journal's list, plus the server's quests.
         *
         * `RefreshQuestList` hands what comes back straight to the Flash array
         * builder, so appending here is the whole of "a server quest is in the
         * journal". The original runs first, so what this extends is the list
         * the game would have shown on its own.
         */
        SDK::StdVector<SDK::wh::questmodule::C_Quest *> *__fastcall QuestManagerCollectActiveQuests_Hook(SDK::wh::questmodule::C_QuestManager *manager, SDK::StdVector<SDK::wh::questmodule::C_Quest *> *collected) {
            g_collectActiveQuestsOriginal(manager, collected);

            if (KCDC::Core::gApplication != nullptr && collected != nullptr) {
                KCDC::Core::gApplication->GetQuestService().AppendTo(*collected);
            }

            return collected;
        }

        /**
         * The name lookup, extended the same way. The diary page, the
         * objective tracker and the marker refresh all come back here for the
         * node, and the game's list is searched first so a name collision
         * resolves to the shipped quest.
         *
         * Answering is not optional: `RefreshQuestDiary` hands the result to a
         * constructor with no null check, so a name that reached the journal
         * and cannot be resolved here is an access violation.
         */
        SDK::wh::questmodule::C_Quest **__fastcall QuestManagerFindQuestByName_Hook(SDK::wh::questmodule::C_QuestManager *manager, SDK::wh::questmodule::C_Quest **result, const SDK::CryStringT<char> *name) {
            g_findQuestByNameOriginal(manager, result, name);

            if (*result == nullptr && KCDC::Core::gApplication != nullptr && name != nullptr) {
                if (SDK::wh::questmodule::C_Quest *quest = KCDC::Core::gApplication->GetQuestService().FindByName(*name)) {
                    // The native answers a smart pointer, so the caller releases it.
                    quest->AddRef();
                    *result = quest;
                }
            }

            return result;
        }
    } // namespace

    void QuestService::Install() {
        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.C_QuestManager__CollectActiveQuests), &QuestManagerCollectActiveQuests_Hook, reinterpret_cast<void **>(&g_collectActiveQuestsOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.C_QuestManager__CollectActiveQuests));

        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.C_QuestManager__FindQuestByName), &QuestManagerFindQuestByName_Hook, reinterpret_cast<void **>(&g_findQuestByNameOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.C_QuestManager__FindQuestByName));

        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.C_Quest__GetLevelId), &QuestGetLevelId_Hook, reinterpret_cast<void **>(&g_getLevelIdOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.C_Quest__GetLevelId));

        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.AssignQuestTrackingSlot), &QuestAssignTrackingSlot_Hook, reinterpret_cast<void **>(&g_assignTrackingSlotOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.AssignQuestTrackingSlot));

        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.C_UIQuestLog__EndTrackingQuest), &QuestEndTracking_Hook, reinterpret_cast<void **>(&g_endTrackingOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.C_UIQuestLog__EndTrackingQuest));

        MH_CreateHook(reinterpret_cast<void *>(SDK::gPatterns.C_UIQuestLog__CollectQuestMarkers), &QuestLogCollectQuestMarkers_Hook, reinterpret_cast<void **>(&g_collectQuestMarkersOriginal));
        MH_EnableHook(reinterpret_cast<void *>(SDK::gPatterns.C_UIQuestLog__CollectQuestMarkers));

        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->RegisterRPC<Shared::RPC::QuestAnnounced>([](const Shared::RPC::QuestAnnounced &announced, MafiaNet::Packet *) {
                if (KCDC::Core::gApplication != nullptr) {
                    KCDC::Core::gApplication->GetQuestService().Receive(announced);
                }
            });

            net->RegisterRPC<Shared::RPC::QuestTrackRequest>([](const Shared::RPC::QuestTrackRequest &request, MafiaNet::Packet *) {
                if (KCDC::Core::gApplication != nullptr) {
                    KCDC::Core::gApplication->GetQuestService().Receive(request);
                }
            });
        }
    }
} // namespace KCDC::Features::Quest
