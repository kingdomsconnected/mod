#include "npc_verbs.h"
#include "features/door/door_service.h"
#include "features/patrol/patrol_scripting.h"
#include "npc_inventory_module.h"

#include "core/server.h"
#include "features/animation/animation_verbs.h"
#include "features/navigation/navigation_service.h"
#include "features/npc/npc_scripting.h"
#include "features/player/player_combatant.h"

#include "shared/entity_filter.h"
#include "shared/features/npc/npc_rpc.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/item_classes.h"
#include "shared/game/npc_roles.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>
#include <mafianet/virtual_world.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace KCDC::Features::Npc {
    namespace {
        constexpr const char *kLogger = "Npc";

        Framework::Networking::Replication::ReplicationManager *Replication() {
            return Framework::CoreModules::GetReplication();
        }

        /** Pushes every server-authored field to the client simulating this NPC. */
        void Publish(Shared::Entities::NpcEntity *npc) {
            npc->ForceState();
        }

        /**
         * The packed `equipment` string for a list of dashed item class GUIDs.
         *
         * All or nothing: a GUID that does not parse, or names no item class
         * this build ships, refuses the whole list and is logged against
         * `who`, so a scripter sees which piece was wrong rather than a body
         * missing one.
         */
        bool PackEquipment(const std::string &who, const std::vector<std::string> &guids, std::string &out) {
            std::string packed;
            packed.reserve(guids.size() * 32);
            bool accepted = true;
            for (const std::string &guid : guids) {
                std::uint64_t low  = 0;
                std::uint64_t high = 0;
                if (!Shared::Game::CryGuidFromText(guid, low, high)) {
                    Framework::Logging::GetLogger(kLogger)->warn("{} was given '{}', which is not a dashed item class GUID", who, guid);
                    accepted = false;
                    continue;
                }
                if (Shared::Game::FindItemClass(guid) == nullptr) {
                    Framework::Logging::GetLogger(kLogger)->warn("{} was given '{}', which names no item class this build has", who, guid);
                    accepted = false;
                    continue;
                }
                Shared::RPC::ItemClassId {low, high}.AppendPackedText(packed);
            }
            if (!accepted) {
                return false;
            }
            out = packed;
            return true;
        }

        /** Who a log line is about. */
        std::string Describe(Shared::Entities::NpcEntity *npc) {
            return "NPC " + std::to_string(npc->GetNetworkID());
        }

        /** The ground-plane distance between two points; KCD2 is Z-up. */
        float GroundDistance(const glm::vec3 &a, const glm::vec3 &b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        /** Starts a revision carrying what the order wants simulated now, and forces it. */
        void Republish(Shared::Entities::NpcEntity *npc, NpcOrder &order) {
            npc->intent         = order.SimulatorIntent(npc->position);
            npc->intentRevision = Shared::Entities::NpcEntity::AdvanceRevision(npc->intentRevision);
            order.Published(npc->intentRevision);
            Publish(npc);
        }

        /** Publishes an order that can run now, or queues the plan it is waiting for. */
        void Settle(Shared::Entities::NpcEntity *npc, NpcOrder &order) {
            if (order.NeedsPlan()) {
                NpcOrders::Get().Track(npc->GetNetworkID());
                return;
            }
            Republish(npc, order);
        }

        /** Attack orders only name a live replicated combatant in the same world. */
        bool CanAttack(Shared::Entities::NpcEntity *npc, std::uint64_t targetId) {
            if (npc == nullptr || !npc->alive || targetId == npc->GetNetworkID()) {
                return false;
            }
            const auto target = KCDC::Features::Player::Combatant::Find(targetId);
            if (!target || !MafiaNet::VirtualWorldsCanSee(npc->GetVirtualWorld(), target.entity->GetVirtualWorld()) || glm::distance(npc->position, target.Position()) > Shared::Game::NpcIntent::kMaxAttackRange) {
                return false;
            }
            return target.Ready() && target.Valid() && target.CanAct();
        }

        /** What an order is told about its NPC this tick. */
        NpcOrderContext ContextFor(Shared::Entities::NpcEntity *npc, const NpcOrder &order, MafiaNet::Time now) {
            NpcOrderContext context;
            context.now            = now;
            context.position       = npc->position;
            context.hasSimulator   = npc->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID;
            context.active         = npc->alive && !Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Frozen);
            context.meshReady      = Server::Get()->GetNavigation().IsReady();
            context.reportStatus   = npc->intentStatus;
            context.reportRevision = npc->statusRevision;
            context.intentRevision = npc->intentRevision;
            if (npc->CanOpenDoors()) {
                context.doorPassage = [npc, now](std::uint64_t guid) { return Server::Get()->GetDoors().PreparePassage(guid, npc->GetVirtualWorld(), npc->position, now); };
            }
            if (order.Goal().kind == Shared::Game::NpcIntentKind::Follow || (order.Goal().kind == Shared::Game::NpcIntentKind::Attack && CanAttack(npc, order.Goal().targetNetworkId))) {
                auto *replication = Replication();
                auto *target      = replication != nullptr ? replication->GetEntityByNetworkID(order.Goal().targetNetworkId) : nullptr;
                if (target != nullptr && MafiaNet::VirtualWorldsCanSee(npc->GetVirtualWorld(), target->GetVirtualWorld())) {
                    context.followTarget = target->position;
                }
            }
            return context;
        }

        /** The player entity behind a NetworkID, or null. */
        Shared::Entities::HumanEntity *Player(std::uint64_t networkId) {
            auto *replication = Replication();
            return replication != nullptr && networkId != 0 ? replication->GetEntity<Shared::Entities::HumanEntity>(networkId) : nullptr;
        }

        /** The NetworkID of the avatar belonging to a connection, or 0. */
        std::uint64_t PlayerIdOf(MafiaNet::PeerGuid guid) {
            auto *replication = Replication();
            if (replication == nullptr || guid == MafiaNet::UNASSIGNED_PEER_GUID) {
                return 0;
            }
            auto *viewer = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            return viewer != nullptr ? viewer->GetNetworkID() : 0;
        }

        /** Applies the shared half of Spawn and Adopt. */
        void ApplyParams(Shared::Entities::NpcEntity *npc, const SpawnParams &params) {
            npc->name       = params.name;
            npc->appearance = params.appearance;
            npc->factionId  = params.factionId;
            npc->flags      = params.flags;
            npc->locomotion = params.locomotion;
            npc->maxHealth  = params.maxHealth > 0.0F ? params.maxHealth : 100.0F;
            npc->health     = std::clamp(params.health, 0.0F, npc->maxHealth);
            npc->alive      = npc->health > 0.0F;

            if (!params.wearing.empty()) {
                std::string packed;
                if (PackEquipment(Describe(npc), params.wearing, packed)) {
                    npc->equipment = packed;
                }
                else {
                    Framework::Logging::GetLogger(kLogger)->warn("NPC {} keeps its own clothes: its item list was refused", npc->GetNetworkID());
                }
            }
            else if (!params.outfit.empty()) {
                if (const Shared::Game::ClothingPresetEntry *preset = Shared::Game::FindClothingPreset(params.outfit)) {
                    std::vector<std::string> guids;
                    guids.reserve(preset->itemGuids.size());
                    for (std::string_view guid : preset->itemGuids) {
                        guids.emplace_back(guid);
                    }
                    std::string packed;
                    if (PackEquipment(Describe(npc), guids, packed)) {
                        npc->equipment = packed;
                    }
                }
                else {
                    Framework::Logging::GetLogger(kLogger)->warn("NPC {} names clothing preset '{}', which this build does not have", npc->GetNetworkID(), params.outfit);
                }
            }

            npc->position        = params.position;
            npc->rotation        = glm::normalize(params.rotation);
            npc->transformReady  = true;
            npc->poseEpoch       = npc->spawnEpoch;
            npc->streaming.range = Shared::Entities::NpcEntity::kStreamRange;
            npc->SetVirtualWorld(params.virtualWorld == 0xFFFFFFFFU ? MafiaNet::VIRTUAL_WORLD_GLOBAL : static_cast<MafiaNet::VirtualWorldId>(params.virtualWorld));
        }
    } // namespace

    void Npcs::Install() {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Replication();
        if (net == nullptr || replication == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("NPC installation has no network peer");
            return;
        }

        SetSimulationBudget(kDefaultSimulationBudget);

        // The framework elects the simulator; what belongs here is only what the
        // game makes of the result: telling every observer who is running the
        // body, and telling resources.
        replication->Delegation().AddSimulatorChangedHandler([](Framework::Networking::Replication::NetworkEntity *entity, MafiaNet::PeerGuid, MafiaNet::PeerGuid current, Framework::Networking::Replication::DelegationChange) {
            auto *npc = dynamic_cast<Shared::Entities::NpcEntity *>(entity);
            if (npc == nullptr) {
                return;
            }
            // The entity's own OnSimulatorChanged has already bumped the
            // intent revision. The new simulator is handed what the order
            // wants now -- a finished move as a hold where it stands, so an
            // order that has ended is neither walked again nor reported
            // again -- unless a route is still being planned, in which case
            // the plan publishes it.
            NpcOrder &order = NpcOrders::Get().For(npc->GetNetworkID());
            order.OnSimulatorChanged(MafiaNet::GetTime());
            if (!order.NeedsPlan()) {
                npc->intent = order.SimulatorIntent(npc->position);
                order.Published(npc->intentRevision);
            }
            // Written after the handover rather than inside it: the field is
            // a player NetworkID, and only the server can map a connection
            // to one.
            npc->simulatorNetworkId = PlayerIdOf(current);
            Publish(npc);
            KCDC::Features::Animation::Animations::SimulatorChanged(npc);
            Scripting::Events::NpcSimulatorChanged(npc->GetNetworkID(), npc->simulatorNetworkId);
        });

        net->RegisterRPC<Shared::RPC::NpcInteract>([](const Shared::RPC::NpcInteract &message, MafiaNet::Packet *packet) {
            if (packet == nullptr || !message.IsSane()) {
                return;
            }
            auto *npc = Find(message.npcNetworkId);
            if (npc == nullptr || !Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Interactable)) {
                return;
            }
            // Who, from the connection rather than from the message: a client
            // can only ever interact as itself.
            const std::uint64_t playerId = PlayerIdOf(MafiaNet::ToPeerGuid(packet->guid));
            auto *player                 = Player(playerId);
            if (player == nullptr || !MafiaNet::VirtualWorldsCanSee(npc->GetVirtualWorld(), player->GetVirtualWorld())) {
                return;
            }

            // Where, against the server's own record rather than the claim: the
            // same arrangement that keeps a marker crossing honest.
            constexpr float kMaxInteractDistance = 4.0F;
            if (GroundDistance(npc->position, player->position) > kMaxInteractDistance) {
                return;
            }

            Scripting::Events::NpcInteracted(npc->GetNetworkID(), playerId);
        });

        net->RegisterRPC<Shared::RPC::NpcDamage>([](const Shared::RPC::NpcDamage &message, MafiaNet::Packet *packet) {
            if (packet == nullptr || !message.IsSane()) {
                return;
            }
            auto *npc = Find(message.npcNetworkId);
            if (npc == nullptr) {
                return;
            }
            if (!npc->alive || npc->spawnEpoch != message.victimEpoch) {
                return;
            }
            const std::uint64_t attackerId = PlayerIdOf(MafiaNet::ToPeerGuid(packet->guid));
            if (attackerId == 0) {
                return;
            }
            Damage(npc, attackerId, message.amount);
        });
    }

    void Npcs::SetSimulationBudget(std::uint32_t perClient) {
        if (auto *replication = Replication()) {
            replication->Delegation().SetClientCapacity(perClient);
        }
    }

    Shared::Entities::NpcEntity *Npcs::Spawn(const SpawnParams &params) {
        auto *replication = Replication();
        if (replication == nullptr) {
            return nullptr;
        }
        if (!Shared::Game::IsFinite(params.position) || !Shared::Game::IsUsableRotation(params.rotation)) {
            return nullptr;
        }

        std::string soulGuid   = params.soul;
        std::string actorClass = params.actorClass;
        std::string outfit     = params.outfit;
        if (const auto *animal = Shared::Game::FindAnimal(params.soul)) {
            if (!actorClass.empty() && actorClass != animal->actorClass) {
                return nullptr;
            }
            soulGuid   = animal->soulGuid;
            actorClass = animal->actorClass;
        }
        if (const Shared::Game::NpcRoleEntry *role = Shared::Game::FindNpcRole(params.soul)) {
            soulGuid = std::string(role->soulGuid);
            if (actorClass.empty()) {
                actorClass = std::string(role->actorClass);
            }
            if (outfit.empty()) {
                outfit = std::string(role->clothingPresetGuid);
            }
        }
        if (Shared::Game::IsAnimalClass(actorClass) && (!outfit.empty() || !params.wearing.empty() || !params.appearance.IsEmpty())) {
            return nullptr;
        }
        if (soulGuid.empty() && !Shared::Game::IsAnimalClass(actorClass)) {
            soulGuid = Shared::Entities::NpcEntity::kDefaultSoulGuid;
        }

        auto *npc = replication->CreateEntity<Shared::Entities::NpcEntity>();
        if (npc == nullptr) {
            return nullptr;
        }

        // Before the entity replicates, so identity rides the construction
        // snapshot and a late joiner spawns the same body everyone else sees
        // rather than a default one corrected a tick later.
        npc->soulGuid   = soulGuid;
        npc->actorClass = actorClass;
        npc->levelGuid  = 0;

        SpawnParams applied = params;
        applied.outfit      = outfit;
        ApplyParams(npc, applied);
        NpcInventoryModule::Get().Create(*npc);

        Framework::Logging::GetLogger(kLogger)->info("Spawned NPC {} (soul '{}', class '{}', name '{}') at {:.2f}, {:.2f}, {:.2f}", npc->GetNetworkID(), npc->soulGuid, npc->SpawnClass(), npc->name, npc->position.x, npc->position.y, npc->position.z);

        const auto networkId = npc->GetNetworkID();
        Scripting::Events::NpcSpawned(networkId);
        // Spawn listeners may remove the new body synchronously.
        return replication->GetEntity<Shared::Entities::NpcEntity>(networkId);
    }

    Shared::Entities::NpcEntity *Npcs::Adopt(std::uint64_t levelGuid, const SpawnParams &params) {
        auto *replication = Replication();
        if (replication == nullptr || levelGuid == 0) {
            return nullptr;
        }

        // One NPC per level body: adopting the same guid twice would leave two
        // entities driving one body, each undoing the other.
        Shared::Entities::NpcEntity *existing = nullptr;
        replication->ForEach<Shared::Entities::NpcEntity>([&](Shared::Entities::NpcEntity *candidate) {
            if (existing == nullptr && candidate->levelGuid == levelGuid) {
                existing = candidate;
            }
        });
        if (existing != nullptr) {
            return existing;
        }

        auto *npc = replication->CreateEntity<Shared::Entities::NpcEntity>();
        if (npc == nullptr) {
            return nullptr;
        }

        npc->levelGuid  = levelGuid;
        npc->soulGuid.clear();
        npc->actorClass = params.actorClass;
        ApplyParams(npc, params);
        NpcInventoryModule::Get().Create(*npc);

        // An adopted body is already standing somewhere; until a simulator says
        // where, the server's own guess is only as good as what the caller
        // passed. A caller that passed nothing gets a body that streams from the
        // origin until the first report, so say so rather than hide it.
        if (!Shared::Game::IsFinite(params.position) || params.position == glm::vec3(0.0F)) {
            Framework::Logging::GetLogger(kLogger)->warn("Adopted level body {} without a position; it will stream from the origin until a client reports one", levelGuid);
        }

        Framework::Logging::GetLogger(kLogger)->info("Adopted level body {} as NPC {}", levelGuid, npc->GetNetworkID());
        const auto networkId = npc->GetNetworkID();
        Scripting::Events::NpcSpawned(networkId);
        // Spawn listeners may remove the new body synchronously.
        return replication->GetEntity<Shared::Entities::NpcEntity>(networkId);
    }

    void Npcs::Destroy(Shared::Entities::NpcEntity *npc) {
        auto *replication = Replication();
        if (npc == nullptr || replication == nullptr) {
            return;
        }
        const std::uint64_t networkId = npc->GetNetworkID();
        // While it can still be read, as every other feature here does.
        Scripting::Events::NpcDestroyed(networkId);
        NpcOrders::Get().Erase(networkId);
        NpcInventoryModule::Get().Destroy(networkId);
        replication->DestroyEntity(npc);
    }

    void Npcs::SetName(Shared::Entities::NpcEntity *npc, const std::string &name) {
        if (npc == nullptr || npc->name == name) {
            return;
        }
        npc->name = name;
        Publish(npc);
    }

    void Npcs::SetAppearance(Shared::Entities::NpcEntity *npc, const Shared::Game::CharacterAppearance &appearance) {
        if (npc == nullptr || npc->IsAnimal() || npc->appearance == appearance) {
            return;
        }
        npc->appearance = appearance;
        Publish(npc);
    }

    bool Npcs::SetOutfit(Shared::Entities::NpcEntity *npc, const std::string &clothingPresetGuid) {
        if (npc == nullptr || npc->IsAnimal()) {
            return false;
        }
        const Shared::Game::ClothingPresetEntry *preset = Shared::Game::FindClothingPreset(clothingPresetGuid);
        if (preset == nullptr) {
            return false;
        }
        std::vector<std::string> guids;
        guids.reserve(preset->itemGuids.size());
        for (std::string_view guid : preset->itemGuids) {
            guids.emplace_back(guid);
        }
        return SetWearing(npc, guids);
    }

    bool Npcs::SetWearing(Shared::Entities::NpcEntity *npc, const std::vector<std::string> &itemClassGuids) {
        std::string packed;
        if (npc == nullptr || npc->IsAnimal() || !PackEquipment(Describe(npc), itemClassGuids, packed)) {
            return false;
        }
        return NpcInventoryModule::Get().Dress(*npc, itemClassGuids);
    }

    std::vector<std::string> Npcs::GetWearing(const Shared::Entities::NpcEntity *npc) {
        std::vector<std::string> guids;
        if (npc == nullptr) {
            return guids;
        }
        guids.reserve(npc->equipment.size() / 32);
        for (std::size_t offset = 0; offset + 32 <= npc->equipment.size(); offset += 32) {
            const std::uint64_t low                                       = std::strtoull(npc->equipment.substr(offset, 16).c_str(), nullptr, 16);
            const std::uint64_t high                                      = std::strtoull(npc->equipment.substr(offset + 16, 16).c_str(), nullptr, 16);
            const std::array<char, Shared::Game::kCryGuidTextLength> text = Shared::Game::CryGuidToText(low, high);
            guids.emplace_back(text.data(), text.size());
        }
        return guids;
    }

    void Npcs::SetFlags(Shared::Entities::NpcEntity *npc, Shared::Game::NpcFlags flags) {
        if (npc == nullptr || npc->flags == flags) {
            return;
        }
        npc->flags = flags;
        Publish(npc);
    }

    void Npcs::SetNametag(Shared::Entities::NpcEntity *npc, const Framework::Networking::Replication::NametagState &nametag) {
        if (npc == nullptr || (npc->nametag.components == nametag.components && npc->nametag.color == nametag.color && npc->nametag.text == nametag.text)) {
            return;
        }
        npc->nametag = nametag;
        Publish(npc);
    }

    void Npcs::SetFaction(Shared::Entities::NpcEntity *npc, std::uint8_t factionId) {
        if (npc == nullptr || npc->factionId == factionId) {
            return;
        }
        npc->factionId = factionId;
        Publish(npc);
    }

    void Npcs::SetLocomotion(Shared::Entities::NpcEntity *npc, Shared::Game::NpcLocomotion locomotion) {
        if (npc == nullptr || npc->locomotion == locomotion) {
            return;
        }
        npc->locomotion = locomotion;
        Publish(npc);
    }

    void Npcs::SetHealth(Shared::Entities::NpcEntity *npc, float health) {
        if (npc == nullptr || !std::isfinite(health)) {
            return;
        }
        const float bounded = std::clamp(health, 0.0F, npc->maxHealth);
        if (npc->health == bounded) {
            return;
        }
        const bool revived = !npc->alive && bounded > 0.0F;
        npc->health = bounded;
        npc->alive  = bounded > 0.0F;
        if (revived) {
            npc->spawnEpoch = Shared::Entities::NpcEntity::AdvanceRevision(npc->spawnEpoch);
            npc->poseEpoch  = npc->spawnEpoch;
            NpcInventoryModule::Get().Revived(*npc);
        }
        Publish(npc);
    }

    void Npcs::Damage(Shared::Entities::NpcEntity *npc, std::uint64_t attackerNetworkId, float amount) {
        if (npc == nullptr || !npc->alive || !std::isfinite(amount) || amount <= 0.0F) {
            return;
        }
        if (Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Invulnerable)) {
            return;
        }

        const float before = npc->health;
        npc->health        = std::max(0.0F, before - amount);
        const bool killed  = npc->health <= 0.0F;
        if (killed) {
            npc->alive = false;
            // A corpse has no orders. Cleared through the intent verb so the
            // revision is bumped and the simulator stops walking a dead body.
            ClearRoute(npc);
        }
        Publish(npc);

        Scripting::Events::NpcDamaged(npc->GetNetworkID(), attackerNetworkId, before - npc->health);
        if (killed) {
            Hold(npc);
            Framework::Logging::GetLogger(kLogger)->info("NPC {} was killed by actor {}", npc->GetNetworkID(), attackerNetworkId);
            Scripting::Events::NpcDied(npc->GetNetworkID(), attackerNetworkId);
        }
    }

    void Npcs::Kill(Shared::Entities::NpcEntity *npc, std::uint64_t attackerNetworkId) {
        if (npc == nullptr || !npc->alive) {
            return;
        }
        // Through Damage, so the ledger, the events and the order cancellation
        // are one path rather than two that can drift.
        const bool invulnerable = Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Invulnerable);
        if (invulnerable) {
            npc->flags = static_cast<Shared::Game::NpcFlags>(static_cast<std::uint16_t>(npc->flags) & ~static_cast<std::uint16_t>(Shared::Game::NpcFlags::Invulnerable));
        }
        Damage(npc, attackerNetworkId, npc->maxHealth + 1.0F);
        if (invulnerable) {
            npc->flags |= Shared::Game::NpcFlags::Invulnerable;
            Publish(npc);
        }
    }

    void Npcs::Revive(Shared::Entities::NpcEntity *npc) {
        if (npc == nullptr || npc->alive) {
            return;
        }
        npc->alive  = true;
        npc->health = npc->maxHealth;
        // A new body: the old one is a corpse on every client, and the epoch is
        // what tells them to make another rather than animate that one.
        npc->spawnEpoch = Shared::Entities::NpcEntity::AdvanceRevision(npc->spawnEpoch);
        npc->poseEpoch  = npc->spawnEpoch;
        NpcInventoryModule::Get().Revived(*npc);
        Publish(npc);
        Scripting::Events::NpcRevived(npc->GetNetworkID());
    }

    void Npcs::Teleport(Shared::Entities::NpcEntity *npc, const glm::vec3 &position, const glm::quat &rotation) {
        if (npc == nullptr || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return;
        }
        npc->position       = position;
        npc->rotation       = glm::normalize(rotation);
        npc->velocity       = glm::vec3(0.0F);
        npc->transformReady = true;
        // What tells the simulator this forced state is a move, not an order.
        npc->teleportSequence = Shared::Entities::NpcEntity::AdvanceRevision(npc->teleportSequence);
        // A server route is replanned from the new position.
        NpcOrders::Get().For(npc->GetNetworkID()).OnTeleported();
        NpcOrders::Get().Track(npc->GetNetworkID());

        // Carried to the simulator on the forced-state channel, which the base
        // class fills with the transform for exactly this, and fenced by the
        // epoch that channel bumps: a pose the simulator had already sent can no
        // longer put the body back. The body itself is not rebuilt --
        // `spawnEpoch` means make a new body, and a teleport is the same body
        // somewhere else.
        Publish(npc);
    }

    void Npcs::RequireMesh(Pathfinding pathfinding) {
        if (pathfinding == Pathfinding::Server && !Server::Get()->GetNavigation().IsReady()) {
            throw std::runtime_error("server pathfinding requires a loaded navmesh; check Navigation.ready and supply files/<level>/recast.pak or set mod.navmesh in server.json");
        }
    }

    Navigation::QueryOptions Npcs::RouteOptions(float searchHeight) {
        return Server::Get()->GetNavigation().Options(Navigation::DoorPolicy::Unlocked, NpcOrders::kSearchRadius, searchHeight, NpcOrders::kSearchNodes);
    }

    void Npcs::SetIntent(Shared::Entities::NpcEntity *npc, const Shared::Game::NpcIntent &intent, Pathfinding pathfinding) {
        if (npc == nullptr || !intent.IsSane()) {
            return;
        }
        // Refused before the old order is touched, so it is left intact.
        RequireMesh(pathfinding);
        // Any order cancels the patrol. Without this a `hold` would be
        // reported as reached, the route would step on that report, and the
        // NPC would walk off again a moment after being told to stand still.
        NpcOrder &order = NpcOrders::Get().For(npc->GetNetworkID());
        order.Command(intent, pathfinding, ContextFor(npc, order, MafiaNet::GetTime()));
        Settle(npc, order);
    }

    void Npcs::Hold(Shared::Entities::NpcEntity *npc) {
        Shared::Game::NpcIntent intent;
        intent.kind = Shared::Game::NpcIntentKind::Hold;
        SetIntent(npc, intent);
    }

    Shared::Game::NpcIntentKind Npcs::GetIntentKind(Shared::Entities::NpcEntity *npc) {
        const NpcOrder *order = NpcOrders::Get().Find(npc->GetNetworkID());
        return order != nullptr ? order->Goal().kind : npc->intent.kind;
    }

    Shared::Game::NpcIntentStatus Npcs::GetStatus(Shared::Entities::NpcEntity *npc) {
        const NpcOrder *order = NpcOrders::Get().Find(npc->GetNetworkID());
        return order != nullptr ? order->Status(ContextFor(npc, *order, MafiaNet::GetTime())) : Shared::Game::NpcIntentStatus::Idle;
    }

    bool Npcs::MoveTo(Shared::Entities::NpcEntity *npc, const glm::vec3 &point, Shared::Game::NpcSpeed speed, float arriveRadius, Pathfinding pathfinding) {
        Shared::Game::NpcIntent intent;
        intent.kind   = Shared::Game::NpcIntentKind::MoveTo;
        intent.point  = point;
        intent.speed  = speed;
        intent.radius = arriveRadius > 0.0F ? arriveRadius : 1.5F;
        if (npc == nullptr || !intent.IsSane()) {
            return false;
        }
        SetIntent(npc, intent, pathfinding);
        return true;
    }

    void Npcs::Follow(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, Shared::Game::NpcSpeed speed, float distance) {
        Shared::Game::NpcIntent intent;
        intent.kind             = Shared::Game::NpcIntentKind::Follow;
        intent.targetNetworkId  = targetNetworkId;
        intent.speed            = speed;
        intent.radius           = distance > 0.0F ? distance : 3.0F;
        SetIntent(npc, intent);
    }

    bool Npcs::Attack(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, bool bow) {
        if (!CanAttack(npc, targetNetworkId) || (npc->IsAnimal() && (bow || !Shared::Game::IsPredatorClass(npc->actorClass)))) {
            return false;
        }
        Shared::Game::NpcIntent intent;
        intent.kind            = Shared::Game::NpcIntentKind::Attack;
        intent.targetNetworkId = targetNetworkId;
        intent.speed           = Shared::Game::NpcSpeed::Run;
        intent.radius          = bow ? Shared::Game::NpcIntent::kBowAttackRadius : Shared::Game::NpcIntent::kMeleeAttackRadius;
        intent.bow             = bow;
        SetIntent(npc, intent);
        return true;
    }

    bool Npcs::Flee(Shared::Entities::NpcEntity *npc, const glm::vec3 &from, std::uint64_t threatNetworkId, Shared::Game::NpcSpeed speed, float distance) {
        Shared::Game::NpcIntent intent;
        intent.kind            = Shared::Game::NpcIntentKind::Flee;
        intent.point           = from;
        intent.targetNetworkId = threatNetworkId;
        intent.speed           = speed;
        intent.radius          = distance > 0.0F ? distance : 25.0F;
        if (threatNetworkId != 0) {
            // Where the threat stands now, which is what a body that steers
            // away runs from, and what an animal's escape runs from while its
            // simulator cannot see the threat.
            auto *replication = Replication();
            auto *threat      = replication != nullptr && threatNetworkId != npc->GetNetworkID() ? replication->GetEntityByNetworkID(threatNetworkId) : nullptr;
            if (threat == nullptr) {
                return false;
            }
            intent.point = threat->position;
        }
        SetIntent(npc, intent);
        return true;
    }

    void Npcs::LookAt(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, const glm::vec3 &point) {
        Shared::Game::NpcIntent intent;
        intent.kind            = Shared::Game::NpcIntentKind::LookAt;
        intent.targetNetworkId = targetNetworkId;
        intent.point           = point;
        SetIntent(npc, intent);
    }

    bool Npcs::Patrol(Shared::Entities::NpcEntity *npc, const std::vector<glm::vec3> &route, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds, Pathfinding pathfinding) {
        if (npc == nullptr || !NpcOrder::IsUsableRoute(route, speed, waitSeconds)) {
            return false;
        }
        RequireMesh(pathfinding);
        NpcOrder &order = NpcOrders::Get().For(npc->GetNetworkID());
        order.Patrol(route, loop, speed, waitSeconds, pathfinding, ContextFor(npc, order, MafiaNet::GetTime()));
        Settle(npc, order);
        return true;
    }

    void Npcs::ClearRoute(Shared::Entities::NpcEntity *npc) {
        if (npc == nullptr) {
            return;
        }
        NpcOrders::Get().For(npc->GetNetworkID()).ClearRoute();
    }

    bool Npcs::Patrol(Shared::Entities::NpcEntity *npc, const Shared::Game::PatrolRouteDefinition &route, Pathfinding pathfinding) {
        if (npc == nullptr || !route.IsSane(!route.id.empty())) {
            return false;
        }
        RequireMesh(pathfinding);
        NpcOrder &order = NpcOrders::Get().For(npc->GetNetworkID());
        order.Patrol(route, pathfinding, ContextFor(npc, order, MafiaNet::GetTime()));
        Settle(npc, order);
        return true;
    }

    bool Npcs::PlayAnimation(Shared::Entities::NpcEntity *npc, std::uint16_t animationId) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (npc == nullptr || net == nullptr || !npc->alive) {
            return false;
        }
        Shared::RPC::NpcAction action;
        action.npcNetworkId = npc->GetNetworkID();
        action.kind         = Shared::RPC::NpcAction::Emote;
        action.animationId  = animationId;
        action.sequence     = NpcOrders::Get().For(npc->GetNetworkID()).NextActionSequence();
        net->BroadcastRPC(action);
        return true;
    }

    bool Npcs::Speak(Shared::Entities::NpcEntity *npc, const std::string &text) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (npc == nullptr || net == nullptr || text.empty() || text.size() > Shared::RPC::NpcAction::kMaxTextLength) {
            return false;
        }
        Shared::RPC::NpcAction action;
        action.npcNetworkId = npc->GetNetworkID();
        action.kind         = Shared::RPC::NpcAction::Speak;
        action.sequence     = NpcOrders::Get().For(npc->GetNetworkID()).NextActionSequence();
        action.text         = text;
        net->BroadcastRPC(action);
        return true;
    }

    void Npcs::PinSimulator(Shared::Entities::NpcEntity *npc, std::uint64_t playerNetworkId) {
        auto *replication = Replication();
        if (npc == nullptr || replication == nullptr) {
            return;
        }
        if (playerNetworkId == 0) {
            replication->Delegation().Unpin(npc);
            return;
        }
        auto *player = Player(playerNetworkId);
        if (player == nullptr) {
            return;
        }
        replication->Delegation().Pin(npc, player->ownerGUID);
    }

    Shared::Entities::NpcEntity *Npcs::Find(std::uint64_t networkId) {
        auto *replication = Replication();
        return replication != nullptr && networkId != 0 ? replication->GetEntity<Shared::Entities::NpcEntity>(networkId) : nullptr;
    }

    std::vector<Shared::Entities::NpcEntity *> Npcs::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::NpcEntity *> npcs;
        if (auto *replication = Replication()) {
            replication->ForEach<Shared::Entities::NpcEntity>([&](Shared::Entities::NpcEntity *npc) {
                if (Shared::Entities::InWorld(npc, virtualWorld)) {
                    npcs.push_back(npc);
                }
            });
        }
        return npcs;
    }

    std::size_t Npcs::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::NpcEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::NpcEntity *npc : doomed) {
            Destroy(npc);
        }
        return doomed.size();
    }

    void Npcs::Update() {
        auto *replication = Replication();
        if (replication == nullptr) {
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        NpcOrders &orders        = NpcOrders::Get();

        // Callbacks may remove this or another NPC, or create a new one.
        // Iterate identities captured before dispatch, resolving each in turn.
        std::vector<std::uint64_t> ids;
        orders.CollectIds(ids);
        for (const auto networkId : ids) {
            auto *npc = Find(networkId);
            if (!npc) {
                continue;
            }
            NpcOrder &order               = orders.For(networkId);
            const NpcOrderContext context = ContextFor(npc, order, now);

            const NpcOrderStep observed = order.Observe(context);
            const std::uint64_t generation = order.Generation();
            Scripting::EmitPatrolEvents(networkId, order.TakePatrolEvents());
            if (Find(networkId) != npc || order.Generation() != generation) {
                continue;
            }
            if (observed.announce) {
                const std::uint32_t revision = npc->intentRevision;
                Scripting::Events::NpcIntentFinished(networkId, Shared::Game::NpcIntentStatusName(*observed.announce));
                // A handler that removed the NPC or gave it a new order has
                // already settled what happens next.
                if (Find(networkId) != npc || order.Generation() != generation || npc->intentRevision != revision || order.NeedsPlan()) {
                    continue;
                }
            }

            const glm::vec3 before      = npc->position;
            const NpcOrderStep advanced = order.Advance(context, npc->position);
            if (!context.hasSimulator && npc->position != before) {
                npc->transformReady = true;
            }
            if (order.NeedsPlan()) {
                orders.Track(networkId);
            }
            else if (observed.republish || advanced.republish) {
                Republish(npc, order);
            }
            Scripting::EmitPatrolEvents(networkId, order.TakePatrolEvents());
        }

        // After every order has been read, so a route planned here resolves
        // on the next tick rather than this one.
        const auto &navigation = Server::Get()->GetNavigation();
        orders.RunPlans(NpcOrders::kPlansPerTick, navigation.IsReady(), [&](std::uint64_t networkId, NpcOrder &order) {
            auto *npc = Find(networkId);
            if (npc == nullptr) {
                return false;
            }
            const bool humanoid                                                   = npc->CanOpenDoors();
            const fu2::function<bool(const Navigation::EntityLink &) const> doors = [npc, humanoid](const Navigation::EntityLink &link) {
                return link.kind != Navigation::TileCodec::kDoorLinkKind || (humanoid && Server::Get()->GetDoors().CanNavigate(link.entityGuid, npc->GetVirtualWorld()));
            };
            auto options     = RouteOptions(order.Navigation().SearchHeight());
            options.linkGate = &doors;
            order.Plan(navigation.GetMesh(), options, npc->position);
            order.Advance(ContextFor(npc, order, now), npc->position);
            Republish(npc, order);
            return true;
        });
    }
} // namespace KCDC::Features::Npc
