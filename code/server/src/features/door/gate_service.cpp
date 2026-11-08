#include "gate_service.h"

#include "shared/features/world/world_level_hash.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger = "Gate";

        /** The clock a cycle start is stamped with, shared with the client. */
        std::uint64_t Now() {
            return MafiaNet::GetTimeUS() / 1000;
        }
    } // namespace

    std::size_t GateService::GateKeyHash::operator()(const GateKey &key) const {
        const std::size_t world = std::hash<MafiaNet::VirtualWorldId> {}(key.virtualWorld);
        return std::hash<std::uint64_t> {}(key.gateGuid) ^ (world + 0x9E3779B9U + (world << 6U) + (world >> 2U));
    }

    void GateService::Install(const std::string &level) {
        _level     = level;
        _levelHash = KCDC::Shared::RPC::LevelHash(level);

        // The catalog is the whole set. Every gate is live in the global world
        // from boot; another virtual world builds each from the same row the
        // first time anything there asks for it.
        for (const KCDC::Shared::Game::GatePlacement &placement : KCDC::Shared::Game::g_GatePlacements) {
            if (level == placement.level) {
                _placements.push_back(&placement);
                Materialize(placement, MafiaNet::VIRTUAL_WORLD_GLOBAL);
            }
        }
        Framework::Logging::GetLogger(kLogger)->info("Seeded {} catalogued gate(s) for '{}'", _index.size(), level);
    }

    bool GateService::IsLive(const GateRecord &record) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && replication->GetEntityByNetworkID(record.networkId) != nullptr;
    }

    KCDC::Shared::Entities::GateEntity *GateService::Materialize(const KCDC::Shared::Game::GatePlacement &placement, MafiaNet::VirtualWorldId virtualWorld) {
        if (auto *existing = Find(placement.guid, virtualWorld)) {
            return existing;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->CreateEntity<KCDC::Shared::Entities::GateEntity>() : nullptr;
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a gate entity for {:016x}; is '{}' registered?", placement.guid, KCDC::Shared::Entities::GateEntity::kTypeName);
            return nullptr;
        }

        // Open, the entity's own default: an animated gate as the level shows
        // it, and a swapped one so that riders can get in, which in the story
        // the fortresses' gates stay shut against until a quest opens them.
        // Everything else is the catalog row.
        entity->gateGuid        = placement.guid;
        entity->levelHash       = _levelHash;
        entity->assetIndex      = placement.assetIndex;
        entity->copyWorld       = virtualWorld;
        entity->position        = glm::vec3(placement.positionX, placement.positionY, placement.positionZ);
        entity->streaming.range = KCDC::Shared::Entities::GateEntity::kStreamRange;
        entity->SetVirtualWorld(virtualWorld);

        GateKey key;
        key.virtualWorld = virtualWorld;
        key.gateGuid     = placement.guid;
        if (_index.contains(key)) {
            Framework::Logging::GetLogger(kLogger)->info("Gate {:016x} in world {} built again: its copy was destroyed, with the resource that asked for it", placement.guid, virtualWorld);
        }
        _index.insert_or_assign(key, GateRecord {entity, entity->GetNetworkID()});
        return entity;
    }

    KCDC::Shared::Entities::GateEntity *GateService::Find(std::uint64_t gateGuid, MafiaNet::VirtualWorldId virtualWorld) const {
        GateKey key;
        key.virtualWorld = virtualWorld;
        key.gateGuid     = gateGuid;
        const auto found = _index.find(key);
        return found != _index.end() && IsLive(found->second) ? found->second.entity : nullptr;
    }

    KCDC::Shared::Entities::GateEntity *GateService::Resolve(std::uint64_t gateGuid, MafiaNet::VirtualWorldId virtualWorld) {
        if (auto *gate = Find(gateGuid, virtualWorld)) {
            return gate;
        }
        for (const KCDC::Shared::Game::GatePlacement *placement : _placements) {
            if (placement->guid == gateGuid) {
                return Materialize(*placement, virtualWorld);
            }
        }
        return nullptr;
    }

    std::string GateService::Name(const KCDC::Shared::Entities::GateEntity &gate) {
        return KCDC::Shared::Game::GateKindName(gate.Asset().kind);
    }

    float GateService::OpennessAt(const KCDC::Shared::Entities::GateEntity &gate, std::uint64_t now) {
        return gate.OpennessAfter(now > gate.cycleStartMs ? static_cast<std::int64_t>(now - gate.cycleStartMs) : 0);
    }

    bool GateService::MostlyOpen(const KCDC::Shared::Entities::GateEntity &gate, float openness) {
        // Mid-cycle the direction of travel decides, so a bare `/gate` always
        // reverses what is happening rather than agreeing with it.
        if (KCDC::Shared::Entities::IsGateMoving(gate.state)) {
            return gate.state == KCDC::Shared::Entities::GateState::Opening;
        }
        return openness >= 0.5F;
    }

    void GateService::Settle(KCDC::Shared::Entities::GateEntity &gate) {
        gate.state        = KCDC::Shared::Entities::GateStateAfter(gate.state);
        gate.cycleStartMs = 0;
    }

    GateService::ToggleResult GateService::Toggle(KCDC::Shared::Entities::GateEntity &gate, std::optional<bool> open) {
        ToggleResult result;
        const std::uint64_t now = Now();
        const bool moving       = KCDC::Shared::Entities::IsGateMoving(gate.state);
        const float openness    = OpennessAt(gate, now);
        const bool opening      = open.value_or(!MostlyOpen(gate, openness));
        result.opening          = opening;

        if (moving && (gate.state == KCDC::Shared::Entities::GateState::Opening) == opening) {
            result.reason = opening ? "is already opening" : "is already closing";
            return result;
        }
        if (!moving && (gate.state == KCDC::Shared::Entities::GateState::Open) == opening) {
            result.reason = opening ? "is already open" : "is already closed";
            return result;
        }

        // Back-dated so the new motion picks up where the gate already is, so
        // reversing mid-cycle does not snap it to the far end and replay the
        // whole clip. Needs no extra wire field -- clients derive phase from
        // `cycleStartMs` -- and a settled gate falls out at start phase zero.
        //
        // Where the gate is comes from the pose, not the clock: the two clips
        // are not mirrors, so the same fraction of time into each is a
        // different place. A swapped gate's motions take no time at all: it
        // starts and ends at phase zero, and settles on the next tick.
        const std::uint32_t duration = opening ? gate.Asset().openDurationMs : gate.Asset().closeDurationMs;
        const float startPhase       = KCDC::Shared::Entities::GateMotionPhaseAt(gate.Asset(), opening, openness);
        const std::uint64_t backdate = static_cast<std::uint64_t>(startPhase * static_cast<float>(duration));

        gate.state        = opening ? KCDC::Shared::Entities::GateState::Opening : KCDC::Shared::Entities::GateState::Closing;
        gate.cycleStartMs = now - std::min(now, backdate);

        result.accepted = true;
        result.seconds  = static_cast<float>(duration - std::min<std::uint64_t>(backdate, duration)) / 1000.0F;
        Framework::Logging::GetLogger(kLogger)->info("{} {:016x} is {} ({:.1f}s)", Name(gate), gate.gateGuid, opening ? "opening" : "closing", result.seconds);
        return result;
    }

    KCDC::Shared::Entities::GateEntity *GateService::Nearest(const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, float radius) {
        const KCDC::Shared::Game::GatePlacement *nearest = nullptr;
        float nearestDistanceSquared                     = radius * radius;
        for (const KCDC::Shared::Game::GatePlacement *placement : _placements) {
            const glm::vec3 separation  = glm::vec3(placement->positionX, placement->positionY, placement->positionZ) - position;
            const float distanceSquared = glm::dot(separation, separation);
            if (distanceSquared <= nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                nearest                = placement;
            }
        }
        return nearest != nullptr ? Materialize(*nearest, virtualWorld) : nullptr;
    }

    float GateService::Openness(const KCDC::Shared::Entities::GateEntity &gate) {
        return OpennessAt(gate, Now());
    }

    std::vector<KCDC::Shared::Entities::GateEntity *> GateService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        std::vector<KCDC::Shared::Entities::GateEntity *> gates;
        gates.reserve(_index.size());
        for (const auto &entry : _index) {
            if (entry.second.entity == nullptr || (virtualWorld.has_value() && entry.first.virtualWorld != *virtualWorld) || !IsLive(entry.second)) {
                continue;
            }
            gates.push_back(entry.second.entity);
        }
        return gates;
    }

    void GateService::Update() {
        const std::uint64_t now = Now();
        for (const auto &entry : _index) {
            KCDC::Shared::Entities::GateEntity *gate = entry.second.entity;
            if (gate == nullptr || !IsLive(entry.second) || !KCDC::Shared::Entities::IsGateMoving(gate->state)) {
                continue;
            }
            if (now >= gate->cycleStartMs && now - gate->cycleStartMs >= gate->CurrentCycleDurationMs()) {
                Settle(*gate);
            }
        }
    }
} // namespace KCDC::Features::Door
