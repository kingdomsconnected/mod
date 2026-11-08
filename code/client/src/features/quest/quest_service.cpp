#include "quest_service.h"

#include "quest.h"

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/conceptmodule/c_shared_resource.h"
#include "sdk/wh/guimodule/c_ui_compass.h"
#include "sdk/wh/guimodule/c_ui_hud_events.h"
#include "sdk/wh/guimodule/c_ui_map.h"
#include "sdk/wh/guimodule/c_ui_quest_log.h"
#include "sdk/wh/questmodule/e_quest_progress.h"
#include "sdk/wh/questmodule/e_quest_type.h"

#include "core/application.h"

#include "features/quest/quest_scripting.h"

#include <algorithm>
#include <cmath>
#include <core_modules.h>
#include <cstring>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <string>
#include <utility>
#include <utils/hooking/hooking.h>

namespace KCDC::Features::Quest {
    namespace {
        constexpr const char *kLogger = "Quest";

        /**
         * Where the mod's marker WUIDs start. The top byte is the WUID's type,
         * and this one is none of the area types the row builder draws as a
         * region, nor one the game hands an item.
         */
        constexpr std::uint64_t kMarkerWuidBase = 0x7F00000000000000ULL;

        /** The state `AddObjectiveCompassMarks` (0x180DC5F24) builds a quest mark with. */
        constexpr std::int32_t kQuestCompassMarkState = 2;

        bool SamePosition(const SDK::Vec3 &lhs, const SDK::Vec3 &rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        /** The objective's node identifier, spelled as `Quest` spells it when it builds the objective. */
        std::string ObjectiveIdentifier(const std::string &questKey, std::size_t index) {
            return questKey + "#" + std::to_string(index);
        }
    } // namespace

    namespace {
        /** Every label this feature answers starts here. `Localize` leaves a token beginning with '@' alone, so a key escapes the per-word mangling. */
        constexpr char kQuestLabelPrefix[]         = "@kcdc_quest_";
        constexpr std::size_t kQuestLabelPrefixLen = sizeof(kQuestLabelPrefix) - 1;

        /** Reads decimal digits, answering false when there are none. */
        bool ReadNumber(const char *&cursor, std::uint64_t &value) {
            if (*cursor < '0' || *cursor > '9') {
                return false;
            }

            value = 0;
            for (; *cursor >= '0' && *cursor <= '9'; ++cursor) {
                value = value * 10U + static_cast<std::uint64_t>(*cursor - '0');
            }
            return true;
        }
    } // namespace

    std::string QuestService::TitleLabel(std::uint64_t questNetworkId) {
        return std::string(kQuestLabelPrefix) + std::to_string(questNetworkId) + "_t";
    }

    std::string QuestService::DescriptionLabel(std::uint64_t questNetworkId) {
        return std::string(kQuestLabelPrefix) + std::to_string(questNetworkId) + "_d";
    }

    std::string QuestService::ObjectiveLabel(std::uint64_t questNetworkId, std::size_t index) {
        return std::string(kQuestLabelPrefix) + std::to_string(questNetworkId) + "_o" + std::to_string(index);
    }

    bool QuestService::ResolveLabel(const char *label, SDK::CryStringT<char> &out) {
        if (label == nullptr || std::strncmp(label, kQuestLabelPrefix, kQuestLabelPrefixLen) != 0) {
            return false;
        }

        const char *cursor           = label + kQuestLabelPrefixLen;
        std::uint64_t questNetworkId = 0;
        if (!ReadNumber(cursor, questNetworkId) || *cursor != '_') {
            return false;
        }
        ++cursor;

        auto *replication = Framework::CoreModules::GetReplication();
        auto *quest       = replication != nullptr ? replication->GetEntity<KCDC::Features::Quest::Quest>(questNetworkId) : nullptr;
        if (quest == nullptr) {
            // The quest has gone but the journal still holds the key: a blank
            // row beats showing the key itself.
            out.Assign("");
            return true;
        }

        if (std::strcmp(cursor, "t") == 0) {
            out.Assign(quest->title.c_str());
            return true;
        }

        if (std::strcmp(cursor, "d") == 0) {
            out.Assign(quest->description.c_str());
            return true;
        }

        if (*cursor == 'o') {
            ++cursor;
            std::uint64_t index = 0;
            if (ReadNumber(cursor, index) && *cursor == '\0' && index < Shared::Entities::QuestEntity::kMaxObjectives) {
                out.Assign(quest->objectiveText[index].c_str());
                return true;
            }
        }

        return false;
    }

