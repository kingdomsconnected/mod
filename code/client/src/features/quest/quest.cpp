#include "quest.h"
#include "quest_service.h"

#include "core/application.h"

#include "sdk/types.h"
#include "sdk/wh/questmodule/c_quest_manager.h"
#include "sdk/wh/questmodule/e_log_type.h"
#include "sdk/wh/questmodule/e_quest_progress.h"
#include "sdk/wh/questmodule/e_quest_type.h"

#include <algorithm>
#include <cstddef>
#include <logging/logger.h>
#include <string>

namespace KCDC::Features::Quest {
    namespace {
        constexpr const char *kLogger = "Quest";

        /**
         * The progress values an objective's log list covers, spelled the way
         * the journal spells them when it looks one up.
         *
         * `None` is left out deliberately: an objective whose list misses its
         * own progress is hidden, which is how a shipped quest keeps a step
         * out of sight until it matters.
         */
        constexpr const char *kProgressNames[] = {"Active", "Done", "Failed"};

        /**
         * The log type each progress reads as, in `kProgressNames` order. The
         * row builder (0x181F5C5A0) shows only `Started` and `Updated`, and the
         * diary (0x181F5C1A0) files `Completed` and `Canceled` as finished, so
         * an objective done or failed leaves the list as a shipped one does.
         */
        constexpr SDK::wh::questmodule::E_LogType kProgressLogTypes[] = {SDK::wh::questmodule::E_LogType::Updated, SDK::wh::questmodule::E_LogType::Completed, SDK::wh::questmodule::E_LogType::Canceled};

        constexpr std::size_t kProgressLogCount = sizeof(kProgressNames) / sizeof(kProgressNames[0]);
        static_assert(sizeof(kProgressLogTypes) / sizeof(kProgressLogTypes[0]) == kProgressLogCount, "one log type per progress name");

        /** Building a node reaches the concept registry, which comes up alongside the quest module -- so the manager existing is the same fact. */
        bool QuestSystemReady() {
            return SDK::wh::questmodule::C_QuestManager::GetInstance() != nullptr;
        }
    } // namespace

    void Quest::DeallocReplica(MafiaNet::Connection_RM3 *) {
        DestroyNative();
        delete this;
    }

    void Quest::OnConstructed() {
        _dirty = true;
    }

    void Quest::OnDeserialized(bool) {
        _dirty = true;
    }

    void Quest::Unbind() {
        // Not a teardown: the registry that held the node went with the level,
        // so releasing it now would walk a registry that no longer exists.
        _native = nullptr;
        _objectives.clear();
        _logs.clear();
        ForgetApplied();
        _buildDeferred = false;
        _dirty         = true;
    }

    void Quest::ForgetApplied() {
        _appliedProgress = 0xFFU;

        for (std::size_t index = 0; index < Shared::Entities::QuestEntity::kMaxObjectives; ++index) {
            _appliedObjectiveProgress[index] = 0xFFU;
        }
    }

    void Quest::Frame() {
        if (!_dirty.exchange(false)) {
            return;
        }

        if (!EnsureNative()) {
            _dirty = true;
            return;
        }

        ApplyState();
    }

    bool Quest::EnsureNative() {
        if (_native != nullptr) {
            return true;
        }

        if (!QuestSystemReady()) {
            if (!_buildDeferred) {
                _buildDeferred = true;
                Framework::Logging::GetLogger(kLogger)->debug("Holding quest \"{}\" until the quest module is up", questKey);
            }
            return false;
        }

        if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetQuestService().HasRoom()) {
            return false;
        }

        SDK::wh::questmodule::C_Quest *quest = SDK::wh::questmodule::C_Quest::Create();
        quest->SetName(questKey.c_str());

        // Labels rather than text: the localiser turns a sentence into one
        // '@word' key per word, but leaves a token that already starts with
        // '@' alone. They never change, because they name the replica.
        quest->SetQuestName(QuestService::TitleLabel(GetNetworkID()).c_str());
        quest->SetQuestDescription(QuestService::DescriptionLabel(GetNetworkID()).c_str());
        KCDC::Core::gApplication->GetQuestService().Register(quest);

