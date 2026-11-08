#include "player_station_service.h"
#include "features/smart_object/smart_object_service.h"

#include "core/server.h"
#include "features/dice/dice_service.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/dice_catalog.h"

#include <core_modules.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cmath>

namespace KCDC::Features::Player {
    StationService &StationService::Get() {
        static StationService service;
        return service;
    }

    void StationService::Install() {
        _records.clear();
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::RPC::DiceScene>([this](const Shared::RPC::DiceScene &report, MafiaNet::Packet *packet) {
            if (packet != nullptr && report.IsSane()) {
                ReceiveScene(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::RPC::PlayerStation>([this](const Shared::RPC::PlayerStation &report, MafiaNet::Packet *packet) {
            if (packet != nullptr && report.IsSane()) {
                Receive(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
    }

    bool StationService::Allowed(Shared::Entities::HumanEntity &human, const Shared::Game::StationState &state) const {
        if (!state.Active()) {
            return true;
        }
        if (!human.soul.CanAct() || !human.transformReady) {
            return false;
        }
        const auto level    = Server::Get()->GetModConfig().Get<std::string>("level");
        const auto isNearby = [&](float x, float y, float z) {
            const double dx = human.position.x - x, dy = human.position.y - y, dz = human.position.z - z;
            const double distance = dx * dx + dy * dy + dz * dz;
            return std::isfinite(distance) && distance <= 64.0;
        };
        if (state.activity == Shared::Game::StationActivity::Dice) {
            if (!KCDC::Features::Dice::DiceService::Get().HasSeat(human.GetNetworkID(), state.match, state.slot)) {
                return false;
            }
            for (const Shared::Game::DiceTable *table : KCDC::Features::SmartObject::SmartObjectService::Get().DiceTables(human.GetVirtualWorld())) {
                if (table->guid == state.guid && table->level == level && isNearby(table->x, table->y, table->z)) {
                    for (const auto &[id, record] : _records) {
                        if (id == human.GetNetworkID() || record.expires <= MafiaNet::GetTime() || record.world != human.GetVirtualWorld() || !record.report.state.Active()) {
                            continue;
                        }
                        if (record.report.state.match == state.match ? record.report.state.guid != state.guid : record.report.state.guid == state.guid) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            return false;
        }
        const auto occupiedByAnother = [&]() {
            for (const auto &[id, record] : _records) {
                if (id != human.GetNetworkID() && record.expires > MafiaNet::GetTime() && record.world == human.GetVirtualWorld() && record.report.state.Active() && record.report.state.guid == state.guid) {
                    return true;
                }
            }
            return false;
        };
        if (state.activity == Shared::Game::StationActivity::Sharpening) {
            // A wheel the level places, near the player, and nobody else's:
            // Grindstone.lua turns a second user away while one works it.
            const Shared::Game::SharpeningStation *grindstone = Shared::Game::FindSharpeningStation(level, state.guid);
            return grindstone != nullptr && isNearby(grindstone->x, grindstone->y, grindstone->z) && !occupiedByAnother();
        }
        for (const Shared::Game::CraftStation *table : KCDC::Features::SmartObject::SmartObjectService::Get().CraftStations(human.GetVirtualWorld())) {
            const auto kind = state.activity == Shared::Game::StationActivity::Alchemy ? Shared::Game::CraftKind::Alchemy : Shared::Game::CraftKind::Smithing;
            if (table->guid != state.guid || table->kind != kind || table->level != level || !isNearby(table->x, table->y, table->z)) {
                continue;
            }
            return !occupiedByAnother();
        }
        return false;
    }

    void StationService::Receive(const Shared::RPC::PlayerStation &report, MafiaNet::PeerGuid peer) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<Shared::Entities::HumanEntity>(peer);
        if (human == nullptr || report.epoch != human->stateEpoch || report.incarnation != human->soul.incarnation) {
            return;
        }
        auto found = _records.find(human->GetNetworkID());
        if (found != _records.end() && found->second.report.epoch == report.epoch && found->second.report.incarnation == report.incarnation && !Shared::Game::StationState::Newer(report.state.revision, found->second.report.state.revision)) {
            return;
        }
        if (!Allowed(*human, report.state)) {
            return;
        }
        auto &record = _records[human->GetNetworkID()];
        if (record.report.epoch != report.epoch || record.report.incarnation != report.incarnation) {
            record           = {};
            human->diceScene = {};
        }
        record.report  = report;
        record.world   = human->GetVirtualWorld();
        record.expires = MafiaNet::GetTime() + 5000;
        // Heartbeats renew the lease without restarting an unchanged pose.
        if (!human->station.SamePose(report.state)) {
            human->station = report.state;
            if (report.state.activity != Shared::Game::StationActivity::Dice || report.state.match != human->diceScene.match) {
                human->diceScene = {};
            }
            Framework::Logging::GetLogger("Player")->debug("Player {} occupancy {} on {:016X}, slot {}, revision {}", human->GetNetworkID(), static_cast<unsigned>(report.state.activity), report.state.guid, report.state.slot, report.state.revision);
        }
    }

    void StationService::ReceiveScene(const Shared::RPC::DiceScene &report, MafiaNet::PeerGuid peer) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<Shared::Entities::HumanEntity>(peer);
        if (human == nullptr || human->stateEpoch != report.epoch || human->soul.incarnation != report.incarnation) {
            return;
        }
        const auto found = _records.find(human->GetNetworkID());
        if (found == _records.end()) {
            return;
        }
        auto &record   = found->second;
        const auto now = MafiaNet::GetTime();
        if (record.expires <= now || now < record.sceneAt + 40 || record.world != human->GetVirtualWorld() || human->station.activity != Shared::Game::StationActivity::Dice || human->station.slot != 0 || human->station.match != report.scene.match) {
            return;
        }
        if (!Shared::Game::StationState::Newer(report.scene.revision, record.sceneRevision) || !Allowed(*human, human->station)) {
            return;
        }
        for (const Shared::Game::DiceTable *table : KCDC::Features::SmartObject::SmartObjectService::Get().DiceTables(human->GetVirtualWorld())) {
            if (table->guid != human->station.guid) {
                continue;
            }
            if (!report.scene.Near(table->x, table->y, table->z)) {
                return;
            }
            human->diceScene     = report.scene;
            record.sceneRevision = report.scene.revision;
            record.sceneAt       = now;
            return;
        }
    }

    void StationService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        for (auto it = _records.begin(); it != _records.end();) {
            auto *human  = replication->GetEntity<Shared::Entities::HumanEntity>(it->first);
            auto &record = it->second;
            if (human == nullptr) {
                it = _records.erase(it);
                continue;
            }
            if (MafiaNet::GetTime() >= record.sceneAt + 1500) {
                human->diceScene = {};
            }
            if (MafiaNet::GetTime() >= record.expires || record.world != human->GetVirtualWorld() || record.report.epoch != human->stateEpoch || record.report.incarnation != human->soul.incarnation || !Allowed(*human, record.report.state)) {
                if (human->station.Active()) {
                    Framework::Logging::GetLogger("Player")->debug("Player {} occupancy cleared", human->GetNetworkID());
                }
                human->station   = {};
                human->diceScene = {};
                // Keep the high-water mark after expiry. A delayed report
                // must not resurrect an ended lease.
                record.expires = 0;
            }
            ++it;
        }
    }

    void StationService::Forget(std::uint64_t player) {
        _records.erase(player);
        if (auto *human = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::HumanEntity>(player)) {
            human->station   = {};
            human->diceScene = {};
        }
    }
} // namespace KCDC::Features::Player