    void QuestService::Receive(const Shared::RPC::QuestAnnounced &announced) {
        if (!announced.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Dropped a malformed quest announcement");
            return;
        }

        // The HUD is Flash and belongs to the game thread; this is the peer pump.
        std::scoped_lock lock(_pendingAnnouncementsMutex);
        if (_pendingAnnouncements.size() < kMaxLiveQuests) {
            _pendingAnnouncements.push_back(announced);
        }
    }

    void QuestService::Receive(const Shared::RPC::QuestTrackRequest &request) {
        if (!request.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Dropped a malformed quest track request");
            return;
        }

        std::scoped_lock lock(_pendingTracksMutex);
        if (_pendingTracks.size() < kMaxLiveQuests) {
            _pendingTracks.push_back(PendingTrack {request, std::chrono::steady_clock::now() + kTrackRequestLifetime});
        }
    }

    void QuestService::DrainTrackRequests() {
        {
            std::scoped_lock lock(_pendingTracksMutex);
            _heldTracks.insert(_heldTracks.end(), _pendingTracks.begin(), _pendingTracks.end());
            _pendingTracks.clear();
        }

        if (_heldTracks.empty()) {
            return;
        }

        SDK::wh::guimodule::C_UIQuestLog *log = SDK::wh::guimodule::C_UIQuestLog::GetInstance();
        if (log == nullptr) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto pending = _heldTracks.begin(); pending != _heldTracks.end();) {
            const SDK::ScopedCryString name(pending->request.questKey.c_str());
            SDK::wh::questmodule::C_Quest *quest = FindByName(name);

            if (quest == nullptr) {
                if (now < pending->deadline) {
                    ++pending;
                    continue;
                }

                Framework::Logging::GetLogger(kLogger)->debug("Dropped the track request for quest \"{}\": the quest never arrived", pending->request.questKey);
                pending = _heldTracks.erase(pending);
                continue;
            }

            // Through the game's own natives, as the track button does: a slot,
            // then the start. A full band leaves the quest unfollowed. The
            // detours report the change back.
            if (pending->request.tracked && !log->IsTracking(quest->GetName()) && log->AssignTrackingSlot(quest) != -1) {
                log->BeginTrackingQuest(quest);
            }
            else if (!pending->request.tracked && log->IsTracking(quest->GetName())) {
                log->EndTrackingQuest(quest);
            }

            pending = _heldTracks.erase(pending);
        }
    }

    QuestService::MarkerSource *QuestService::AcquireSource() {
        if (!_freeSources.empty()) {
            MarkerSource *source = _freeSources.back();
            _freeSources.pop_back();
            return source;
        }

        _sources.push_back(std::make_unique<MarkerSource>());
        MarkerSource *source = _sources.back().get();
        source->wuid         = kMarkerWuidBase + ++_nextSourceSerial;
        return source;
    }

    void QuestService::AddCompassMark(ObjectiveMarker &marker) {
        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        if (compass == nullptr) {
            return;
        }

        // A fixed-position mark rather than the source-following one a shipped
        // objective gets: that one attaches its source to the AI object
        // manager, which a stand-in must never reach.
        marker.compassMark = SDK::wh::guimodule::MakeCompassMark(marker.markType, marker.source->position, kQuestCompassMarkState);
        if (!marker.compassMark) {
            return;
        }

        marker.compassMark.Get()->SetQuestObjective(marker.trackingId, static_cast<std::int32_t>(marker.index));
        compass->AddMark(marker.compassMark.Clone());
    }

    void QuestService::RemoveCompassMark(ObjectiveMarker &marker) {
        if (!marker.compassMark) {
            return;
        }

        // The reference held here keeps the mark up across the call: the
        // native lets go of the compass's own before it reads the mark again.
        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        if (compass != nullptr && marker.compassMark.Get()->IsRegistered()) {
            compass->RemoveMark(marker.compassMark.Clone());
        }

        marker.compassMark.Release();
    }

