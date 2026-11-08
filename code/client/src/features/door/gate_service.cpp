#include "gate_service.h"

#include "shared/features/door/door_policy.h"
#include "shared/features/world/world_level_hash.h"
#include "shared/game/gate_catalog.h"

#include "core/application.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/c_entity_layer.h"
#include "sdk/cryengine/entitysystem/i_entity_iterator.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <vector>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger = "Gate";

        /** How long to let a layer stream in before asking for it again. */
        constexpr std::uint64_t kLayerRequestRetryMs = 10000;

        /** How long a swapped gate's layer is left alone after a switch. */
        constexpr std::uint64_t kLayerSwitchHoldMs = 3000;
    } // namespace

    std::uint64_t GateService::Now() {
        return MafiaNet::GetTimeUS() / 1000;
    }

    void GateService::NativeContextChanged() {
        ++_nativeContext;
    }

    void GateService::Reset() {
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Gate>([](Gate *gate) {
                gate->Unbind(true);
            });
        }

        // The session is over, so every layer this took goes back to how the
        // game had it -- at once, since nothing is left to wait for.
        for (const auto &claim : _layerClaims) {
            if (const Shared::Game::GatePlacement *placement = Shared::Game::FindGatePlacement(_observedLevel.c_str(), claim.first)) {
                ReleaseLayer(*placement);
            }
        }
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            for (const auto &swapped : _swappedLayers) {
                if (swapped.second.enabled != swapped.second.wasEnabled) {
                    entitySystem->EnableLayer(swapped.first.c_str(), swapped.second.wasEnabled, false);
                }
            }
        }

        _replicaIndex.clear();
        _layerClaims.clear();
        _swappedLayers.clear();
        _stages.clear();
        _observedContext   = 0;
        _observedLevelHash = 0;
        _observedLevel.clear();
    }

    void GateService::ObserveNativeContext(const std::string &level) {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context && _observedLevel == level) {
            return;
        }

        _observedContext   = context;
        _observedLevel     = level;
        _observedLevelHash = level.empty() ? 0 : Shared::RPC::LevelHash(level);

        // The layers belonged to the world that is gone, so there is nothing
        // to give back; forget them.
        _layerClaims.clear();
        _swappedLayers.clear();
        _stages.clear();

        // Every native pointer a replica held belonged to that world too, so
        // none may be touched again.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Gate>([](Gate *gate) {
                gate->Unbind();
            });
        }
    }

    void GateService::RefreshReplicaIndex() {
        _replicaIndex.clear();
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        // Each gate is driven by the copy of the player's own world, else the
        // global one; another world's copy, which a player in the global world
        // also holds, never. A copy left out is kept unbound, so it binds the
        // gate again once it is the one shown.
        const std::uint32_t world = KCDC::Core::gApplication->GetPlayerInventoryService().SyncedWorld().value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        replication->ForEach<Gate>([this, world](Gate *gate) {
            const int rank = Shared::Game::WorldCopyRank(gate->copyWorld, world);
            if (gate->gateGuid == 0 || rank == 0) {
                gate->Unbind();
                return;
            }
            const auto [entry, added] = _replicaIndex.emplace(gate->gateGuid, gate);
            if (!added) {
                Gate *shadowed = gate;
                if (rank > Shared::Game::WorldCopyRank(entry->second->copyWorld, world)) {
                    shadowed      = entry->second;
                    entry->second = gate;
                }
                shadowed->Unbind();
            }
        });
    }

    SDK::IEntity *GateService::ResolveEntity(SDK::EntityGUID gateGuid, bool allowSweep, bool &viaSweep) {
        viaSweep           = false;
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return nullptr;
        }

        // The fast path, when the engine has a mapping for this GUID.
        const SDK::EntityId entityId = entitySystem->FindEntityByGuid(gateGuid);
        if (entityId != 0) {
            SDK::IEntity *entity = entitySystem->GetEntity(entityId);
            if (entity != nullptr) {
                return entity;
            }
        }

        // A pooled entity never enters that registry, but its own GUID is
        // still correct, so the sweep finds what the lookup cannot.
        if (!allowSweep) {
            return nullptr;
        }
        SDK::IEntityItPtr iterator(entitySystem->GetEntityIterator());
        if (!iterator) {
            return nullptr;
        }
        iterator->MoveFirst();
        while (!iterator->IsEnd()) {
            SDK::IEntity *entity = iterator->Next();
            if (entity != nullptr && entity->GetGuid() == gateGuid) {
                viaSweep = true;
                return entity;
            }
        }
        return nullptr;
    }

    void GateService::ClaimLayer(const Shared::Game::GatePlacement &placement, std::uint64_t now) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *layer        = entitySystem != nullptr ? static_cast<SDK::CEntityLayer *>(entitySystem->FindLayer(placement.layer)) : nullptr;
        if (layer == nullptr) {
            NoteStage(placement.guid, Stage::NoLayer, "the level registers no layer by the name the catalog gives it");
            return;
        }

        // What the game had before this touched it is what goes back when
        // the gate leaves. The entity byte is no guide -- the constructor
        // sets it on a layer nothing ever enabled -- but the brush one is.
        auto claim = _layerClaims.find(placement.guid);
        if (claim == _layerClaims.end()) {
            claim = _layerClaims.emplace(placement.guid, LayerClaim {layer->IsEnabledBrush(), 0}).first;
        }

        // Asynchronous, and asking again meanwhile only restarts the read.
        if (claim->second.requestedAt != 0 && now - claim->second.requestedAt < kLayerRequestRetryMs) {
            return;
        }
        // Not serialized: this has no place in the player's save.
        entitySystem->EnableLayer(placement.layer, true, false);
        claim->second.requestedAt = now;
        NoteStage(placement.guid, Stage::LayerRequested, "streaming its layer in");
    }

    void GateService::ReleaseLayer(const Shared::Game::GatePlacement &placement) {
        const auto claim = _layerClaims.find(placement.guid);
        if (claim == _layerClaims.end()) {
            return;
        }
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            entitySystem->EnableLayer(placement.layer, claim->second.wasEnabled, false);
        }
        _layerClaims.erase(claim);
        NoteStage(placement.guid, Stage::LayerReleased, "out of range; its layer is back as the game had it");
    }

    void GateService::NoteStage(SDK::EntityGUID gateGuid, Stage stage, const char *detail) {
        const auto known = _stages.find(gateGuid);
        if (known != _stages.end() && known->second == stage) {
            return;
        }
        _stages.insert_or_assign(gateGuid, stage);
        Framework::Logging::GetLogger(kLogger)->info("Gate {:016x} in '{}': {}", gateGuid, _observedLevel, detail);
    }

    void GateService::ResolveLayers(std::uint64_t now) {
        // A layer whose gate this client no longer has a replica of goes back
        // too: out of range or gone, the server is no longer telling it
        // anything, and the game's own world is what is left.
        std::vector<SDK::EntityGUID> orphaned;
        for (const auto &claim : _layerClaims) {
            if (!_replicaIndex.contains(claim.first)) {
                orphaned.push_back(claim.first);
            }
        }
        for (const SDK::EntityGUID gateGuid : orphaned) {
            if (const Shared::Game::GatePlacement *placement = Shared::Game::FindGatePlacement(_observedLevel.c_str(), gateGuid)) {
                ReleaseLayer(*placement);
            }
            else {
                _layerClaims.erase(gateGuid);
            }
        }

        for (const auto &entry : _replicaIndex) {
            Gate *gate = entry.second;
            if (gate->levelHash != _observedLevelHash) {
                continue;
            }
            // A swapped gate has no entity to find; DriveSwappedGates has it.
            const Shared::Game::GatePlacement *placement = Shared::Game::FindGatePlacement(_observedLevel.c_str(), gate->gateGuid);
            if (placement == nullptr || placement->IsSwapped()) {
                continue;
            }

            if (gate->IsBound()) {
                continue;
            }

            // Each gate keeps its own sweep clock: one shared across them was
            // spent on the first frame of a session, before any replica had
            // arrived, and left a late joiner two seconds on the stand-in.
            const bool allowSweep = gate->SweepDue(now);
            if (allowSweep) {
                gate->NoteSwept(now);
            }
            bool viaSweep        = false;
            SDK::IEntity *entity = ResolveEntity(gate->gateGuid, allowSweep, viaSweep);
            if (entity == nullptr) {
                // Only once a sweep has missed it too: the GUID lookup cannot
                // see a pooled entity, and re-enabling a layer that is already
                // loaded streams it again.
                if (allowSweep) {
                    ClaimLayer(*placement, now);
                }
                continue;
            }
            if (viaSweep) {
                NoteStage(gate->gateGuid, Stage::SweepOnly, "found by GUID sweep; the engine has no guid mapping for it (pooled spawn)");
            }
            if (!gate->Bind(entity, *placement)) {
                NoteStage(gate->gateGuid, Stage::NoCharacter, "present but its character has not streamed in yet");
                continue;
            }
            NoteStage(gate->gateGuid, Stage::Bound, "bound and driven by the server's cycle");
        }
    }

    void GateService::DriveReplicas(std::uint64_t now) {
        std::int64_t offset         = 0;
        const bool serverClockKnown = KCDC::Common::ServerClockDifferential(offset);
        for (const auto &entry : _replicaIndex) {
            if (entry.second->IsBound()) {
                entry.second->Drive(now, offset, serverClockKnown);
            }
        }
    }

    void GateService::SwitchSwappedLayer(const char *layer, bool enabled, std::uint64_t now) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }

        auto claim = _swappedLayers.find(layer);
        if (claim == _swappedLayers.end()) {
            auto *found = static_cast<SDK::CEntityLayer *>(entitySystem->FindLayer(layer));
            if (found == nullptr) {
                return;
            }
            // A gate's layers are brushes, so they go by the brush byte: the
            // entity byte reads true on a layer nothing has enabled. A cleared
            // quest layer holds entities and goes by the entity byte.
            const bool wasEnabled = Shared::Game::IsClearedLevelLayer(_observedLevel.c_str(), layer) ? found->IsEnabled() : found->IsEnabledBrush();
            claim                 = _swappedLayers.emplace(layer, SwappedLayer {wasEnabled, wasEnabled, 0}).first;
            Framework::Logging::GetLogger(kLogger)->info("Gate layer {} in '{}' found {}", layer, _observedLevel, wasEnabled ? "on" : "off");
        }

        SwappedLayer &swapped = claim->second;
        if (swapped.enabled == enabled || (swapped.switchedAt != 0 && now - swapped.switchedAt < kLayerSwitchHoldMs)) {
            return;
        }
        // Not serialized: this has no place in the player's save.
        entitySystem->EnableLayer(layer, enabled, false);
        swapped.enabled    = enabled;
        swapped.switchedAt = now;
        Framework::Logging::GetLogger(kLogger)->info("Gate layer {} in '{}' switched {}", layer, _observedLevel, enabled ? "on" : "off");
    }

    void GateService::DriveSwappedGates(std::uint64_t now) {
        _wantedLayers.clear();
        for (const auto &entry : _replicaIndex) {
            const Gate *gate = entry.second;
            if (gate->levelHash != _observedLevelHash) {
                continue;
            }
            const Shared::Game::GatePlacement *placement = Shared::Game::FindGatePlacement(_observedLevel.c_str(), gate->gateGuid);
            if (placement == nullptr || !placement->IsSwapped()) {
                continue;
            }
            // A swapped gate's cycle has no length, so it is wherever it is heading.
            const bool open = Shared::Entities::GateStateAfter(gate->state) == Shared::Entities::GateState::Open;
            for (const char *layer : placement->openLayers) {
                _wantedLayers.emplace_back(layer, open);
            }
            for (const char *layer : placement->closedLayers) {
                _wantedLayers.emplace_back(layer, !open);
            }
            for (const char *layer : placement->clearedLayers) {
                _wantedLayers.emplace_back(layer, false);
            }
        }
        // Not before the game's own session start (module message 52): it
        // applies the quest phases then, and switches a cleared layer back on.
        const bool gameSessionStarted = Core::gApplication->GetSessionLoadContext().gameSessionStarted;
        for (const Shared::Game::ClearedLevelLayer &cleared : Shared::Game::kClearedLevelLayers) {
            if (gameSessionStarted && _observedLevel == cleared.level) {
                _wantedLayers.emplace_back(cleared.layer, false);
            }
        }

        for (const auto &wanted : _wantedLayers) {
            SwitchSwappedLayer(wanted.first, wanted.second, now);
        }

        // A layer no gate here wants any more goes back to how the game had
        // it, and is forgotten once it is there.
        for (auto claim = _swappedLayers.begin(); claim != _swappedLayers.end();) {
            const bool wanted = std::any_of(_wantedLayers.begin(), _wantedLayers.end(), [&claim](const std::pair<const char *, bool> &entry) {
                return claim->first == entry.first;
            });
            if (wanted) {
                ++claim;
                continue;
            }
            if (claim->second.enabled == claim->second.wasEnabled) {
                claim = _swappedLayers.erase(claim);
                continue;
            }
            SwitchSwappedLayer(claim->first.c_str(), claim->second.wasEnabled, now);
            ++claim;
        }
    }

    void GateService::Update() {
        const std::string level = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetCurrentLevelName() : std::string {};
        auto *gameFramework     = SDK::IGameFramework::GetInstance();
        auto *levelSystem       = gameFramework != nullptr ? gameFramework->GetILevelSystem() : nullptr;

        ObserveNativeContext(level);
        if (level.empty() || levelSystem == nullptr || !levelSystem->IsLevelLoaded()) {
            return;
        }

        const std::uint64_t now = Now();
        RefreshReplicaIndex();
        ResolveLayers(now);
        DriveReplicas(now);
        DriveSwappedGates(now);
    }
} // namespace KCDC::Features::Door
