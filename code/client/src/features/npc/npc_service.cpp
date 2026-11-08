#include "features/npc/npc_service.h"
#include "features/npc/npc_inventory_module.h"

#include "features/npc/npc.h"

#include "core/application.h"
#include "core/input/native_keybinds.h"

#include "common/module_support.h"

#include "sdk/cryengine/entitysystem/i_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Npc {
    namespace {
        constexpr const char *kLogger = "Npc";

        /** How close the local player has to be to talk to an NPC. */
        constexpr float kInteractRange = 3.0F;

        /**
         * How far off straight ahead an NPC may be and still be the one meant.
         *
         * The dot product of the player's facing with the direction to the body:
         * 0.5 is a 60-degree cone either side, which is wide enough to be
         * forgiving and narrow enough that two people standing together are not
         * both candidates.
         */
        constexpr float kInteractFacing = 0.5F;

        /** A floor on how often one client may report a press. */
        constexpr MafiaNet::Time kInteractCooldownMs = 400;
    } // namespace

    NpcService &NpcService::Get() {
        static NpcService service;
        return service;
    }

    void NpcService::Install() {
        NpcInventoryModule::Install();
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("NPC RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<Shared::RPC::NpcAction>([](const Shared::RPC::NpcAction &action, MafiaNet::Packet *) {
            if (!action.IsSane()) {
                return;
            }
            Get().Receive(action);
        });
    }

    void NpcService::Receive(const Shared::RPC::NpcAction &action) {
        const std::lock_guard<std::mutex> lock(_inboxMutex);
        _inbox.push_back(action);
    }

    bool NpcService::IsServerOwnedBody(SDK::EntityId entityId) {
        if (entityId != 0 && Get()._previewEntity.load() == entityId) {
            return true;
        }
        if (entityId == 0) {
            return false;
        }
        return KCDC::Common::FindReplica<Npc>([entityId](const Npc *candidate) { return candidate->GetEntityId() == entityId; }) != nullptr;
    }

    std::optional<NpcService::CombatBody> NpcService::FindCombatBody(SDK::EntityId entityId) {
        if (entityId == 0) {
            return std::nullopt;
        }
        for (const CombatBody &body : Get()._combatBodies.Current().bodies) {
            if (body.entityId == entityId) {
                return body;
            }
        }
        return std::nullopt;
    }

    std::optional<NpcService::CombatBody> NpcService::FindCombatBodyOfSoul(const SDK::wh::rpgmodule::I_Soul *soul) {
        if (soul == nullptr) {
            return std::nullopt;
        }
        for (const CombatBody &body : Get()._combatBodies.Current().bodies) {
            if (body.soul == soul) {
                return body;
            }
        }
        return std::nullopt;
    }

    std::optional<NpcService::CombatBody> NpcService::FindCombatBodyOfSoulWuid(std::uint64_t soulWuid) {
        if (soulWuid == 0) {
            return std::nullopt;
        }
        for (const CombatBody &body : Get()._combatBodies.Current().bodies) {
            if (body.soulWuid == soulWuid) {
                return body;
            }
        }
        return std::nullopt;
    }

    bool NpcService::IsCombatEnabled(SDK::EntityId entityId) {
        if (entityId == 0) {
            return false;
        }
        for (const CombatBody &body : Get()._combatBodies.Current().bodies) {
            if (body.targetEntityId != 0 && (body.entityId == entityId || body.targetEntityId == entityId)) {
                return true;
            }
        }
        return false;
    }

    bool NpcService::IsCombatPair(SDK::EntityId first, SDK::EntityId second) {
        if (first == 0 || second == 0) {
            return false;
        }
        for (const CombatBody &body : Get()._combatBodies.Current().bodies) {
            if (body.targetEntityId != 0 && ((body.entityId == first && body.targetEntityId == second) || (body.entityId == second && body.targetEntityId == first))) {
                return true;
            }
        }
        return false;
    }

    void NpcService::PublishCombatBodies(Framework::Networking::Replication::ReplicationManager &replication) {
        CombatBodies &next = _combatBodies.Next();
        next.bodies.clear();
        replication.ForEach<Npc>([&next](Npc *npc) {
            auto *actor = npc->ResolveActor();
            if (actor == nullptr) {
                return;
            }
            CombatBody body;
            body.entityId   = actor->GetEntityId();
            body.networkId  = npc->GetNetworkID();
            body.simulating = npc->IsSimulating();
            if (auto *soul = actor->GetSoul()) {
                body.soul     = soul;
                body.soulWuid = soul->GetWuid();
            }
            if (npc->IsAttacking()) {
                auto *target        = npc->TargetActor();
                body.targetEntityId = target != nullptr ? target->GetEntityId() : 0;
                body.shooting       = body.simulating && npc->intent.bow;
            }
            next.bodies.push_back(body);
        });
        _combatBodies.Publish();
    }

    bool NpcService::IsInteractableBody(SDK::EntityId entityId) {
        return FindInteractableBody(entityId) != nullptr;
    }

    Npc *NpcService::FindInteractableBody(SDK::EntityId entityId) {
        if (entityId == 0) {
            return nullptr;
        }
        return KCDC::Common::FindReplica<Npc>([entityId](const Npc *candidate) { return candidate->GetEntityId() == entityId && candidate->IsInteractable(); });
    }

    bool NpcService::IsHeldBrainOwner(const void *npcRecord) {
        if (npcRecord != nullptr && Get()._previewMind.load() == npcRecord) {
            return true;
        }
        if (npcRecord == nullptr) {
            return false;
        }
        return KCDC::Common::FindReplica<Npc>([npcRecord](const Npc *candidate) { return candidate->GetNpcRecord() == npcRecord && !candidate->IsBrainReleased(); }) != nullptr;
    }

    const char *NpcService::ResolveName(std::uint64_t networkId) {
        auto *npc = KCDC::Common::FindReplicaById<Npc>(networkId);
        if (npc == nullptr || npc->name.empty()) {
            return nullptr;
        }
        return npc->name.c_str();
    }

    void NpcService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Bodies first: an action queued on a body that has not been made yet has
        // nothing to play on, and this is the frame that makes it.
        replication->ForEach<Npc>([](Npc *npc) {
            npc->Frame();
        });
        PublishCombatBodies(*replication);

        std::vector<Shared::RPC::NpcAction> actions;
        {
            const std::lock_guard<std::mutex> lock(_inboxMutex);
            actions.swap(_inbox);
        }
        for (const Shared::RPC::NpcAction &action : actions) {
            // Resolved by network id on the tick rather than held from the
            // packet: the replica may have gone in between.
            if (auto *npc = KCDC::Common::FindReplicaById<Npc>(action.npcNetworkId)) {
                npc->PerformAction(action);
            }
        }

        PollInteraction();
    }

    void NpcService::Reset() {
        // The session ended. Every replica goes with it and drops its own body
        // from its own teardown, so there is nothing to sweep here but the
        // messages that will never be delivered.
        {
            const std::lock_guard<std::mutex> lock(_inboxMutex);
            _inbox.clear();
        }
        _interactHeld    = false;
        _nextInteraction = 0;
        _combatBodies.Reset([](CombatBodies &slot) {
            slot.bodies.clear();
        });
    }

    void NpcService::NativeContextChanged() {
        // Every body went with the level. Unbound rather than despawned: the
        // entities are already gone, and asking the spawner to remove one would
        // reach into an entity system that has forgotten it.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Npc>([](Npc *npc) {
                npc->Unbind();
            });
            PublishCombatBodies(*replication);
        }
    }

    Npc *NpcService::FindInteractionTarget() const {
        SDK::IEntity *player = KCDC::Common::LocalPlayer();
        if (player == nullptr) {
            return nullptr;
        }

        const glm::vec3 eye      = KCDC::Common::ToGlm(player->GetWorldPos());
        const glm::quat facing   = KCDC::Common::ToGlm(player->GetWorldRotation());
        const glm::vec3 forward  = glm::normalize(facing * glm::vec3(0.0F, 1.0F, 0.0F));

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        Npc *best          = nullptr;
        float bestDistance = kInteractRange;
        replication->ForEach<Npc>([&](Npc *npc) {
            if (!npc->IsInteractable()) {
                return;
            }
            const glm::vec3 to = npc->position - eye;
            const float planar = std::sqrt(to.x * to.x + to.y * to.y);
            if (planar > bestDistance || planar < 0.01F) {
                return;
            }
            const glm::vec3 direction = glm::vec3(to.x / planar, to.y / planar, 0.0F);
            if (glm::dot(glm::vec3(forward.x, forward.y, 0.0F), direction) < kInteractFacing) {
                return;
            }
            best         = npc;
            bestDistance = planar;
        });
        return best;
    }

    void NpcService::PollInteraction() {
        // The mod's own key, declared to the game's controls rather than watched
        // for: the engine gives rebinding, conflict detection and persistence,
        // and the key is live exactly when the player has control of their
        // character.
        const bool held = KCDC::Core::NativeKeybinds::Get().IsHeld(KCDC::Core::ModKeybind::NpcInteract);
        const bool press = held && !_interactHeld;
        _interactHeld    = held;
        if (!press) {
            return;
        }
        SendInteraction(FindInteractionTarget());
    }

    void NpcService::InteractWith(SDK::EntityId entityId) {
        SendInteraction(FindInteractableBody(entityId));
    }

    void NpcService::SendInteraction(Npc *target) {
        if (target == nullptr) {
            return;
        }
        const auto now = MafiaNet::GetTime();
        if (now < _nextInteraction) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        _nextInteraction = now + kInteractCooldownMs;

        // Sent with no player id: the server fills that in from the connection,
        // so a client can only ever interact as itself. It checks the distance
        // too, against the position it already replicates.
        Shared::RPC::NpcInteract message;
        message.npcNetworkId = target->GetNetworkID();
        net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace KCDC::Features::Npc