    void QuestService::UpdateMarkers() {
        auto *replication                     = Framework::CoreModules::GetReplication();
        SDK::wh::guimodule::C_UIQuestLog *log = SDK::wh::guimodule::C_UIQuestLog::GetInstance();

        // What should be marked now: every positioned, active objective of an
        // active quest this machine follows -- the rule a shipped quest's
        // markers obey too.
        std::vector<ObjectiveMarker> wanted;
        std::vector<SDK::Vec3> wantedPositions;
        if (replication != nullptr && log != nullptr) {
            replication->ForEach<KCDC::Features::Quest::Quest>([&](KCDC::Features::Quest::Quest *quest) {
                SDK::wh::questmodule::C_Quest *native = quest->GetNative();
                if (native == nullptr || quest->GetProgress() != Shared::Entities::QuestProgress::Active) {
                    return;
                }

                const std::int32_t trackingId = log->GetTrackingId(native->GetName());
                if (trackingId == -1) {
                    return;
                }

                const std::size_t count = std::min<std::size_t>(quest->objectiveCount, Shared::Entities::QuestEntity::kMaxObjectives);
                for (std::size_t index = 0; index < count; ++index) {
                    const glm::vec3 &position = quest->objectivePosition[index];
                    if (!quest->objectiveHasPosition[index] || quest->GetObjectiveProgress(index) != Shared::Entities::QuestProgress::Active || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
                        continue;
                    }

                    ObjectiveMarker marker;
                    marker.questNetworkId = quest->GetNetworkID();
                    marker.index          = index;
                    marker.questKey       = quest->questKey;
                    marker.text           = quest->objectiveText[index];
                    marker.markType       = SDK::wh::guimodule::QuestTypeToCompassMarkType(static_cast<SDK::wh::questmodule::E_QuestType>(quest->GetQuestType()));
                    marker.trackingId     = trackingId;
                    wanted.push_back(std::move(marker));
                    wantedPositions.push_back(SDK::Vec3 {position.x, position.y, position.z});
                }
            });
        }

        bool changed = false;

        // Let go of what is no longer wanted first.
        for (auto marker = _markers.begin(); marker != _markers.end();) {
            const bool keep = std::any_of(wanted.begin(), wanted.end(), [&marker](const ObjectiveMarker &entry) {
                return entry.questNetworkId == marker->questNetworkId && entry.index == marker->index;
            });
            if (keep) {
                ++marker;
                continue;
            }

            RemoveCompassMark(*marker);
            _freeSources.push_back(marker->source);
            marker  = _markers.erase(marker);
            changed = true;
        }

        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        for (std::size_t slot = 0; slot < wanted.size(); ++slot) {
            ObjectiveMarker &entry    = wanted[slot];
            const SDK::Vec3 &position = wantedPositions[slot];

            auto marker = std::find_if(_markers.begin(), _markers.end(), [&entry](const ObjectiveMarker &existing) {
                return existing.questNetworkId == entry.questNetworkId && existing.index == entry.index;
            });
            if (marker == _markers.end()) {
                entry.source           = AcquireSource();
                entry.source->position = position;
                AddCompassMark(entry);
                _markers.push_back(std::move(entry));
                changed = true;
                continue;
            }

            // The compass takes the icon and the tracking slot once, when the
            // mark is added, so either changing is a new mark.
            if (marker->markType != entry.markType || marker->trackingId != entry.trackingId) {
                RemoveCompassMark(*marker);
                marker->markType   = entry.markType;
                marker->trackingId = entry.trackingId;
                AddCompassMark(*marker);
                marker->stale = true;
                changed       = true;
            }

            // The compass re-reads the mark's position every frame; the map
            // row copied it when it was drawn, so it has to be drawn again.
            if (!SamePosition(marker->source->position, position)) {
                marker->source->position = position;
                if (marker->compassMark) {
                    marker->compassMark.Get()->SetWorldPosition(position);
                }
                marker->stale = true;
                changed       = true;
            }

            if (marker->text != entry.text) {
                marker->text  = entry.text;
                marker->stale = true;
                changed       = true;
            }

            // The game empties the compass across its own lifecycle edges.
            if (!marker->compassMark) {
                AddCompassMark(*marker);
            }
            else if (compass != nullptr && !marker->compassMark.Get()->IsRegistered()) {
                compass->AddMark(marker->compassMark.Clone());
            }
        }

        if (changed) {
            _mapRefreshWanted = true;
        }
    }

