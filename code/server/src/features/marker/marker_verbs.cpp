#include "marker_verbs.h"

#include "features/marker/marker_scripting.h"

#include "shared/entity_filter.h"
#include "shared/features/marker/marker_rpc.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/marker_catalog.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Marker {
    namespace {
        constexpr const char *kLogger = "Marker";
    } // namespace

    void Markers::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Marker RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<Shared::RPC::MarkerCrossed>([](const Shared::RPC::MarkerCrossed &crossing, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (packet == nullptr || !crossing.IsSane() || replication == nullptr) {
                return;
            }

            const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
            auto *player                  = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            auto *marker                  = replication->GetEntity<Shared::Entities::MarkerEntity>(crossing.markerNetworkId);
            if (player == nullptr || marker == nullptr || !marker->trigger) {
                return;
            }

            if (marker->GetVirtualWorld() != player->GetVirtualWorld()) {
                return;
            }

            if (crossing.entered) {
                Scripting::Events::MarkerEntered(marker->GetNetworkID(), player->GetNetworkID());
            }
            else {
                Scripting::Events::MarkerExited(marker->GetNetworkID(), player->GetNetworkID());
            }
        });
    }

    std::vector<Shared::Entities::MarkerEntity *> Markers::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::MarkerEntity *> markers;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::MarkerEntity>([&](Shared::Entities::MarkerEntity *marker) {
                if (Shared::Entities::InWorld(marker, virtualWorld)) {
                    markers.push_back(marker);
                }
            });
        }
        return markers;
    }

    Shared::Entities::MarkerEntity *Markers::Place(std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, float spin, float bob, std::uint32_t color, bool trigger, std::uint32_t virtualWorld) {
        const Shared::Game::MarkerMaterialEntry *entry = Shared::Game::MarkerMaterialAt(materialIndex);
        const bool sane                                = Shared::Entities::MarkerEntity::IsSaneSize(size) && Shared::Entities::MarkerEntity::IsSaneHeight(height) && Shared::Entities::MarkerEntity::IsSaneMotion(spin, bob);
        if (entry == nullptr || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation) || !sane) {
            return nullptr;
        }

        // Refused here rather than left to look wrong on the clients: a decal
        // material on a mesh renders nothing and corrupts the lighting of the
        // geometry behind it.
        if (!Shared::Game::MarkerMaterialSuits(materialIndex, !Shared::Entities::IsMeshShape(shape))) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing \"{}\" as a {} marker: it is {} material", entry->name, Shared::Entities::MarkerShapeName(shape), entry->decal ? "a decal" : "a surface");
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *marker = replication->CreateEntity<Shared::Entities::MarkerEntity>();
        if (marker == nullptr) {
            return nullptr;
        }

        // Before the entity replicates, so it rides the construction snapshot
        // rather than arriving a tick after the marker was built from defaults.
        marker->materialIndex   = static_cast<std::uint16_t>(materialIndex);
        marker->shape           = static_cast<std::uint8_t>(shape);
        marker->size            = size;
        marker->height          = height;
        marker->spin            = spin;
        marker->bob             = bob;
        marker->color           = color;
        marker->trigger         = trigger;
        marker->position        = position;
        marker->rotation        = glm::normalize(rotation);
        marker->streaming.range = Shared::Entities::MarkerEntity::StreamRangeFor(size, height);
        marker->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Placed {} marker {} (\"{}\") in world {} at {:.2f}, {:.2f}, {:.2f}", Shared::Entities::MarkerShapeName(shape), marker->GetNetworkID(), entry->name, virtualWorld, position.x, position.y, position.z);

        Scripting::Events::MarkerPlaced(marker->GetNetworkID());
        return marker;
    }

    void Markers::SetExtent(Shared::Entities::MarkerEntity *marker, float size, float height) {
        if (marker == nullptr || !Shared::Entities::MarkerEntity::IsSaneSize(size) || !Shared::Entities::MarkerEntity::IsSaneHeight(height)) {
            return;
        }
        if (marker->size == size && marker->height == height) {
            return;
        }
        marker->size            = size;
        marker->height          = height;
        marker->streaming.range = Shared::Entities::MarkerEntity::StreamRangeFor(size, height);
    }

    void Markers::SetColor(Shared::Entities::MarkerEntity *marker, std::uint32_t color) {
        if (marker == nullptr || marker->color == color) {
            return;
        }
        marker->color = color;
    }

    void Markers::SetMotion(Shared::Entities::MarkerEntity *marker, float spin, float bob) {
        if (marker == nullptr || !Shared::Entities::MarkerEntity::IsSaneMotion(spin, bob)) {
            return;
        }
        marker->spin = spin;
        marker->bob  = bob;
    }

    void Markers::Destroy(Shared::Entities::MarkerEntity *marker) {
        if (marker == nullptr) {
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can read it.
        Scripting::Events::MarkerRemoved(marker->GetNetworkID());
        replication->DestroyEntity(marker);
    }

    std::size_t Markers::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::MarkerEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::MarkerEntity *marker : doomed) {
            Destroy(marker);
        }
        return doomed.size();
    }
} // namespace KCDC::Features::Marker
