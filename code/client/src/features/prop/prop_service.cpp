#include "prop_service.h"

#include "common/spawn/spawn_budget.h"

#include "core/application.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Prop {
    namespace {
        // One per script call on a prop near this player: an inbox this deep is a server pushing faster than any frame can apply.
        constexpr std::size_t kMaxIncomingMotions = 512;
    } // namespace

    void PropService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger("Prop")->error("Cannot register prop motion RPCs: network peer is unavailable during client initialization");
            return;
        }

        net->RegisterRPC<Shared::RPC::PropMotion>([](const Shared::RPC::PropMotion &motion, MafiaNet::Packet *) {
            if (motion.IsSane()) {
                KCDC::Core::gApplication->GetPropService().QueueMotion(motion);
            }
        });
    }

    void PropService::QueueMotion(const Shared::RPC::PropMotion &motion) {
        std::scoped_lock lock(_incomingMotionsMutex);
        if (_acceptMotions.load() && _incomingMotions.size() < kMaxIncomingMotions) {
            _incomingMotions.push_back({motion, MafiaNet::GetTime()});
        }
    }

    void PropService::Reset() {
        _acceptMotions.store(false);
        std::scoped_lock lock(_incomingMotionsMutex);
        _incomingMotions.clear();
        _heldMotions.clear();
    }

    void PropService::DeliverMotions() {
        {
            std::scoped_lock lock(_incomingMotionsMutex);
            _heldMotions.insert(_heldMotions.end(), _incomingMotions.begin(), _incomingMotions.end());
            _incomingMotions.clear();
        }
        if (_heldMotions.empty()) {
            return;
        }

        auto *replication        = Framework::CoreModules::GetReplication();
        const MafiaNet::Time now = MafiaNet::GetTime();
        std::erase_if(_heldMotions, [&](const IncomingMotion &incoming) {
            Prop *prop = replication != nullptr ? replication->GetEntity<Prop>(incoming.motion.propNetworkId) : nullptr;
            if (prop != nullptr) {
                prop->QueueMotion(incoming.motion, incoming.receivedAt);
                return true;
            }
            return now - incoming.receivedAt > Prop::kMotionShelfLifeMs;
        });
    }
    void PropService::NativeContextChanged() {
        ++_nativeContext;
    }

    void PropService::QueueDespawn(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        std::scoped_lock lock(_pendingDespawnsMutex);
        _pendingDespawns.push_back(entityId);
    }

    void PropService::QueueRelease(SDK::IRenderNode *brush, SDK::IPhysicalEntity *collider) {
        if (brush == nullptr && collider == nullptr) {
            return;
        }

        std::scoped_lock lock(_pendingDespawnsMutex);
        _pendingReleases.push_back({brush, collider});
    }

    void PropService::DrainDespawns() {
        // Here rather than in Update: this tick runs whether or not a session
        // is up, and a level can load either way.
        ObserveNativeContext();

        std::vector<SDK::EntityId> pending;
        std::vector<PendingRelease> releases;
        {
            std::scoped_lock lock(_pendingDespawnsMutex);
            pending.swap(_pendingDespawns);
            releases.swap(_pendingReleases);
        }

        for (const PendingRelease &release : releases) {
            KCDC::Common::Spawn::StaticBrush::DestroyReleased(release.brush, release.collider);
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }

        for (const SDK::EntityId entityId : pending) {
            entitySystem->RemoveEntity(entityId, false);
        }
    }

    void PropService::ObserveNativeContext() {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context) {
            return;
        }
        _observedContext = context;

        // Entity ids are reused, so both a queued removal and a replica's
        // binding now name whatever the new world has given the id to. A
        // queued brush points into the octree the old level took with it.
        {
            std::scoped_lock lock(_pendingDespawnsMutex);
            _pendingDespawns.clear();
            _pendingReleases.clear();
        }

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Prop>([](Prop *prop) {
                prop->Unbind();
            });
        }
    }

    void PropService::Update() {
        _acceptMotions.store(true);
        DeliverMotions();

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Brushes are built inline rather than through the spawner, so they
        // need their own ceiling: a map arriving whole on join is a few
        // frames of building, not one long one.
        KCDC::Common::Spawn::SpawnBudget budget(KCDC::Common::Spawn::kSpawnFrameBudget);
        replication->ForEach<Prop>([&budget](Prop *prop) {
            prop->Frame(budget);
        });
    }
} // namespace KCDC::Features::Prop