        _native        = quest;
        _buildDeferred = false;
        return true;
    }

    void Quest::ApplyState() {
        // A plain store, so it is not worth tracking.
        _native->SetType(static_cast<SDK::wh::questmodule::E_QuestType>(GetQuestType()));

        if (_appliedProgress != progress) {
            _native->SetProgress(static_cast<SDK::wh::questmodule::E_QuestProgress>(GetProgress()));
            _appliedProgress = progress;

            // Read back through the game's own getter: the port is bound
            // through its resolved cache rather than an authored definition,
            // and a mismatch is why a built quest would still not render.
            const auto observed = static_cast<std::uint8_t>(_native->GetProgress());
            if (observed != progress) {
                Framework::Logging::GetLogger(kLogger)->warn("Quest \"{}\" progress port reads {} after writing {}; the journal will not show it", questKey, observed, progress);
            }
        }

        ApplyObjectives();
    }

    void Quest::ApplyObjectives() {
        const std::size_t count = std::min<std::size_t>(objectiveCount, Shared::Entities::QuestEntity::kMaxObjectives);

        if (_objectives.size() != count) {
            // The node's claim goes first, so the replica's is the last one
            // standing and the new list is in place before anything is let go.
            std::vector<SDK::wh::questmodule::C_Objective *> replaced;
            replaced.swap(_objectives);

            std::vector<SDK::wh::questmodule::C_LogBase *> replacedLogs;
            replacedLogs.swap(_logs);

            _objectives.reserve(count);
            _logs.reserve(count * kProgressLogCount);
            for (std::size_t index = 0; index < count; ++index) {
                SDK::wh::questmodule::C_Objective *objective = SDK::wh::questmodule::C_Objective::Create();
                const std::string name                       = questKey + "#" + std::to_string(index);
                objective->SetName(name.c_str());
                objective->SetLocalizedName(QuestService::ObjectiveLabel(GetNetworkID(), index).c_str());

                // Not the node name: this is what the Flash row is addressed
                // by, and it is empty on a freshly built objective.
                objective->SetIdentifier(name.c_str());

                // One log per progress value, so the objective is found
                // whatever state it is in; the order is what it sorts by.
                SDK::wh::questmodule::C_LogBase *logs[kProgressLogCount] = {};
                for (std::size_t slot = 0; slot < kProgressLogCount; ++slot) {
                    logs[slot] = SDK::wh::questmodule::C_LogBase::Create();
                    logs[slot]->SetLogType(kProgressLogTypes[slot]);
                    logs[slot]->SetOrder(static_cast<std::int64_t>(index) + 1);
                    _logs.push_back(logs[slot]);
                }

                objective->SetProgressLogs(logs, kProgressNames, kProgressLogCount);
                _objectives.push_back(objective);
            }

            _native->SetObjectives(_objectives.data(), _objectives.size());
            Framework::Logging::GetLogger(kLogger)->debug("Quest \"{}\" now carries {} objective(s); the node reads {}", questKey, _objectives.size(), _native->GetObjectives().Size());

            for (SDK::wh::questmodule::C_Objective *objective : replaced) {
                objective->Release();
            }

            // After the objectives, so nothing is left pointing at a freed log.
            for (SDK::wh::questmodule::C_LogBase *log : replacedLogs) {
                SDK::wh::questmodule::C_LogBase::Destroy(log);
            }

            // The new objectives are blank, whatever the old ones held.
            for (std::size_t index = 0; index < Shared::Entities::QuestEntity::kMaxObjectives; ++index) {
                _appliedObjectiveProgress[index] = 0xFFU;
            }
        }

        for (std::size_t index = 0; index < count; ++index) {
            SDK::wh::questmodule::C_Objective *objective = _objectives[index];
            objective->SetOptional(objectiveOptional[index]);

            if (_appliedObjectiveProgress[index] != objectiveProgress[index]) {
                objective->SetProgress(static_cast<SDK::wh::questmodule::E_QuestProgress>(GetObjectiveProgress(index)));
                _appliedObjectiveProgress[index] = objectiveProgress[index];
            }
        }
    }

    void Quest::DestroyNative() {
        if (_native == nullptr) {
            return;
        }

        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetQuestService().QueueDestroy(_native, std::move(_objectives), std::move(_logs));
        }

        _native = nullptr;
        _objectives.clear();
        _logs.clear();
    }
} // namespace KCDC::Features::Quest
