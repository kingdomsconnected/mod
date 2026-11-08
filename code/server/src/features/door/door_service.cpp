#include "door_service.h"
#include "door_scripting.h"

#include "features/level_edit/level_edit_module.h"
#include "features/level_edit/level_edit_verbs.h"

#include "shared/features/door/door_policy.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/door_catalog.h"
#include "shared/game/item_classes.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <fmt/format.h>

#include <mafianet/get_time.h>
#include <mafianet/virtual_world.h>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger = "Door";

        // One answer per operator question, so this only bounds a client
        // that answers questions nobody asked.
        constexpr std::size_t kMaxIncomingNearestAnswers = 256;
        constexpr std::uint64_t kNearestAnswerTimeout    = 5000;

        const char *EventName(KCDC::Shared::RPC::DoorEvent event) {
            switch (event) {
            case KCDC::Shared::RPC::DoorEvent::Manipulation: return "manipulation";
            case KCDC::Shared::RPC::DoorEvent::Lockpicked: return "lockpicked";
            }
            return "invalid";
        }
    } // namespace

    std::size_t DoorService::DoorKeyHash::operator()(const DoorKey &key) const {
        const std::size_t world = std::hash<MafiaNet::VirtualWorldId> {}(key.virtualWorld);
        return std::hash<std::uint64_t> {}(key.doorGuid) ^ (world + 0x9E3779B9U + (world << 6U) + (world >> 2U));
    }

    void DoorService::Install(const std::string &level) {
        _level     = level;
        _levelHash = KCDC::Shared::RPC::DoorLevelHash(level);

        // The catalog is the whole set: the server never takes a client's
        // word that a door exists. Every one is live in the global world from
        // boot; another virtual world builds each from the same row the first
        // time anything there asks for it.
        for (const KCDC::Shared::Game::DoorPlacement &placement : KCDC::Shared::Game::g_DoorPlacements) {
            if (level == placement.level) {
                _catalog.emplace(placement.guid, &placement);
                Materialize(placement, MafiaNet::VIRTUAL_WORLD_GLOBAL);
            }
        }
        Framework::Logging::GetLogger(kLogger)->info("Seeded {} catalogued door(s) for '{}'", _index.size(), level);

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Door RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::DoorNearestAnswer>([this](const KCDC::Shared::RPC::DoorNearestAnswer &answer, MafiaNet::Packet *packet) {
            QueueNearestAnswer(answer, packet);
        });
    }

    void DoorService::QueueNearestAnswer(const KCDC::Shared::RPC::DoorNearestAnswer &answer, MafiaNet::Packet *packet) {
        if (packet == nullptr || !answer.IsSane()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incomingNearestAnswers.size() < kMaxIncomingNearestAnswers) {
            _incomingNearestAnswers.emplace_back(answer, MafiaNet::ToPeerGuid(packet->guid));
        }
    }

    void DoorService::Update() {
        std::vector<std::pair<KCDC::Shared::RPC::DoorNearestAnswer, MafiaNet::PeerGuid>> nearestAnswers;
        {
            std::scoped_lock lock(_incomingMutex);
            nearestAnswers.swap(_incomingNearestAnswers);
        }

        for (const auto &answer : nearestAnswers) {
            ResolveNearestAnswer(answer.first, answer.second);
        }
        const std::uint64_t now = MafiaNet::GetTime();
        std::vector<std::uint64_t> expired;
        for (const auto &entry : _pendingNearest) {
            if (now >= entry.second.expiresAt) {
                expired.push_back(entry.first);
            }
        }
        for (const std::uint64_t requestId : expired) {
            SettleNearest(requestId, NearestDoor {false, "Your client did not answer in time.", 0, {}, 0.0F, false, false});
        }

        ReconcileRemovals();
        ForgetGoneHungDoors();
    }

    bool DoorService::IsLive(const DoorRecord &record) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && replication->GetEntityByNetworkID(record.networkId) != nullptr;
    }

    KCDC::Shared::Entities::DoorEntity *DoorService::Materialize(const KCDC::Shared::Game::DoorPlacement &placement, MafiaNet::VirtualWorldId virtualWorld, const KCDC::Shared::Entities::SpawnedDoor *spawned) {
        if (auto *existing = Find(placement.guid, virtualWorld)) {
            return existing;
        }
        // A level edit has the door out of this world: there is nothing to build.
        if (spawned == nullptr && IsRemoved(placement.guid, virtualWorld)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->CreateEntity<KCDC::Shared::Entities::DoorEntity>() : nullptr;
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a door entity for {:016x}; is '{}' registered?", placement.guid, KCDC::Shared::Entities::DoorEntity::kTypeName);
            return nullptr;
        }

        // Closed, and locked exactly when AnimDoor:Reset would lock it, so a
        // client building the door finds it already agreeing.
        entity->doorGuid        = placement.guid;
        entity->levelHash       = _levelHash;
        entity->copyWorld       = virtualWorld;
        entity->stateToken      = 1;
        entity->open            = false;
        entity->locked          = placement.startsLocked;
        entity->position        = glm::vec3(placement.positionX, placement.positionY, placement.positionZ);
        entity->streaming.range = KCDC::Shared::Entities::DoorEntity::kStreamRange;
        if (spawned != nullptr) {
            entity->spawned  = *spawned;
            entity->rotation = spawned->rotation;
        }
        entity->SetVirtualWorld(virtualWorld);

        DoorKey key;
        key.virtualWorld = virtualWorld;
        key.doorGuid     = placement.guid;
        if (_index.contains(key)) {
            Framework::Logging::GetLogger(kLogger)->info("Door {:016x} in world {} built again: its copy was destroyed, with the resource that asked for it", placement.guid, virtualWorld);
        }
        DoorRecord record;
        record.entity    = entity;
        record.networkId = entity->GetNetworkID();
        record.placement = &placement;
        _index.insert_or_assign(key, record);
        return entity;
    }

    KCDC::Shared::Entities::DoorEntity *DoorService::Find(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) const {
        DoorKey key;
        key.virtualWorld = virtualWorld;
        key.doorGuid     = doorGuid;
        // A hung door has one copy, in the world it was hung in, which every world that sees it works.
        if (const auto hung = _hung.find(doorGuid); hung != _hung.end() && MafiaNet::VirtualWorldsCanSee(hung->second->virtualWorld, virtualWorld)) {
            key.virtualWorld = hung->second->virtualWorld;
        }
        const auto found = _index.find(key);
        // A copy a level edit has just taken out reads as gone before the tick takes it down.
        return found != _index.end() && IsLive(found->second) && !IsRemoved(doorGuid, virtualWorld) ? found->second.entity : nullptr;
    }

    KCDC::Shared::Entities::DoorEntity *DoorService::Resolve(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) {
        if (auto *door = Find(doorGuid, virtualWorld)) {
            return door;
        }
        const auto catalogued = _catalog.find(doorGuid);
        return catalogued != _catalog.end() ? Materialize(*catalogued->second, virtualWorld) : nullptr;
    }

    bool DoorService::IsRemoved(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) const {
        return _catalog.contains(doorGuid) && KCDC::Features::LevelEdit::LevelEditModule::Get().RemovalOf(doorGuid, virtualWorld) != nullptr;
    }

    std::vector<DoorService::RemovedDoor> DoorService::CollectRemoved(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        std::vector<RemovedDoor> removed;
        for (const auto *edit : KCDC::Features::LevelEdit::LevelEditModule::Get().Removals()) {
            const auto placement = _catalog.find(edit->guid);
            if (placement == _catalog.end() || (virtualWorld.has_value() && edit->GetVirtualWorld() != *virtualWorld)) {
                continue;
            }
            removed.push_back(RemovedDoor {edit->guid, edit->GetVirtualWorld(), placement->second});
        }
        return removed;
    }

    bool DoorService::Remove(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld, std::string &error) {
        const auto catalogued = _catalog.find(doorGuid);
        if (catalogued == _catalog.end()) {
            error = _hung.contains(doorGuid) ? "a door a script hung is taken down with destroy(), not removed" : "the level places no such door";
            return false;
        }
        // Already out, here or from the global world: a second edit would only list it twice.
        if (IsRemoved(doorGuid, virtualWorld)) {
            return true;
        }
        const KCDC::Shared::Game::DoorPlacement &placement = *catalogued->second;

        // The edit is the record: every player in the world hides the native
        // door, collision and all, and this service reads the door as gone.
        KCDC::Shared::Game::LevelEditDefinition definition;
        definition.kind      = KCDC::Shared::Game::LevelEditKind::Entity;
        definition.guid      = doorGuid;
        // Only a label: the edit finds the door by GUID, and some prefab doors' names outrun what an edit carries.
        definition.name      = std::strlen(placement.name) <= KCDC::Shared::Entities::LevelEditEntity::kMaxNameLength ? placement.name : "";
        definition.className = "AnimDoor";
        definition.at        = glm::vec3(placement.positionX, placement.positionY, placement.positionZ);
        definition.hidden    = true;
        if (KCDC::Features::LevelEdit::LevelEdits::Apply(definition, virtualWorld, error) == nullptr) {
            return false;
        }
        ReconcileRemovals();
        Framework::Logging::GetLogger(kLogger)->info("Door {:016x} taken out of world {}", doorGuid, virtualWorld);
        return true;
    }

    bool DoorService::Unremove(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) {
        auto *edit = KCDC::Features::LevelEdit::LevelEditModule::Get().RemovalOf(doorGuid, virtualWorld);
        if (edit == nullptr || edit->GetVirtualWorld() != virtualWorld || !_catalog.contains(doorGuid)) {
            return false;
        }
        KCDC::Features::LevelEdit::LevelEdits::Restore(edit);
        ReconcileRemovals();
        Framework::Logging::GetLogger(kLogger)->info("Door {:016x} put back in world {}", doorGuid, virtualWorld);
        return true;
    }

    void DoorService::ReconcileRemovals() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // New removals, from any route -- a script, a world resource, a raw
        // LevelEdit.apply -- take the copies they cover down. Out of the
        // global world, a door is out of every world.
        const std::uint64_t revision = KCDC::Features::LevelEdit::LevelEditModule::Get().Revision();
        if (revision != _seenEditRevision) {
            _seenEditRevision = revision;
            for (const auto *edit : KCDC::Features::LevelEdit::LevelEditModule::Get().Removals()) {
                if (!_catalog.contains(edit->guid)) {
                    continue;
                }
                const MafiaNet::VirtualWorldId removedFrom = edit->GetVirtualWorld();
                for (auto entry = _index.begin(); entry != _index.end();) {
                    const bool covered = entry->first.doorGuid == edit->guid && (removedFrom == MafiaNet::VIRTUAL_WORLD_GLOBAL || entry->first.virtualWorld == removedFrom);
                    if (!covered) {
                        ++entry;
                        continue;
                    }
                    if (IsLive(entry->second)) {
                        replication->DestroyEntity(entry->second.entity);
                    }
                    if (std::find(_removed.begin(), _removed.end(), entry->first) == _removed.end()) {
                        _removed.push_back(entry->first);
                    }
                    entry = _index.erase(entry);
                }
            }
        }

        // A removal that went, however it went, gives the global world its
        // copy back; another world builds its own on the next ask, as ever.
        std::erase_if(_removed, [this](const DoorKey &key) {
            if (IsRemoved(key.doorGuid, key.virtualWorld)) {
                return false;
            }
            if (key.virtualWorld == MafiaNet::VIRTUAL_WORLD_GLOBAL) {
                if (const auto catalogued = _catalog.find(key.doorGuid); catalogued != _catalog.end()) {
                    Materialize(*catalogued->second, MafiaNet::VIRTUAL_WORLD_GLOBAL);
                }
            }
            return true;
        });
    }

    KCDC::Shared::Entities::DoorEntity *DoorService::Spawn(const KCDC::Shared::Game::DoorDefinition &definition, MafiaNet::VirtualWorldId virtualWorld, std::string &error) {
        const std::uint16_t modelIndex             = KCDC::Shared::Game::ResolveDoorModel(definition.model);
        const KCDC::Shared::Game::DoorModel *model = KCDC::Shared::Game::DoorModelAt(modelIndex);
        if (model == nullptr) {
            error = "'" + definition.model + "' is not a door model";
            return nullptr;
        }
        // The door's wire format carries positions within the world's bounds only.
        if (!KCDC::Shared::Game::IsWorldPosition(definition.position) || !KCDC::Shared::Game::IsUsableRotation(definition.rotation)) {
            error = "the position is outside the world or the rotation is not finite";
            return nullptr;
        }
        if (definition.name.size() > KCDC::Shared::Game::DoorDefinition::kMaxNameLength) {
            error = "the name is longer than 64 bytes";
            return nullptr;
        }
        if (!definition.keyItem.empty() && (!KCDC::Shared::Game::ItemClassIdFromGuid(definition.keyItem).IsValid() || KCDC::Shared::Game::FindItemClass(definition.keyItem) == nullptr)) {
            error = "keyItem '" + definition.keyItem + "' is not an item class this build knows";
            return nullptr;
        }

        const std::uint64_t serial   = _nextHungSerial++;
        auto hung                    = std::make_unique<HungDoor>();
        hung->virtualWorld           = virtualWorld;
        hung->definition             = definition;
        hung->definition.model       = model->path;
        hung->definition.rotation    = glm::normalize(definition.rotation);
        hung->name                   = definition.name.empty() ? "spawned_door_" + std::to_string(serial) : definition.name;
        hung->spawned.model          = modelIndex;
        hung->spawned.rotation       = hung->definition.rotation;
        hung->spawned.lockType       = definition.lock;
        hung->spawned.lockpickable   = definition.lockpickable;
        hung->spawned.lockDifficulty = definition.lockDifficulty < 0.0F ? -1.0F : std::clamp(definition.lockDifficulty, 0.0F, 1.0F);
        hung->spawned.keyItem        = definition.keyItem;
        const glm::vec3 &position    = definition.position;

        // Every rule the level's own doors answer to reads this, so a hung
        // door is reached, keyed and lockpicked the same way.
        const glm::vec2 forward                      = KCDC::Shared::Game::HungDoorForward(hung->spawned.rotation);
        KCDC::Shared::Game::DoorPlacement &placement = hung->placement;
        placement.level                              = _level.c_str();
        placement.guid                               = KCDC::Shared::Entities::DoorEntity::SpawnedGuid(serial);
        placement.name                               = hung->name.c_str();
        placement.positionX                          = position.x;
        placement.positionY                          = position.y;
        placement.positionZ                          = position.z;
        placement.forwardX                           = forward.x;
        placement.forwardY                           = forward.y;
        placement.keyItemClass                       = hung->spawned.keyItem.c_str();
        placement.lockType                           = definition.lock;
        placement.startsLocked                       = definition.locked;
        placement.lockReversed                       = false;
        placement.lockpickable                       = definition.lockpickable;
        placement.dynamicKey                         = false;
        placement.model                              = modelIndex;

        auto *entity = Materialize(placement, virtualWorld, &hung->spawned);
        if (entity == nullptr) {
            error = "the door entity could not be created";
            return nullptr;
        }
        _hung.emplace(placement.guid, std::move(hung));
        Framework::Logging::GetLogger(kLogger)->info("Hung door {:016x} ('{}') in world {} at {:.2f}, {:.2f}, {:.2f}", entity->doorGuid, model->path, virtualWorld, position.x, position.y, position.z);
        return entity;
    }

    void DoorService::Despawn(KCDC::Shared::Entities::DoorEntity &door) {
        const auto hung = _hung.find(door.doorGuid);
        if (hung == _hung.end()) {
            return;
        }
        DoorKey key;
        key.virtualWorld = hung->second->virtualWorld;
        key.doorGuid     = door.doorGuid;
        _index.erase(key);
        _hung.erase(hung);
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(&door);
        }
    }

    void DoorService::ForgetGoneHungDoors() {
        std::erase_if(_hung, [this](const auto &entry) {
            DoorKey key;
            key.virtualWorld = entry.second->virtualWorld;
            key.doorGuid     = entry.first;
            const auto found = _index.find(key);
            if (found != _index.end() && IsLive(found->second)) {
                return false;
            }
            // Destroyed with the resource that hung it: the placement goes with the record that points at it.
            _index.erase(key);
            return true;
        });
    }

    const KCDC::Shared::Game::DoorDefinition *DoorService::DefinitionOf(const KCDC::Shared::Entities::DoorEntity &door) const {
        const auto hung = _hung.find(door.doorGuid);
        return hung != _hung.end() ? &hung->second->definition : nullptr;
    }

    DoorService::DoorRecord *DoorService::FindRecord(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) {
        DoorKey key;
        key.virtualWorld = virtualWorld;
        key.doorGuid     = doorGuid;
        const auto found = _index.find(key);
        return found != _index.end() && IsLive(found->second) ? &found->second : nullptr;
    }

    const DoorService::DoorRecord *DoorService::FindRecord(const KCDC::Shared::Entities::DoorEntity &door) const {
        DoorKey key;
        key.virtualWorld = door.GetVirtualWorld();
        key.doorGuid     = door.doorGuid;
        const auto found = _index.find(key);
        return found != _index.end() && found->second.entity == &door ? &found->second : nullptr;
    }

    const KCDC::Shared::Game::DoorPlacement *DoorService::PlacementOf(const KCDC::Shared::Entities::DoorEntity &door) const {
        const DoorRecord *record = FindRecord(door);
        return record != nullptr ? record->placement : nullptr;
    }

    bool DoorService::IsLockedByServer(const KCDC::Shared::Entities::DoorEntity &door) const {
        const DoorRecord *record = FindRecord(door);
        return record != nullptr && record->lockedByServer;
    }

    void DoorService::Correct(KCDC::Shared::Entities::DoorEntity &door) {
        door.changedByServer = true;
        ++door.stateToken;
    }

    void DoorService::ProcessInteraction(const KCDC::Shared::RPC::DoorInteraction &interaction, KCDC::Shared::Entities::HumanEntity &actor) {
        const auto logger = Framework::Logging::GetLogger(kLogger);
        if (interaction.levelHash != _levelHash) {
            logger->warn("Rejected door interaction from actor {} for level {:#x}; server level '{}' hashes to {:#x}", actor.GetNetworkID(), interaction.levelHash, _level, _levelHash);
            return;
        }

        const MafiaNet::VirtualWorldId virtualWorld = actor.GetVirtualWorld();
        auto *door                                  = Resolve(interaction.doorGuid, virtualWorld);
        DoorRecord *record                          = door != nullptr ? FindRecord(door->doorGuid, door->copyWorld) : nullptr;
        if (record == nullptr) {
            logger->warn("Rejected door interaction from actor {} for {:016x}: {}", actor.GetNetworkID(), interaction.doorGuid, IsRemoved(interaction.doorGuid, virtualWorld) ? "a level edit has the door out of this world" : "no such door");
            return;
        }

        KCDC::Shared::Game::DoorReport report;
        report.open                                   = door->open;
        report.locked                                 = door->locked;
        report.lockedByServer                         = record->lockedByServer;
        report.usable                                 = door->usable;
        report.actorX                                 = actor.position.x;
        report.actorY                                 = actor.position.y;
        report.actorZ                                 = actor.position.z;
        report.event                                  = interaction.event;
        report.reportedOpen                           = interaction.open;
        report.reportedLocked                         = interaction.locked;
        const KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(*record->placement, report);

        if (verdict.refusal != nullptr) {
            logger->warn("Refused door {} on {:016x} by actor {}: {}", EventName(interaction.event), interaction.doorGuid, actor.GetNetworkID(), verdict.refusal);
            Correct(*door);
            return;
        }

        // The reporter worked a door it had not yet seen change: what it did
        // is what the door already is. Nothing is put to the scripts or
        // applied, and the reporter is told the door's state in case a bit of
        // its own lags. Viewers still watch the body push; they replay no
        // swing on a door that already stands there.
        if (verdict.actionCount == 0) {
            Correct(*door);
        }
        else {
            // Every change is put to the scripts before any of it is applied. A
            // handler that changes the door itself has decided for this report.
            const std::uint64_t doorNetworkId  = door->GetNetworkID();
            const std::uint64_t actorNetworkId = actor.GetNetworkID();
            const std::uint32_t tokenBefore    = door->stateToken;
            bool accepted                      = true;
            // A refused change ends the report: the push behind a refused
            // unlock never happens, so it is not put to the handlers.
            for (std::size_t index = 0; index < verdict.actionCount && accepted; ++index) {
                accepted = KCDC::Scripting::Events::DoorInteract(actorNetworkId, doorNetworkId, KCDC::Shared::Game::DoorActionName(verdict.actions[index]), verdict.keySide);
            }
            // A handler can take the door down, out of the world, or the actor
            // away: nothing from before the events is used without a look-up.
            auto *replication = Framework::CoreModules::GetReplication();
            door              = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(doorNetworkId) : nullptr;
            record            = door != nullptr ? FindRecord(door->doorGuid, door->copyWorld) : nullptr;
            if (record == nullptr || replication->GetEntityByNetworkID(actorNetworkId) == nullptr || IsRemoved(door->doorGuid, door->copyWorld)) {
                return;
            }
            if (!accepted || door->stateToken != tokenBefore) {
                Correct(*door);
                return;
            }

            if (door->open != verdict.open || door->locked != verdict.locked) {
                if (door->open != verdict.open) {
                    door->openedFromFront = interaction.fromFront;
                }
                door->open            = verdict.open;
                door->locked          = verdict.locked;
                door->changedByServer = false;
                ++door->stateToken;
            }
            if (!door->locked) {
                record->lockedByServer = false;
            }
        }

        KCDC::Shared::RPC::DoorInteraction relay = interaction;
        relay.actorNetworkId                     = actor.GetNetworkID();
        relay.open                               = verdict.open;
        relay.locked                             = verdict.locked;
        relay.copyWorld                          = door->copyWorld;
        RelayInteraction(relay, actor);
    }

    // Only a viewer with the acting body streamed in can replay its half --
    // exactly the peers that have it constructed. A viewer outside still gets
    // the door entity's state.
    std::size_t DoorService::RelayInteraction(KCDC::Shared::RPC::DoorInteraction &interaction, KCDC::Shared::Entities::HumanEntity &actor) const {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (replication == nullptr || net == nullptr) {
            return 0;
        }

        std::size_t recipients = 0;
        interaction.relayedAt  = MafiaNet::GetTime();
        replication->ForEachStreamingPeer(&actor, [&](MafiaNet::PeerGuid peer) {
            if (peer != actor.ownerGUID) {
                net->SendRPC(interaction, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
                ++recipients;
            }
        });
        return recipients;
    }

    void DoorService::SetLock(KCDC::Shared::Entities::DoorEntity &door, bool locked) {
        if (DoorRecord *record = FindRecord(door.doorGuid, door.GetVirtualWorld())) {
            record->lockedByServer = locked;
        }
        if (door.locked == locked) {
            return;
        }
        door.locked          = locked;
        door.changedByServer = true;
        ++door.stateToken;
        Framework::Logging::GetLogger(kLogger)->info("Door {:016x} set locked={} at token {}", door.doorGuid, locked, door.stateToken);
    }

    bool DoorService::SetOpen(KCDC::Shared::Entities::DoorEntity &door, bool open, bool fromFront) {
        if (door.open == open) {
            return false;
        }
        if (open && door.locked) {
            SetLock(door, false);
        }
        door.open            = open;
        door.openedFromFront = fromFront;
        door.changedByServer = true;
        ++door.stateToken;
        Framework::Logging::GetLogger(kLogger)->info("Door {:016x} set open={} fromFront={} at token {}", door.doorGuid, open, fromFront, door.stateToken);
        return true;
    }

    void DoorService::SetUsable(KCDC::Shared::Entities::DoorEntity &door, bool usable) {
        if (door.usable == usable) {
            return;
        }
        // A doorInteract handler that changes it has decided for the report
        // being asked about, as it has by moving or locking the door.
        door.usable = usable;
        ++door.stateToken;
        Framework::Logging::GetLogger(kLogger)->info("Door {:016x} set usable={} at token {}", door.doorGuid, usable, door.stateToken);
    }

    bool DoorService::CanNavigate(std::uint64_t guid, MafiaNet::VirtualWorldId world) const {
        // A door taken out leaves an open doorway.
        if (IsRemoved(guid, world)) {
            return true;
        }
        if (const auto *door = Find(guid, world)) {
            return !door->locked;
        }
        const auto placement = _catalog.find(guid);
        return placement == _catalog.end() || !placement->second->startsLocked;
    }

    Shared::Game::DoorPassage DoorService::PreparePassage(std::uint64_t guid, MafiaNet::VirtualWorldId world, const glm::vec3 &position, MafiaNet::Time now) {
        if (IsRemoved(guid, world)) {
            return Shared::Game::DoorPassage::Ready;
        }
        auto *door = Resolve(guid, world);
        if (!door) {
            return Shared::Game::DoorPassage::Blocked;
        }
        auto *record = FindRecord(guid, world);
        if (!door->open) {
            if (door->locked) {
                return Shared::Game::DoorPassage::Blocked;
            }
            SetOpen(*door, true, Shared::Game::DoorFrontDistance(*record->placement, position.x, position.y) >= 0.0F);
            // The server has no animation clock. Pause before walking the remaining
            // approach distance, giving replicas time to receive and swing the leaf.
            record->passageReadyAt = now + 1500;
        }
        return now < record->passageReadyAt ? Shared::Game::DoorPassage::Opening : Shared::Game::DoorPassage::Ready;
    }

    bool DoorService::QueryNearest(KCDC::Shared::Entities::HumanEntity &caller, NearestDoorHandler handler) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return false;
        }

        KCDC::Shared::RPC::DoorNearestQuery query;
        query.requestId = _nextNearestRequestId++;
        if (_nextNearestRequestId == 0) {
            _nextNearestRequestId = 1;
        }

        PendingNearest pending;
        pending.sender    = caller.ownerGUID;
        pending.expiresAt = MafiaNet::GetTime() + kNearestAnswerTimeout;
        pending.handler   = std::move(handler);
        _pendingNearest.insert_or_assign(query.requestId, std::move(pending));
        net->SendRPC(query, MafiaNet::ToGuid(caller.ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    void DoorService::SettleNearest(std::uint64_t requestId, const NearestDoor &answer) {
        const auto pending = _pendingNearest.find(requestId);
        if (pending == _pendingNearest.end()) {
            return;
        }

        // Dropped before the handler runs: it is script, and it can ask again.
        const NearestDoorHandler handler = std::move(pending->second.handler);
        _pendingNearest.erase(pending);
        if (handler) {
            handler(answer);
        }
    }

    void DoorService::ResolveNearestAnswer(const KCDC::Shared::RPC::DoorNearestAnswer &answer, MafiaNet::PeerGuid sender) {
        const auto pending = _pendingNearest.find(answer.requestId);
        if (pending == _pendingNearest.end() || pending->second.sender != sender) {
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *caller      = replication != nullptr ? replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(sender) : nullptr;
        if (caller == nullptr) {
            SettleNearest(answer.requestId, NearestDoor {false, "Your body is no longer on the server.", 0, {}, 0.0F, false, false});
            return;
        }
        if (!answer.found) {
            SettleNearest(answer.requestId, NearestDoor {false, "No door in sight.", 0, {}, 0.0F, false, false});
            return;
        }
        if (answer.levelHash != _levelHash) {
            SettleNearest(answer.requestId, NearestDoor {false, "That door belongs to another level.", 0, {}, 0.0F, false, false});
            return;
        }

        if (Resolve(answer.doorGuid, caller->GetVirtualWorld()) == nullptr) {
            SettleNearest(answer.requestId, NearestDoor {false, IsRemoved(answer.doorGuid, caller->GetVirtualWorld()) ? "That door has been taken out." : "The level places no such door.", 0, {}, 0.0F, false, false});
            return;
        }

        SettleNearest(answer.requestId, NearestDoor {true, {}, answer.doorGuid, answer.name, answer.distance, answer.open, answer.locked});
    }

    std::vector<KCDC::Shared::Entities::DoorEntity *> DoorService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        std::vector<KCDC::Shared::Entities::DoorEntity *> doors;
        doors.reserve(_index.size());
        for (const auto &entry : _index) {
            if (entry.second.entity == nullptr || (virtualWorld.has_value() && entry.first.virtualWorld != *virtualWorld) || !IsLive(entry.second) || IsRemoved(entry.first.doorGuid, entry.first.virtualWorld)) {
                continue;
            }
            doors.push_back(entry.second.entity);
        }
        return doors;
    }

    void DoorService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        // Doors belong to the level; only this player's operator queries
        // expire with them.
        std::vector<std::uint64_t> abandoned;
        for (const auto &entry : _pendingNearest) {
            if (entry.second.sender == guid) {
                abandoned.push_back(entry.first);
            }
        }
        for (const std::uint64_t requestId : abandoned) {
            SettleNearest(requestId, NearestDoor {false, "You left before your client answered.", 0, {}, 0.0F, false, false});
        }
    }
} // namespace KCDC::Features::Door
