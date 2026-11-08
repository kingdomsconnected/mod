#include "horse_navigation.h"

#include "core/server.h"
#include "horse_scripting.h"

#include <cmath>
#include <core_modules.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Horse {
    HorseNavigation &HorseNavigation::Get() {
        static HorseNavigation service;
        return service;
    }

    Npc::NpcOrderContext HorseNavigation::Context(Shared::Entities::HorseEntity *horse) {
        Npc::NpcOrderContext context;
        context.now            = MafiaNet::GetTime();
        context.position       = horse->position;
        context.hasSimulator   = horse->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID;
        context.active         = horse->alive && horse->riderNetworkId == 0;
        context.meshReady      = Server::Get()->GetNavigation().IsReady();
        context.reportStatus   = horse->navigationStatus;
        context.reportRevision = horse->navigationStatusRevision;
        context.intentRevision = horse->navigationRevision;
        return context;
    }

    void HorseNavigation::Publish(Shared::Entities::HorseEntity *horse, Npc::NpcOrder &order) {
        horse->navigationIntent   = order.SimulatorIntent(horse->position);
        horse->navigationRevision = Shared::Entities::HorseEntity::AdvanceRevision(horse->navigationRevision);
        order.Published(horse->navigationRevision);
        horse->ForceState();
    }

    bool HorseNavigation::MoveTo(Shared::Entities::HorseEntity *horse, const glm::vec3 &point, Shared::Game::NpcSpeed speed, float radius) {
        Shared::Game::NpcIntent intent;
        intent.kind   = Shared::Game::NpcIntentKind::MoveTo;
        intent.point  = point;
        intent.speed  = speed;
        intent.radius = radius;
        if (horse == nullptr || !horse->alive || horse->riderNetworkId != 0 || !intent.IsSane() || !Server::Get()->GetNavigation().IsReady()) {
            return false;
        }
        auto &order = _orders.For(horse->GetNetworkID());
        order.Command(intent, Npc::Pathfinding::Server, Context(horse));
        _orders.Track(horse->GetNetworkID());
        return true;
    }

    bool HorseNavigation::Patrol(Shared::Entities::HorseEntity *horse, const std::vector<glm::vec3> &points, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds) {
        if (horse == nullptr || !horse->alive || horse->riderNetworkId != 0 || !Npc::NpcOrder::IsUsableRoute(points, speed, waitSeconds) || !Server::Get()->GetNavigation().IsReady()) {
            return false;
        }
        auto &order = _orders.For(horse->GetNetworkID());
        order.Patrol(points, loop, speed, waitSeconds, Npc::Pathfinding::Server, Context(horse));
        _orders.Track(horse->GetNetworkID());
        return true;
    }

    void HorseNavigation::Hold(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr) {
            return;
        }
        auto *order = _orders.Find(horse->GetNetworkID());
        if (order != nullptr) {
            order->Command({}, Npc::Pathfinding::Server, Context(horse));
            Publish(horse, *order);
        }
    }

    void HorseNavigation::Forget(std::uint64_t id) {
        _orders.Erase(id);
        _simulators.erase(id);
    }

    const char *HorseNavigation::Status(Shared::Entities::HorseEntity *horse) {
        auto *order = horse != nullptr ? _orders.Find(horse->GetNetworkID()) : nullptr;
        return order != nullptr ? Shared::Game::NpcIntentStatusName(order->Status(Context(horse))) : "idle";
    }

    void HorseNavigation::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        // Snapshot only horses with orders, without scanning the world's
        // replicas. Callbacks can destroy bodies or add orders during updates.
        _orders.CollectIds(_updateIds);
        for (const auto id : _updateIds) {
            UpdateHorse(id);
        }
        const auto &navigation = Server::Get()->GetNavigation();
        _orders.RunPlans(Npc::NpcOrders::kPlansPerTick, navigation.IsReady(), [&](std::uint64_t id, Npc::NpcOrder &order) {
            auto *horse = replication->GetEntity<Shared::Entities::HorseEntity>(id);
            if (horse == nullptr) {
                Forget(id);
                return false;
            }
            // A horse cannot open a door. Use the same door-aware mesh, with
            // door links refused rather than walking into a closed leaf.
            order.Plan(navigation.GetMesh(), navigation.Options(Navigation::DoorPolicy::None, Npc::NpcOrders::kSearchRadius, order.Navigation().SearchHeight(), Npc::NpcOrders::kSearchNodes), horse->position);
            Publish(horse, order);
            return true;
        });
    }

    void HorseNavigation::UpdateHorse(std::uint64_t id) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *horse       = replication->GetEntity<Shared::Entities::HorseEntity>(id);
        if (horse == nullptr) {
            Forget(id);
            return;
        }
        auto *order = _orders.Find(id);
        if (order == nullptr) {
            return;
        }
        auto context = Context(horse);
        if (!context.active && order->Goal().IsMotion()) {
            Hold(horse);
            return;
        }
        const auto previous = _simulators.find(id);
        if (previous == _simulators.end() || previous->second != horse->ownerGUID) {
            _simulators[id] = horse->ownerGUID;
            order->OnSimulatorChanged(context.now);
            if (!order->NeedsPlan()) {
                Publish(horse, *order);
                context = Context(horse);
            }
        }
        const auto observed = order->Observe(context);
        if (observed.announce) {
            const auto revision = horse->navigationRevision;
            Scripting::Events::HorseIntentFinished(id, Shared::Game::NpcIntentStatusName(*observed.announce));
            if (replication->GetEntity<Shared::Entities::HorseEntity>(id) != horse || horse->navigationRevision != revision) {
                return;
            }
            order = _orders.Find(id);
            if (order == nullptr || order->NeedsPlan()) {
                return;
            }
        }
        const glm::vec3 previousPosition = horse->position;
        const auto advanced              = order->Advance(context, horse->position);
        const glm::vec2 travel           = glm::vec2(horse->position - previousPosition);
        if (!context.hasSimulator && glm::dot(travel, travel) > 0.000001F) {
            horse->rotation = glm::angleAxis(std::atan2(-travel.x, travel.y), glm::vec3(0.0F, 0.0F, 1.0F));
        }
        if (order->NeedsPlan()) {
            _orders.Track(id);
        }
        else if (observed.republish || advanced.republish) {
            Publish(horse, *order);
        }
    }
} // namespace KCDC::Features::Horse
