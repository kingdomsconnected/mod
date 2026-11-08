#include "quest_verbs.h"

#include "shared/entity_filter.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/quest/quest_rpc.h"

#include "features/quest/quest_scripting.h"

#include <algorithm>
#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Quest {
    namespace {
        constexpr const char *kLogger = "Quest";

        /** Trimmed rather than refused: a title one character over length is not worth losing the quest over. */
        std::string Bounded(const std::string &text) {
            return text.size() <= Shared::Entities::QuestEntity::kMaxTextLength ? text : text.substr(0, Shared::Entities::QuestEntity::kMaxTextLength);
        }

        void WriteObjectives(Shared::Entities::QuestEntity *quest, const std::vector<Shared::Entities::QuestObjective> &objectives) {
            const std::size_t count = std::min(objectives.size(), Shared::Entities::QuestEntity::kMaxObjectives);

            for (std::size_t index = 0; index < Shared::Entities::QuestEntity::kMaxObjectives; ++index) {
                if (index < count) {
                    quest->objectiveText[index]        = Bounded(objectives[index].text);
                    quest->objectiveProgress[index]    = static_cast<std::uint8_t>(objectives[index].progress);
                    quest->objectiveOptional[index]    = objectives[index].optional;
                    quest->objectiveHasPosition[index] = objectives[index].hasPosition;
                    quest->objectivePosition[index]    = objectives[index].hasPosition ? objectives[index].position : glm::vec3(0.0F);
                }
                else {
                    // Cleared rather than left behind: every slot goes over the
                    // wire on every write, so a stale one would keep arriving.
                    quest->objectiveText[index].clear();
                    quest->objectiveProgress[index]    = static_cast<std::uint8_t>(Shared::Entities::QuestProgress::None);
                    quest->objectiveOptional[index]    = false;
                    quest->objectiveHasPosition[index] = false;
                    quest->objectivePosition[index]    = glm::vec3(0.0F);
                }
            }

            quest->objectiveCount = static_cast<std::uint8_t>(count);
        }
    } // namespace

    std::vector<Shared::Entities::QuestEntity *> Quests::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::QuestEntity *> quests;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::QuestEntity>([&](Shared::Entities::QuestEntity *quest) {
                if (Shared::Entities::InWorld(quest, virtualWorld)) {
                    quests.push_back(quest);
                }
            });
        }
        return quests;
    }

    void Quests::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Quest RPC installation has no network peer");
            return;
        }

        // The one thing a client tells the server about a quest, and a report
        // rather than a request: nothing here refuses it or writes back.
        net->RegisterRPC<Shared::RPC::QuestTrackChanged>([](const Shared::RPC::QuestTrackChanged &change, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (packet == nullptr || !change.IsSane() || replication == nullptr) {
                return;
            }

            const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
            auto *player                  = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            if (player == nullptr) {
                return;
            }

            // Resolved against the sender, so a client can only speak for a
            // quest it was given: its own copy first, then a world-wide one.
            Shared::Entities::QuestEntity *quest = Find(change.questKey, player->GetVirtualWorld(), guid);
            if (quest == nullptr) {
                quest = Find(change.questKey, player->GetVirtualWorld());
                if (quest != nullptr && OwnerOf(quest).has_value()) {
                    return;
                }
            }
            if (quest == nullptr) {
                return;
            }

            Scripting::Events::QuestTrackingChanged(quest->GetNetworkID(), player->GetNetworkID(), change.tracked);
        });
    }

    std::optional<MafiaNet::PeerGuid> Quests::OwnerOf(const Shared::Entities::QuestEntity *quest) {
        if (quest == nullptr || quest->streaming.targetGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            return std::nullopt;
        }
        return quest->streaming.targetGUID;
    }

    Shared::Entities::QuestEntity *Quests::Find(const std::string &questKey, std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::optional<MafiaNet::PeerGuid> owner) {
        for (Shared::Entities::QuestEntity *quest : Collect(virtualWorld)) {
            if (quest->questKey != questKey) {
                continue;
            }
            if (owner.has_value() && OwnerOf(quest) != owner) {
                continue;
            }
            return quest;
        }
        return nullptr;
    }

    void Quests::Announce(const Shared::Entities::QuestEntity *quest, Shared::RPC::QuestAnnounceKind kind, const std::string &detail) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        if (quest == nullptr || net == nullptr || replication == nullptr) {
            return;
        }

        Shared::RPC::QuestAnnounced announced;
        announced.questKey  = quest->questKey;
        announced.title     = quest->title;
        announced.detail    = Bounded(detail);
        announced.questType = quest->questType;
        announced.kind      = static_cast<std::uint8_t>(kind);

        if (!announced.IsSane()) {
            return;
        }

        const std::optional<MafiaNet::PeerGuid> owner = OwnerOf(quest);
        if (owner.has_value()) {
            net->SendRPC(announced, MafiaNet::ToGuid(*owner), MafiaNet::Priority::Medium, MafiaNet::Reliability::Reliable);
            return;
        }

        // Nobody owns it, so everyone in its world hears it. A player in
        // another world cannot see the quest and has nothing to be told.
        const MafiaNet::VirtualWorldId virtualWorld = quest->GetVirtualWorld();
        replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *player) {
            if (player->GetVirtualWorld() == virtualWorld) {
                net->SendRPC(announced, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::Medium, MafiaNet::Reliability::Reliable);
            }
        });
    }

    Shared::Entities::QuestEntity *Quests::Give(const std::string &questKey, const std::string &title, const std::string &description, Shared::Entities::QuestType questType, const std::vector<Shared::Entities::QuestObjective> &objectives, std::optional<MafiaNet::PeerGuid> owner,
        std::uint32_t virtualWorld) {
        if (!Shared::Entities::QuestEntity::IsSaneKey(questKey)) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing quest key \"{}\": a key is up to 64 letters, digits, underscores or dashes", questKey);
            return nullptr;
        }

        // Per owner, not per world: two players in one world each holding
        // "errand" is the ordinary case, and each client only ever receives
        // the copy addressed to it.
        if (Find(questKey, virtualWorld, owner) != nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing quest \"{}\": that recipient already has one under that key in world {}", questKey, virtualWorld);
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *quest = replication->CreateEntity<Shared::Entities::QuestEntity>();
        if (quest == nullptr) {
            return nullptr;
        }

        // Before the entity replicates, so all of it rides the construction
        // snapshot rather than arriving a tick after an empty journal row.
        quest->questKey    = questKey;
        quest->title       = Bounded(title);
        quest->description = Bounded(description);
        quest->questType   = static_cast<std::uint8_t>(questType);
        quest->progress    = static_cast<std::uint8_t>(Shared::Entities::QuestProgress::Active);
        WriteObjectives(quest, objectives);

        // A quest is not anywhere, so range culling has nothing to measure.
        quest->streaming.alwaysVisible = true;
        if (owner.has_value()) {
            quest->streaming.targetGUID = *owner;
        }
        quest->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Gave quest {} (\"{}\") in world {} with {} objectives", quest->GetNetworkID(), questKey, virtualWorld, quest->objectiveCount);
        return quest;
    }

    void Quests::SetText(Shared::Entities::QuestEntity *quest, const std::string &title, const std::string &description) {
        if (quest == nullptr) {
            return;
        }

        quest->title       = Bounded(title);
        quest->description = Bounded(description);
    }

    void Quests::SetProgress(Shared::Entities::QuestEntity *quest, Shared::Entities::QuestProgress progress, bool announce) {
        if (quest == nullptr) {
            return;
        }

        // None would drop the quest out of the journal while leaving the entity
        // replicated, which reads to a player as the quest having silently
        // vanished. Removing one is destroying it.
        if (progress == Shared::Entities::QuestProgress::None) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing to move quest \"{}\" to None; destroy it instead", quest->questKey);
            return;
        }

        if (quest->progress == static_cast<std::uint8_t>(progress)) {
            return;
        }

        quest->progress = static_cast<std::uint8_t>(progress);
        if (announce) {
            Announce(quest, progress == Shared::Entities::QuestProgress::Done ? Shared::RPC::QuestAnnounceKind::Completed : progress == Shared::Entities::QuestProgress::Failed ? Shared::RPC::QuestAnnounceKind::Failed : Shared::RPC::QuestAnnounceKind::Started);
        }
    }

    void Quests::SetObjective(Shared::Entities::QuestEntity *quest, std::size_t index, const Shared::Entities::QuestObjective &objective, bool announce) {
        if (quest == nullptr || index >= quest->objectiveCount) {
            return;
        }

        quest->objectiveText[index]        = Bounded(objective.text);
        quest->objectiveProgress[index]    = static_cast<std::uint8_t>(objective.progress);
        quest->objectiveOptional[index]    = objective.optional;
        quest->objectiveHasPosition[index] = objective.hasPosition;
        quest->objectivePosition[index]    = objective.hasPosition ? objective.position : glm::vec3(0.0F);

        if (announce) {
            Announce(quest, objective.progress == Shared::Entities::QuestProgress::Done ? Shared::RPC::QuestAnnounceKind::ObjectiveCompleted : objective.progress == Shared::Entities::QuestProgress::Failed ? Shared::RPC::QuestAnnounceKind::ObjectiveFailed : Shared::RPC::QuestAnnounceKind::ObjectiveChanged, quest->objectiveText[index]);
        }
    }

    void Quests::SetObjectives(Shared::Entities::QuestEntity *quest, const std::vector<Shared::Entities::QuestObjective> &objectives, bool announce) {
        if (quest == nullptr) {
            return;
        }

        WriteObjectives(quest, objectives);
        if (!announce) {
            return;
        }

        // The line worth saying out loud is the one still to do.
        for (std::size_t index = 0; index < quest->objectiveCount; ++index) {
            if (quest->objectiveProgress[index] == static_cast<std::uint8_t>(Shared::Entities::QuestProgress::Active) && !quest->objectiveText[index].empty()) {
                Announce(quest, Shared::RPC::QuestAnnounceKind::ObjectiveChanged, quest->objectiveText[index]);
                return;
            }
        }
    }

    std::size_t Quests::Track(const Shared::Entities::QuestEntity *quest, bool tracked, std::optional<std::uint64_t> playerNetworkId) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        if (quest == nullptr || net == nullptr || replication == nullptr) {
            return 0;
        }

        Shared::RPC::QuestTrackRequest request;
        request.questKey = quest->questKey;
        request.tracked  = tracked;
        if (!request.IsSane()) {
            return 0;
        }

        const std::optional<MafiaNet::PeerGuid> owner = OwnerOf(quest);
        const MafiaNet::VirtualWorldId virtualWorld   = quest->GetVirtualWorld();

        // Only a client that has the quest can follow it: the owner of one
        // written for a single player, or anyone in the world of a shared one.
        const auto canSee = [&](const Shared::Entities::HumanEntity *player) {
            return owner.has_value() ? player->ownerGUID == *owner : player->GetVirtualWorld() == virtualWorld;
        };

        if (playerNetworkId.has_value()) {
            auto *player = replication->GetEntity<Shared::Entities::HumanEntity>(*playerNetworkId);
            if (player == nullptr || !canSee(player)) {
                return 0;
            }

            net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
            return 1;
        }

        std::size_t told = 0;
        replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *player) {
            if (canSee(player)) {
                net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
                ++told;
            }
        });
        return told;
    }

    void Quests::Remove(Shared::Entities::QuestEntity *quest) {
        if (quest == nullptr) {
            return;
        }

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            Framework::Logging::GetLogger(kLogger)->info("Removed quest {} (\"{}\")", quest->GetNetworkID(), quest->questKey);
            replication->DestroyEntity(quest);
        }
    }

    std::size_t Quests::RemoveAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::QuestEntity *> quests = Collect(virtualWorld);
        for (Shared::Entities::QuestEntity *quest : quests) {
            Remove(quest);
        }
        return quests.size();
    }
} // namespace KCDC::Features::Quest
