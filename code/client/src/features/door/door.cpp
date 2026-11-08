#include "door.h"

#include "common/module_support.h"
#include "door_service.h"

#include "core/application.h"

#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include "shared/game/door_catalog.h"

#include <logging/logger.h>

#include <algorithm>
#include <cstdio>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger = "Door";

        constexpr std::uint64_t kRetryBaseMs    = 100;
        constexpr std::uint64_t kRetryMaximumMs = 1600;

        /** A hung door that will not build after this many tries is not going to. */
        constexpr std::uint32_t kMaxBuildAttempts = 5;

        /** `esLockTypes` as AnimDoor.lua compares it. */
        const char *LockTypesValue(Shared::Game::DoorLockType lock) {
            switch (lock) {
            case Shared::Game::DoorLockType::None: return "none/none";
            case Shared::Game::DoorLockType::Manual: return "manual/none";
            case Shared::Game::DoorLockType::ManualKey: return "manual/key";
            case Shared::Game::DoorLockType::Key: return "key/key";
            }
            return "manual/key";
        }
    } // namespace

    void Door::DeallocReplica(MafiaNet::Connection_RM3 *) {
        // The level owns its own doors; only the binding goes away. A hung
        // door's entity is ours, removed on the tick: a replica can go from
        // the loading-time peer pump, mid-way through the entity system's work.
        if (_builtEntityId != 0 && KCDC::Core::gApplication) {
            KCDC::Core::gApplication->GetDoorService().QueueDespawn(_builtEntityId);
        }
        delete this;
    }

    void Door::BuildNative() {
        if (!IsSpawned() || _buildRefused) {
            return;
        }
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }
        if (_builtEntityId != 0) {
            SDK::IEntity *built = entitySystem->GetEntity(_builtEntityId);
            if (built != nullptr && !built->IsGarbage()) {
                return;
            }
            _builtEntityId = 0;
        }
        if (entitySystem->GetEntityByGuid(doorGuid) != nullptr) {
            return;
        }
        if (++_buildAttempts > kMaxBuildAttempts) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Gave up building hung door {:#x} after {} attempts", doorGuid, kMaxBuildAttempts);
            return;
        }

        const Shared::Game::DoorModel *model = Shared::Game::DoorModelAt(spawned.model);
        SDK::IEntityClass *entityClass       = entitySystem->GetClassRegistry() != nullptr ? entitySystem->GetClassRegistry()->FindClass(SDK::EntityClassNames::AnimDoor) : nullptr;
        if (model == nullptr || entityClass == nullptr) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cannot build hung door {:#x}: {}", doorGuid, model == nullptr ? "this build's catalog has no such door model" : "the AnimDoor class is not registered");
            return;
        }

        char name[48];
        std::snprintf(name, sizeof(name), "KCDC_Door_%016llx", static_cast<unsigned long long>(doorGuid));

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.guid        = doorGuid;
        params.name        = name;
        params.position    = SDK::Vec3 {position.x, position.y, position.z};
        params.rotation    = SDK::Quat {spawned.rotation.x, spawned.rotation.y, spawned.rotation.z, spawned.rotation.w};

        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("SpawnEntity refused hung door {:#x}", doorGuid);
            return;
        }
        SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entity);
        SDK::ScriptTablePtr props = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        // Each nested table the entity's own, inheriting the class's values (C_ScriptTableTemplate, 0x180718B38).
        SDK::ScriptTablePtr lock    = props ? props->GetTable("Lock") : SDK::ScriptTablePtr {};
        SDK::ScriptTablePtr physics = props ? props->GetTable("Physics") : SDK::ScriptTablePtr {};
        if (!props || !lock || !physics) {
            entitySystem->RemoveEntity(entity->GetId(), false);
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Hung door {:#x} has no AnimDoor Properties table to carry its model", doorGuid);
            return;
        }

        // What every one of the level's placements of this leaf gives it.
        props->SetValue("object_Model", model->path);
        props->SetValue("esDoorAnimSet", model->animSet);
        props->SetValue("soclass_SmartObjectHelpers", model->helpers);
        props->SetValue("guidSmartObjectType", model->smartObjectType);
        physics->SetValue("bRigidBody", true);
        physics->SetValue("bPushableByPlayers", true);

        // The lock the server judges uses by. No home area links a hung door,
        // so nothing may lock it for one: AnimDoor.lua would otherwise bolt
        // it at night for want of an owner.
        lock->SetValue("bLocked", locked);
        lock->SetValue("esLockTypes", LockTypesValue(spawned.lockType));
        lock->SetValue("bCanLockPick", spawned.lockpickable);
        lock->SetValue("bCanUnlockWithDynamicKey", false);
        lock->SetValue("bDoLockOnMissingHomeArea", false);
        lock->SetValue("bNeverLockByPassingNPC", true);
        lock->SetValue("bLockInside", false);
        lock->SetValue("bLockOutside", false);
        lock->SetValue("guidItemClassId", spawned.keyItem.c_str());
        if (spawned.lockDifficulty >= 0.0F) {
            lock->SetValue("fLockDifficulty", spawned.lockDifficulty);
            lock->SetValue("bLockDifficultyOverride", true);
        }

        const SDK::EntityId spawnedId = entity->GetId();
        // InitEntity queues the entity for deletion on failure.
        if (!entitySystem->InitEntity(entity, params)) {
            Framework::Logging::GetLogger(kLogger)->warn("InitEntity refused hung door {:#x} ('{}')", doorGuid, model->path);
            return;
        }
        _builtEntityId = spawnedId;
        // The attempts are for one build: a door built again after a level change starts over.
        _buildAttempts = 0;
        Unbind();
        Framework::Logging::GetLogger(kLogger)->info("Built hung door {:#x} ('{}') as entity {}", doorGuid, model->path, spawnedId);
    }

    void Door::DestroyNative() {
        if (_builtEntityId == 0) {
            return;
        }
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            entitySystem->RemoveEntity(_builtEntityId, false);
        }
        _builtEntityId = 0;
        Unbind();
    }

    void Door::NoteApplyFailed(std::uint64_t now) {
        const std::uint32_t step = std::min(_failedAttempts, 4U);
        _retryAt                 = now + std::min(kRetryBaseMs << step, kRetryMaximumMs);
        if (_failedAttempts < 5) {
            ++_failedAttempts;
        }
    }
} // namespace KCDC::Features::Door
