#include "prop_verbs.h"

#include "core/server.h"
#include "features/prop/prop_scripting.h"

#include "shared/entity_filter.h"
#include "shared/pose_math.h"
#include "shared/streamed_assets/streamed_asset_policy.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/virtual_world.h>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kLogger = "Prop";

        float ClampScale(float scale) {
            return std::clamp(scale, Shared::Entities::PropEntity::kMinScale, Shared::Entities::PropEntity::kMaxScale);
        }
    } // namespace

    std::vector<Shared::Entities::PropEntity *> Props::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::PropEntity *> props;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::PropEntity>([&](Shared::Entities::PropEntity *prop) {
                if (Shared::Entities::InWorld(prop, virtualWorld)) {
                    props.push_back(prop);
                }
            });
        }
        return props;
    }

    bool Props::IsStreamedModel(const std::string &path) {
        return KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath(path) && Server::Get()->GetAssetPaks().HasEntry(KCDC::Shared::StreamedAssets::kLaneStream, path);
    }

    Shared::Entities::PropEntity *Props::Spawn(std::size_t modelIndex, const std::string &streamedModel, const glm::vec3 &position, const glm::quat &rotation, float scale, Shared::Entities::PropPhysics physics, std::uint32_t virtualWorld, const glm::vec3 &axisScale) {
        const bool streamed = modelIndex == Shared::Game::kNoPropModel;
        if (streamed ? !IsStreamedModel(streamedModel) : Shared::Game::PropModelAt(modelIndex) == nullptr) {
            return nullptr;
        }
        if (!Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation) || !std::isfinite(scale) || !Shared::Game::IsFinite(axisScale)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *prop = replication->CreateEntity<Shared::Entities::PropEntity>();
        if (prop == nullptr) {
            return nullptr;
        }

        // All of it before the entity replicates, so it rides the construction snapshot rather than arriving a tick after the mesh was built from the defaults.
        prop->modelIndex      = streamed ? Shared::Entities::PropEntity::kNoModel : static_cast<std::uint32_t>(modelIndex);
        prop->streamedModel   = streamed ? streamedModel : std::string();
        prop->physics         = physics;
        prop->scale           = ClampScale(scale);
        prop->axisScale       = glm::clamp(axisScale, glm::vec3(0.01F), glm::vec3(100.0F));
        prop->position        = position;
        prop->rotation        = glm::normalize(rotation);
        prop->restPosition        = prop->position;
        prop->restRotation        = prop->rotation;
        prop->motion.homePosition = prop->position;
        prop->motion.homeRotation = prop->rotation;
        prop->motion.anchor       = prop->position;
        prop->streaming.range = Shared::Entities::PropEntity::kStreamRange;
        prop->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Spawned prop {} ('{}', {}, {:.2f}x) in world {} at {:.2f}, {:.2f}, {:.2f}", prop->GetNetworkID(), ModelOf(prop)->name, Shared::Entities::PropPhysicsName(prop->physics), prop->scale, virtualWorld, position.x, position.y,
            position.z);

        const auto networkId = prop->GetNetworkID();
        Scripting::Events::PropSpawned(networkId);
        // A spawn handler may immediately destroy the entity.
        return replication->GetEntity<Shared::Entities::PropEntity>(networkId);
    }

    void Props::Destroy(Shared::Entities::PropEntity *prop) {
        if (prop == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can read it.
        Scripting::Events::PropDestroyed(prop->GetNetworkID());

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(prop);
        }
    }

    std::size_t Props::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        // Collected first: destroying while iterating would mutate the set the walk is standing in.
        const std::vector<Shared::Entities::PropEntity *> doomed = Collect(virtualWorld);
        for (auto *prop : doomed) {
            Destroy(prop);
        }

        if (!doomed.empty()) {
            Framework::Logging::GetLogger(kLogger)->info("Despawned {} prop(s)", doomed.size());
        }
        return doomed.size();
    }

    std::size_t Props::Count(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::size_t count = 0;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::PropEntity>([&](Shared::Entities::PropEntity *prop) {
                count += InWorld(prop, virtualWorld) ? 1 : 0;
            });
        }
        return count;
    }

    bool Props::SetPhysics(Shared::Entities::PropEntity *prop, Shared::Entities::PropPhysics physics) {
        if (prop == nullptr || prop->physics == physics) {
            return false;
        }

        // It is built again, at rest, where the server has it.
        prop->physics           = physics;
        prop->resting           = true;
        prop->restPosition      = prop->position;
        prop->restRotation      = prop->rotation;
        prop->motion.anchor     = prop->position;
        prop->motion.wasResting = true;
        prop->ForceState();
        // A body is the simulator's only while the prop is rigid. Delegation passes over a prop with no policy rather than taking it back, so the server does, and the former simulator then builds it again like everyone else.
        if (physics != Shared::Entities::PropPhysics::Rigid) {
            prop->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
        }
        Framework::Logging::GetLogger(kLogger)->info("Prop {} is now {}", prop->GetNetworkID(), Shared::Entities::PropPhysicsName(physics));
        return true;
    }

    bool Props::SetScale(Shared::Entities::PropEntity *prop, float scale) {
        if (prop == nullptr || !std::isfinite(scale)) {
            return false;
        }

        const float clamped = ClampScale(scale);
        if (clamped == prop->scale) {
            return false;
        }

        prop->scale = clamped;
        prop->ForceState();
        return true;
    }

    bool Props::SetMass(Shared::Entities::PropEntity *prop, float mass) {
        if (prop == nullptr || !std::isfinite(mass) || mass < 0.0F || mass > Shared::Entities::PropEntity::kMaxMass || mass == prop->mass) {
            return false;
        }

        prop->mass = mass;
        prop->ForceState();
        return true;
    }

    bool Props::Move(Shared::Entities::PropEntity *prop, Shared::RPC::PropMotion motion) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (prop == nullptr || prop->physics != Shared::Entities::PropPhysics::Rigid || replication == nullptr || net == nullptr) {
            return false;
        }

        motion.propNetworkId = prop->GetNetworkID();
        if (!motion.IsSane()) {
            return false;
        }

        // A prop nobody was near enough to simulate is offered round now rather than at the next pass, so a push straight after a spawn finds the client that will run it.
        if (prop->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            replication->Delegation().UpdateNow();
        }
        if (prop->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            Framework::Logging::GetLogger(kLogger)->debug("Prop {} motion (kind {}) not sent: no player is near enough to simulate it", motion.propNetworkId, motion.kind);
            return false;
        }

        // Ordered: a push and the sleep that follows it must land in the order they were made.
        net->SendRPC(motion, MafiaNet::ToGuid(prop->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(kLogger)->debug("Prop {} motion (kind {}) sent to its simulator", motion.propNetworkId, motion.kind);
        return true;
    }

    bool Props::Reset(Shared::Entities::PropEntity *prop) {
        if (prop == nullptr || prop->physics != Shared::Entities::PropPhysics::Rigid) {
            return false;
        }

        // A forced pose: the simulator stops its body and takes it, and everyone else follows. Resting there already, so the reset raises no `propRest`.
        prop->position          = prop->motion.homePosition;
        prop->rotation          = prop->motion.homeRotation;
        prop->velocity          = glm::vec3(0.0F);
        prop->resting           = true;
        prop->restPosition      = prop->motion.homePosition;
        prop->restRotation      = prop->motion.homeRotation;
        prop->motion.anchor     = prop->motion.homePosition;
        prop->motion.wasResting = true;
        prop->ForceState();
        return true;
    }

    void Props::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        std::vector<Shared::Entities::PropEntity *> stopped;
        replication->ForEach<Shared::Entities::PropEntity>([&](Shared::Entities::PropEntity *prop) {
            if (prop->physics != Shared::Entities::PropPhysics::Rigid || prop->resting == prop->motion.wasResting) {
                return;
            }
            prop->motion.wasResting = prop->resting;
            if (prop->resting) {
                stopped.push_back(prop);
            }
        });

        // Raised after the walk: a handler may destroy a prop, or spawn one.
        for (Shared::Entities::PropEntity *prop : stopped) {
            const std::uint64_t networkId = prop->GetNetworkID();
            // A simulator's word for where the body stopped. One that says it went farther than a body could is put back where it last rested.
            if (glm::distance(prop->restPosition, prop->motion.anchor) > kMaxRestTravel || !Shared::Game::IsUsableRotation(prop->restRotation)) {
                Framework::Logging::GetLogger(kLogger)->warn("Prop {} reported resting {:.1f} m from where it last rested; put back", networkId, glm::distance(prop->restPosition, prop->motion.anchor));
                prop->position     = prop->motion.anchor;
                prop->restPosition = prop->motion.anchor;
                prop->ForceState();
                continue;
            }
            prop->motion.anchor = prop->restPosition;
            Scripting::Events::PropRested(networkId, prop->restPosition, glm::normalize(prop->restRotation));
        }
    }

    std::optional<Shared::Game::PropModelEntry> Props::ModelOf(const Shared::Entities::PropEntity *prop) {
        if (prop == nullptr) {
            return std::nullopt;
        }
        if (prop->streamedModel.empty()) {
            const Shared::Game::PropModelEntry *model = Shared::Game::PropModelAt(prop->modelIndex);
            return model != nullptr ? std::optional<Shared::Game::PropModelEntry>(*model) : std::nullopt;
        }
        // `objects/kcdc/<resource>/.../<stem>.cgf`: grouped as `kcdc/<resource>`, named by its file.
        const std::string_view path  = prop->streamedModel;
        const std::size_t groupStart = std::string_view("objects/").size();
        const std::size_t groupEnd   = path.find('/', groupStart + std::string_view("kcdc/").size());
        const std::size_t slash      = path.rfind('/');
        return Shared::Game::PropModelEntry {path, path.substr(groupStart, groupEnd - groupStart), path.substr(slash + 1, path.size() - slash - 1 - std::string_view(".cgf").size())};
    }

    std::vector<std::string> Props::Describe(std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::size_t limit) {
        std::vector<Shared::Entities::PropEntity *> props = Collect(virtualWorld);

        // Newest first, and only here: NetworkIDs are handed out in order, so
        // the id is the age, and nothing but a listing cares about the order.
        std::sort(props.begin(), props.end(), [](Shared::Entities::PropEntity *lhs, Shared::Entities::PropEntity *rhs) {
            return lhs->GetNetworkID() > rhs->GetNetworkID();
        });

        std::vector<std::string> lines;
        for (auto *prop : props) {
            if (lines.size() >= limit) {
                break;
            }

            const std::optional<Shared::Game::PropModelEntry> model = ModelOf(prop);
            const std::string_view name                             = model ? model->name : std::string_view("<unknown model>");
            lines.push_back(fmt::format("{}: {} ({}, {:.2f}x) at {:.1f}, {:.1f}, {:.1f}", prop->GetNetworkID(), name, Shared::Entities::PropPhysicsName(prop->physics), prop->scale, prop->position.x, prop->position.y, prop->position.z));
        }
        return lines;
    }
} // namespace KCDC::Features::Prop
