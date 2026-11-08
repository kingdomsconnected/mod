#include "siege_ladder_service.h"

#include "core/server_clock.h"
#include "features/animation/animation_verbs.h"
#include "features/player/player_roster.h"
#include "features/siege/siege_ladder_scripting.h"

#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cmath>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "SiegeLadder";

        /** How far above or below the end a player may stand and still reach it, in metres. */
        constexpr float kReachHeight = 2.5F;

        bool Reaches(const Shared::Entities::HumanEntity &player, const glm::vec3 &point, float reach) {
            return player.transformReady && glm::length(glm::vec2(player.position - point)) <= reach && std::abs(player.position.z - point.z) <= kReachHeight;
        }

        Shared::Game::AnimationPlayback Fragment(const char *fragment, const char *tags) {
            Shared::Game::AnimationPlayback playback;
            playback.fragment         = fragment;
            playback.tags             = tags;
            playback.lockMovement     = true;
            playback.endsWithFragment = true;
            return playback;
        }
    } // namespace

    SiegeLadderService &SiegeLadderService::Get() {
        static SiegeLadderService instance;
        return instance;
    }

    void SiegeLadderService::Install() {
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->RegisterRPC<Shared::RPC::SiegeLadderUse>([this](const Shared::RPC::SiegeLadderUse &use, MafiaNet::Packet *packet) {
                OnUse(use, packet);
            });
        }
    }

    Shared::Entities::SiegeLadderEntity *SiegeLadderService::Spawn(std::size_t assetIndex, const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld) {
        const Shared::Game::SiegeLadderAsset *asset = Shared::Game::SiegeLadderAssetAt(assetIndex);
        if (asset == nullptr || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return nullptr;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *ladder      = replication != nullptr ? replication->CreateEntity<Shared::Entities::SiegeLadderEntity>() : nullptr;
        if (ladder == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a siege ladder; is '{}' registered?", Shared::Entities::SiegeLadderEntity::kTypeName);
            return nullptr;
        }

        // All of it before the entity replicates, so it rides the construction snapshot.
        ladder->assetIndex      = static_cast<std::uint8_t>(assetIndex);
        ladder->position        = position;
        ladder->rotation        = glm::normalize(rotation);
        ladder->streaming.range = Shared::Entities::SiegeLadderEntity::kStreamRange;
        ladder->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Spawned {} ladder {} in world {} at {:.2f}, {:.2f}, {:.2f}", asset->name, ladder->GetNetworkID(), virtualWorld, position.x, position.y, position.z);
        Scripting::Events::SiegeLadderSpawned(ladder->GetNetworkID());
        return ladder;
    }

    void SiegeLadderService::Destroy(Shared::Entities::SiegeLadderEntity *ladder) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (ladder == nullptr || replication == nullptr) {
            return;
        }
        // While the entity is still here, so a handler can read it.
        Scripting::Events::SiegeLadderDestroyed(ladder->GetNetworkID());
        replication->DestroyEntity(ladder);
    }

    std::size_t SiegeLadderService::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::SiegeLadderEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::SiegeLadderEntity *ladder : doomed) {
            Destroy(ladder);
        }
        return doomed.size();
    }

    std::vector<Shared::Entities::SiegeLadderEntity *> SiegeLadderService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        return CollectSiegeEntities<Shared::Entities::SiegeLadderEntity>(virtualWorld);
    }

    Shared::Entities::SiegeLadderEntity *SiegeLadderService::Find(std::uint64_t networkId) {
        return FindSiegeEntity<Shared::Entities::SiegeLadderEntity>(networkId);
    }

    SiegeResult SiegeLadderService::Push(Shared::Entities::SiegeLadderEntity &ladder, std::uint64_t pusherNetworkId) {
        if (ladder.state != Shared::Entities::SiegeLadderState::Standing) {
            return SiegeResult {false, "is not standing against a wall", 0.0F};
        }
        ladder.state           = Shared::Entities::SiegeLadderState::Pushed;
        ladder.stepStartMs     = KCDC::ServerClock::NowMs();
        ladder.pusherNetworkId = pusherNetworkId;
        if (Shared::Entities::HumanEntity *pusher = Player::Players::Find(pusherNetworkId)) {
            Animation::Animations::Play(pusher, Fragment("GuardHlbrdPushAway_Player", ladder.Asset().pushTags));
        }
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} pushed by player {}", ladder.GetNetworkID(), pusherNetworkId);
        return SiegeResult {true, "", static_cast<float>(Shared::Entities::SiegeLadderEntity::kPushLandMs) / 1000.0F};
    }

    SiegeResult SiegeLadderService::Raise(Shared::Entities::SiegeLadderEntity &ladder, std::uint64_t raiserNetworkId) {
        if (ladder.state == Shared::Entities::SiegeLadderState::Broken) {
            return SiegeResult {false, "is broken", 0.0F};
        }
        if (ladder.state != Shared::Entities::SiegeLadderState::Down) {
            return SiegeResult {false, "is not lying on the ground", 0.0F};
        }
        ladder.state       = Shared::Entities::SiegeLadderState::Raising;
        ladder.stepStartMs = KCDC::ServerClock::NowMs();
        if (Shared::Entities::HumanEntity *raiser = Player::Players::Find(raiserNetworkId)) {
            Animation::Animations::Play(raiser, Fragment("LayLadder", "layFromIdle"));
        }
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} raised by player {}", ladder.GetNetworkID(), raiserNetworkId);
        return SiegeResult {true, "", static_cast<float>(Shared::Entities::SiegeLadderEntity::kRaiseMs) / 1000.0F};
    }

    void SiegeLadderService::Break(Shared::Entities::SiegeLadderEntity &ladder) {
        ladder.state           = Shared::Entities::SiegeLadderState::Broken;
        ladder.stepStartMs     = 0;
        ladder.pusherNetworkId = 0;
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} is broken", ladder.GetNetworkID());
    }

    void SiegeLadderService::Repair(Shared::Entities::SiegeLadderEntity &ladder) {
        ladder.state           = Shared::Entities::SiegeLadderState::Standing;
        ladder.stepStartMs     = 0;
        ladder.pusherNetworkId = 0;
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} is repaired", ladder.GetNetworkID());
    }

    void SiegeLadderService::OnUse(const Shared::RPC::SiegeLadderUse &use, MafiaNet::Packet *packet) {
        if (!use.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *player       = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        Shared::Entities::SiegeLadderEntity *ladder = Find(use.ladderNetworkId);
        if (player == nullptr || ladder == nullptr || !ladder->usable || player->GetVirtualWorld() != ladder->GetVirtualWorld()) {
            return;
        }
        const bool push = use.action == Shared::RPC::SiegeLadderAction::Push;

        // A push is done from the walk at the ladder's top, a raise from
        // beside its foot; a request from anywhere else is somebody's stale
        // prompt.
        if (push ? !Reaches(*player, ladder->PushFrom(), Shared::Entities::SiegeLadderEntity::kPushReach) : !Reaches(*player, ladder->position, Shared::Entities::SiegeLadderEntity::kRaiseReach)) {
            return;
        }
        if (push ? ladder->state != Shared::Entities::SiegeLadderState::Standing : ladder->state != Shared::Entities::SiegeLadderState::Down) {
            return;
        }

        const std::uint64_t playerNetworkId = player->GetNetworkID();
        const std::uint64_t ladderNetworkId = ladder->GetNetworkID();
        if (!Scripting::Events::SiegeLadderUse(playerNetworkId, ladderNetworkId, push ? "push" : "raise")) {
            return;
        }
        // A handler may have moved, pushed or destroyed it.
        ladder = Find(ladderNetworkId);
        if (ladder != nullptr) {
            if (push) {
                Push(*ladder, playerNetworkId);
            }
            else {
                Raise(*ladder, playerNetworkId);
            }
        }
    }

    void SiegeLadderService::Settle(Shared::Entities::SiegeLadderEntity &ladder) {
        const std::uint64_t ladderNetworkId = ladder.GetNetworkID();
        if (ladder.state == Shared::Entities::SiegeLadderState::Pushed) {
            ladder.state       = Shared::Entities::SiegeLadderState::Down;
            ladder.stepStartMs = 0;
            Scripting::Events::SiegeLadderFell(ladderNetworkId, ladder.pusherNetworkId);
        }
        else if (ladder.state == Shared::Entities::SiegeLadderState::Raising) {
            ladder.state       = Shared::Entities::SiegeLadderState::Standing;
            ladder.stepStartMs = 0;
            Scripting::Events::SiegeLadderRaised(ladderNetworkId);
        }
    }

    void SiegeLadderService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        const std::uint64_t now = KCDC::ServerClock::NowMs();
        // Collected first: a handler of the events this raises may destroy a ladder.
        _due.clear();
        replication->ForEach<Shared::Entities::SiegeLadderEntity>([this, now](Shared::Entities::SiegeLadderEntity *ladder) {
            const std::uint32_t duration = ladder->StepDurationMs();
            if (duration != 0 && ladder->stepStartMs != 0 && now >= ladder->stepStartMs && now - ladder->stepStartMs >= duration) {
                _due.push_back(ladder->GetNetworkID());
            }
        });
        for (const std::uint64_t networkId : _due) {
            if (Shared::Entities::SiegeLadderEntity *ladder = Find(networkId)) {
                Settle(*ladder);
            }
        }
    }
} // namespace KCDC::Features::Siege