    void QuestService::RefreshMapMarkers() {
        if (!_mapRefreshWanted) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - _lastMapRefresh < kMarkerRefreshInterval) {
            return;
        }

        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr) {
            return;
        }

        _lastMapRefresh   = now;
        _mapRefreshWanted = false;

        // The map tells its rows apart by quest, objective and tracking slot
        // alone, so a row whose position or text changed is only drawn again
        // once it has been missing from one refresh.
        const bool anyStale = std::any_of(_markers.begin(), _markers.end(), [](const ObjectiveMarker &marker) {
            return marker.stale;
        });
        if (anyStale) {
            _leaveOutStale = true;
            map->RefreshQuestMarkers();
            _leaveOutStale = false;

            for (ObjectiveMarker &marker : _markers) {
                marker.stale = false;
            }
        }

        map->RefreshQuestMarkers();
    }

    void QuestService::ReleaseMarkers() {
        for (ObjectiveMarker &marker : _markers) {
            RemoveCompassMark(marker);
            _freeSources.push_back(marker.source);
        }

        if (!_markers.empty()) {
            _mapRefreshWanted = true;
        }
        _markers.clear();
    }

    void QuestService::AppendMarkerEntries(SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> &entries) const {
        if (_markers.empty()) {
            return;
        }

        SDK::wh::guimodule::C_UIQuestLog *log = SDK::wh::guimodule::C_UIQuestLog::GetInstance();
        if (log == nullptr) {
            return;
        }

        // What the quest's own GetLevelId detour answers, so the rows file
        // under the map the player is on, like the quest does.
        const std::int32_t levelId = hook::call<std::int32_t>(SDK::gPatterns.GetCurrentLevelId);

        for (const ObjectiveMarker &marker : _markers) {
            if (_leaveOutStale && marker.stale) {
                continue;
            }

            // Asked again rather than taken from the tick: the tracking natives
            // redraw the markers from inside themselves, before the tick has
            // seen the change.
            const SDK::ScopedCryString questName(marker.questKey.c_str());
            const std::int32_t trackingId = log->GetTrackingId(questName);
            if (trackingId == -1) {
                continue;
            }

            const std::string identifier = ObjectiveIdentifier(marker.questKey, marker.index);
            const SDK::ScopedCryString objectiveIdentifier(identifier.c_str());

            // C_UIFlashObject localizes the row's name again, so give it one
            // key rather than letting it turn each word of the text into a key.
            const std::string label = ObjectiveLabel(marker.questNetworkId, marker.index);
            const SDK::ScopedCryString objectiveText(label.c_str());

            // The strings are borrowed: the append copies the entry, and the
            // copy takes its own reference on each.
            SDK::wh::guimodule::S_QuestMarkerEntry entry {};
            entry.questName           = questName.Get();
            entry.objectiveIdentifier = objectiveIdentifier.Get();
            entry.markType            = marker.markType;
            entry.levelId             = levelId;
            entry.trackingId          = trackingId;
            entry.objectiveText       = objectiveText.Get();
            entry.objectiveIndex      = static_cast<std::int32_t>(marker.index);
            entry.source              = marker.source;
            SDK::wh::guimodule::AppendQuestMarkerEntry(entries, entry);
        }
    }

    void QuestService::DrainAnnouncements() {
        std::vector<Shared::RPC::QuestAnnounced> pending;
        {
            std::scoped_lock lock(_pendingAnnouncementsMutex);
            pending.swap(_pendingAnnouncements);
        }

        if (pending.empty()) {
            return;
        }

        SDK::wh::guimodule::C_UIHudEvents *hudEvents = SDK::wh::guimodule::C_UIHudEvents::GetInstance();
        if (hudEvents == nullptr) {
            return;
        }

        for (const Shared::RPC::QuestAnnounced &announced : pending) {
            // The node name is what the tracking-id lookup keys on; the title
            // goes through unlocalized, because the server wrote it.
            const SDK::ScopedCryString nodeName(announced.questKey.c_str());
            const SDK::ScopedCryString displayName(announced.title.c_str());

            // Not the quest toast, which can only say a quest started, finished
            // or failed: the objective toast, by the log type its state gives.
            const auto kind = static_cast<Shared::RPC::QuestAnnounceKind>(announced.kind);
            if (kind == Shared::RPC::QuestAnnounceKind::ObjectiveChanged || kind == Shared::RPC::QuestAnnounceKind::ObjectiveCompleted || kind == Shared::RPC::QuestAnnounceKind::ObjectiveFailed) {
                const auto logType     = kind == Shared::RPC::QuestAnnounceKind::ObjectiveCompleted ? SDK::wh::questmodule::E_LogType::Completed : kind == Shared::RPC::QuestAnnounceKind::ObjectiveFailed ? SDK::wh::questmodule::E_LogType::Canceled : SDK::wh::questmodule::E_LogType::Updated;
                const auto *log        = SDK::wh::guimodule::C_UIQuestLog::GetInstance();
                const std::int32_t slot = log != nullptr ? log->GetTrackingId(nodeName.Get()) : -1;
                const SDK::ScopedCryString objectiveName(announced.detail.c_str());
                hudEvents->ShowObjectiveEvent(logType, objectiveName.Get(), announced.questType, slot, nodeName.Get(), displayName.Get());
                continue;
            }

            const auto progress = kind == Shared::RPC::QuestAnnounceKind::Completed ? SDK::wh::questmodule::E_QuestProgress::Done : kind == Shared::RPC::QuestAnnounceKind::Failed ? SDK::wh::questmodule::E_QuestProgress::Failed : SDK::wh::questmodule::E_QuestProgress::Active;

            static_cast<SDK::wh::questmodule::I_UIHudEventsQuest *>(hudEvents)->InvokeShowQuestEventRaw(nodeName, progress, displayName, false, static_cast<SDK::wh::questmodule::E_QuestType>(announced.questType));
        }
    }

    void QuestService::QueueDestroy(SDK::wh::questmodule::C_Quest *quest, std::vector<SDK::wh::questmodule::C_Objective *> &&objectives, std::vector<SDK::wh::questmodule::C_LogBase *> &&logs) {
        if (quest == nullptr) {
            return;
        }

        std::scoped_lock lock(_pendingDestroysMutex);
        _pendingDestroys.push_back(PendingDestroy {quest, std::move(objectives), std::move(logs)});
    }

    void QuestService::DrainDestroys() {
        std::vector<PendingDestroy> pending;
        {
            std::scoped_lock lock(_pendingDestroysMutex);
            pending.swap(_pendingDestroys);
        }

        for (PendingDestroy &entry : pending) {
            Unregister(entry.quest);

            // The node's claim goes before the replica's, so nothing is
            // destroyed while the other list still names it.
            entry.quest->SetObjectives(nullptr, 0);
            for (SDK::wh::questmodule::C_Objective *objective : entry.objectives) {
                objective->Release();
            }

            // The objectives are gone, so nothing names these any more.
            for (SDK::wh::questmodule::C_LogBase *log : entry.logs) {
                SDK::wh::questmodule::C_LogBase::Destroy(log);
            }

            entry.quest->Release();
        }
    }

    void QuestService::Update() {
        ObserveNativeContext();
        DrainDestroys();

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        replication->ForEach<KCDC::Features::Quest::Quest>([](KCDC::Features::Quest::Quest *quest) {
            quest->Frame();
        });

        // After the frames, so a quest given and tracked in one breath is
        // built by the time its instruction is looked at.
        DrainTrackRequests();
        // After the tracks: the toast reads the tracked table as it is shown,
        // so a quest given tracked is announced in its tracked colours.
        DrainAnnouncements();
        UpdateMarkers();
        RefreshMapMarkers();
    }

    void QuestService::Reset() {
        DrainDestroys();

        {
            std::scoped_lock lock(_pendingAnnouncementsMutex);
            _pendingAnnouncements.clear();
        }

        {
            std::scoped_lock lock(_pendingTracksMutex);
            _pendingTracks.clear();
        }
        _heldTracks.clear();

        // The map is redrawn on the next tick, once `_quests` below no longer
        // names nodes whose replicas are gone.
        ReleaseMarkers();

        // What is left belongs to replicas already torn down.
        _quests.clear();
        _warnedAtBudget    = false;
        _warnedAtPageLimit = false;
    }

    void QuestService::NativeContextChanged() {
        ++_nativeContext;
    }

    void QuestService::ObserveNativeContext() {
        const std::uint64_t current = _nativeContext.load();
        if (current == _observedContext) {
            return;
        }
        _observedContext = current;

        // Dropped rather than released: these belonged to the registry of the
        // level that is gone, so releasing one now would unregister an id that
        // registry has already handed out again.
        {
            std::scoped_lock lock(_pendingDestroysMutex);
            _pendingDestroys.clear();
        }

        _quests.clear();
        _warnedAtBudget    = false;
        _warnedAtPageLimit = false;

        // The quests they marked are gone with the level; the replicas build
        // new ones and the next tick marks those.
        ReleaseMarkers();

        // The replicas are still here and still want their quests.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::Quest::Quest>([](KCDC::Features::Quest::Quest *quest) {
                quest->Unbind();
            });
        }
    }

    bool QuestService::HasRoom() {
        if (_quests.size() < kMaxLiveQuests) {
            return true;
        }

        if (!_warnedAtBudget) {
            _warnedAtBudget = true;
            Framework::Logging::GetLogger(kLogger)->warn("Refusing quests: the journal already carries {}, which is the budget", kMaxLiveQuests);
        }
        return false;
    }

    void QuestService::Register(SDK::wh::questmodule::C_Quest *quest) {
        _quests.push_back(quest);
    }

    void QuestService::Unregister(SDK::wh::questmodule::C_Quest *quest) {
        _quests.erase(std::remove(_quests.begin(), _quests.end(), quest), _quests.end());
    }

    void QuestService::AppendTo(SDK::StdVector<SDK::wh::questmodule::C_Quest *> &collected) {
        Framework::Logging::GetLogger(kLogger)->debug("The quest list was asked for: {} from the game, {} from the server", collected.Size(), _quests.size());

        if (_quests.empty()) {
            return;
        }

        // Rebuilt rather than grown: the mod cannot reach the template that
        // grows one, and the list is short. AssignResourceVector claims the new
        // entries before it lets the old ones go, so a list that starts with
        // what the vector already holds is safe to hand it.
        std::vector<SDK::wh::questmodule::C_Quest *> combined;
        combined.reserve(collected.Size() + _quests.size());
        combined.insert(combined.end(), collected.begin(), collected.end());

        for (SDK::wh::questmodule::C_Quest *quest : _quests) {
            if (IsRenderable(quest)) {
                combined.push_back(quest);
                continue;
            }

            // Held back: the row builder would fault on it. It stays
            // registered, so the next refresh gets another chance.
            const SDK::StdVector<SDK::wh::conceptmodule::S_WeakResourceId> &objectives = quest->GetObjectives();
            Framework::Logging::GetLogger(kLogger)->warn("Holding quest \"{}\" back from the journal: its objective list is {} entries at {}..{}", quest->GetName().m_pData != nullptr ? quest->GetName().m_pData : "?", objectives.Size(), static_cast<const void *>(objectives.first),
                static_cast<const void *>(objectives.last));
        }

        if (combined.size() == collected.Size()) {
            return;
        }

        if (!SDK::wh::conceptmodule::AssignResourceVector(collected, combined.data(), combined.size()) && !_warnedAtPageLimit) {
            _warnedAtPageLimit = true;
            Framework::Logging::GetLogger(kLogger)->warn("Leaving the journal as the game built it: {} quests is more than one page of handles", combined.size());
        }
    }

    bool QuestService::IsRenderable(const SDK::wh::questmodule::C_Quest *quest) {
        if (quest == nullptr) {
            return false;
        }

        const SDK::StdVector<SDK::wh::conceptmodule::S_WeakResourceId> &objectives = quest->GetObjectives();
        if ((objectives.first == nullptr) != (objectives.last == nullptr) || objectives.last < objectives.first) {
            return false;
        }

        if (objectives.Size() > Shared::Entities::QuestEntity::kMaxObjectives) {
            return false;
        }

        // Only the shape. Whether each handle still resolves is the registry's
        // answer, and this side cannot ask it.
        return true;
    }

    bool QuestService::Owns(const SDK::wh::questmodule::C_Quest *quest) const {
        return std::find(_quests.begin(), _quests.end(), quest) != _quests.end();
    }

    void QuestService::NoteTracking(const SDK::wh::questmodule::C_Quest *quest, bool tracked) {
        if (quest == nullptr || !Owns(quest) || quest->GetName().m_pData == nullptr) {
            return;
        }

        const std::string questKey(quest->GetName().m_pData);

        // This machine first, then the wire: a resource loaded on the client
        // sees the change at the moment it happened rather than after a round
        // trip it does not need.
        Scripting::Events::QuestTrackingChanged(questKey, tracked);
        KCDC::Core::gApplication->GetLocalEvents().QuestTrackingChanged(questKey, tracked);
    }

    SDK::wh::questmodule::C_Quest *QuestService::FindByName(const SDK::CryStringT<char> &name) const {
        if (name.m_pData == nullptr) {
            return nullptr;
        }

        for (SDK::wh::questmodule::C_Quest *quest : _quests) {
            const SDK::CryStringT<char> &nodeName = quest->GetName();
            if (nodeName.m_pData != nullptr && std::strcmp(nodeName.m_pData, name.m_pData) == 0) {
                return quest;
            }
        }

        return nullptr;
    }
} // namespace KCDC::Features::Quest
