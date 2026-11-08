#include "prop_spawner.h"

#include "spawn_budget.h"

#include <logging/logger.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>

#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/pe_body_actions.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

namespace KCDC::Common::Spawn {
    const char *PropSpawnResultToString(PropSpawnResult result) {
        switch (result) {
        case PropSpawnResult::None: return "no request yet";
        case PropSpawnResult::Succeeded: return "spawned";
        case PropSpawnResult::NoEntitySystem: return "IEntitySystem is not available";
        case PropSpawnResult::NoLocalPlayer: return "no local player to place against";
        case PropSpawnResult::NoModel: return "no model chosen";
        case PropSpawnResult::ClassNotRegistered: return "BasicEntity is not registered";
        case PropSpawnResult::SpawnRefused: return "SpawnEntity refused (spawn lock or spawn sink)";
        case PropSpawnResult::InitFailed: return "entity initialization failed or removed the entity";
        case PropSpawnResult::InvalidTransform: return "spawn transform or scale is invalid";
        case PropSpawnResult::InvalidPhysics: return "unknown prop physics mode";
        case PropSpawnResult::ModelLoadFailed: return "the script loaded no static mesh into slot 0";
        case PropSpawnResult::PhysicsUnavailable: return "the model could not create collision; use no physics for decoration";
        case PropSpawnResult::NoScriptTable: return "the entity has no Properties table to carry the model";
        }
        return "unknown";
    }

    void PropSpawner::QueueSpawn(const PropSpawnRequest &request) {
        if (request.model.empty()) {
            SetResult(PropSpawnResult::NoModel, 0);
            return;
        }

        Command command;
        command.kind    = CommandKind::Spawn;
        command.request = request;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void PropSpawner::QueueDespawn(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        Command command;
        command.kind     = CommandKind::Despawn;
        command.entityId = entityId;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void PropSpawner::QueueDespawnAll() {
        Command command;
        command.kind = CommandKind::DespawnAll;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void PropSpawner::Update() {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(_mutex);
            commands.swap(_pending);
        }

        SpawnBudget budget(kSpawnFrameBudget);

        // Tickets first, then the debug queue, exactly as in ActorSpawner.
        // Indexed rather than iterated: a callback can Request another prop.
        const std::size_t ticketCount = _tickets.size();
        for (std::size_t i = 0; i < ticketCount; ++i) {
            PropSpawnTicket *ticket = _tickets[i].get();
            if (ticket == nullptr || ticket->_finished) {
                continue;
            }
            if (budget.Exhausted()) {
                break;
            }

            budget.Charge();
            ExecuteTicket(*ticket);
        }

        _tickets.erase(std::remove_if(_tickets.begin(), _tickets.end(),
                           [](const std::unique_ptr<PropSpawnTicket> &ticket) {
                               return ticket == nullptr || ticket->_finished;
                           }),
            _tickets.end());

        // Queued order is preserved for the same reason as in ActorSpawner:
        // a `DespawnAll` behind a spawn is meant to take that spawn with it.
        std::size_t executed = 0;
        for (const auto &command : commands) {
            if (budget.Exhausted()) {
                break;
            }

            switch (command.kind) {
            case CommandKind::Spawn: ExecuteSpawn(command.request); break;
            case CommandKind::Despawn: ExecuteDespawn(command.entityId); break;
            case CommandKind::DespawnAll: ExecuteDespawnAll(); break;
            }

            budget.Charge();
            ++executed;
        }

        // Whatever the budget cut off goes back in front of anything queued
        // from another thread while this frame was draining.
        if (executed < commands.size()) {
            std::scoped_lock lock(_mutex);
            _pending.insert(_pending.begin(), std::make_move_iterator(commands.begin() + static_cast<std::ptrdiff_t>(executed)), std::make_move_iterator(commands.end()));
        }

        PruneDeadHandles();
    }

    bool PropSpawner::ApplyPropProperties(SDK::IEntity *entity, const PropSpawnRequest &request) {
        auto *proxy = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
        if (proxy == nullptr) {
            return false;
        }

        SDK::IScriptTable *scriptTable = proxy->GetScriptTable();
        if (scriptTable == nullptr) {
            return false;
        }

        auto properties = scriptTable->GetTable("Properties");
        if (!properties) {
            return false;
        }

        properties->SetValue("object_Model", request.model.c_str());

        auto physics = properties->GetTable("Physics");
        if (!physics) {
            return false;
        }

        // Lua treats numeric zero as truthy; these must be booleans.
        physics->SetValue("bPhysicalize", request.physics != PropPhysics::None);
        physics->SetValue("bRigidBody", request.physics == PropPhysics::Rigid);

        // BasicEntity hands `Mass` and `Density` to the body it physicalizes in `OnSpawn`; -1, the class default, takes them from the mesh.
        if (request.physics == PropPhysics::Rigid && request.mass > 0.0F) {
            physics->SetValue("Mass", request.mass);
            physics->SetValue("Density", -1.0F);
        }

        return true;
    }

    bool PropSpawner::EnsureCompoundPhysics(SDK::IEntity *entity, float scale) {
        SDK::IPhysicalEntity *physics = entity->GetPhysics();
        SDK::pe_status_nparts parts;
        if (physics == nullptr) {
            return false;
        }
        if (physics->GetStatus(&parts) > 0) {
            return true;
        }

        // BasicEntity creates the body, but CPhysicalProxy::AddSlotGeometry
        // rejects a compound root whose own physics list is empty. The
        // model's Physicalize handles its children, as the brush path does.
        SDK::IStatObj *mesh = entity->GetStatObj(static_cast<int>(0x80000000u));
        if (mesh != nullptr && (mesh->GetFlags() & SDK::IStatObj::kCompound) != 0 && !mesh->IsPhysicsExist()) {
            SDK::Matrix34 local = SDK::Matrix34FromTransform({}, {0.0F, 0.0F, 0.0F, 1.0F}, {scale, scale, scale});
            SDK::pe_geomparams params;
            params.density = -1.0F;
            params.mass    = -1.0F;
            params.pMtx3x4 = &local;
            mesh->Physicalize(physics, &params);
            return physics->GetStatus(&parts) > 0;
        }
        return false;
    }

    bool PropSpawner::EnsurePhysics(SDK::IEntity *entity, const PropSpawnRequest &request) {
        if (request.physics == PropPhysics::None) {
            return true;
        }
        if (EnsureCompoundPhysics(entity, request.scale)) {
            return true;
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Prop '{}' asked for {} physics but has no collision parts", request.model, PropPhysicsToString(request.physics));
        return false;
    }

    PropSpawnTicket *PropSpawner::Request(const PropSpawnRequest &request, PropSpawnTicket::FinishCallback onFinished) {
        auto ticket         = std::make_unique<PropSpawnTicket>();
        ticket->_request    = request;
        ticket->_onFinished = std::move(onFinished);

        PropSpawnTicket *handle = ticket.get();
        _tickets.push_back(std::move(ticket));
        return handle;
    }

    void PropSpawner::Return(PropSpawnTicket *ticket) {
        if (ticket == nullptr) {
            return;
        }

        // Looked up rather than dereferenced, so a caller holding the pointer
        // of a ticket that already ran finds nothing rather than faulting.
        for (auto &owned : _tickets) {
            if (owned.get() == ticket) {
                owned->_finished   = true;
                owned->_onFinished = nullptr;
                return;
            }
        }
    }

    void PropSpawner::ExecuteTicket(PropSpawnTicket &ticket) {
        ticket._finished = true;

        // Moved out before the call so that a callback returning this ticket is
        // not destroying the callable it is running inside of.
        PropSpawnTicket::FinishCallback callback = std::move(ticket._onFinished);
        ticket._onFinished                       = nullptr;

        const PropSpawnOutcome outcome = SpawnNow(ticket._request);
        ticket._entityId               = outcome.prop.entityId;
        ticket._result                 = outcome.result;

        SetResult(outcome.result, outcome.prop.entityId);

        if (callback) {
            callback(ticket, ticket._entityId != 0);
        }
    }

    PropSpawnOutcome PropSpawner::SpawnNow(const PropSpawnRequest &request) {
        PropSpawnOutcome outcome;

        if (request.model.empty()) {
            outcome.result = PropSpawnResult::NoModel;
            return outcome;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            outcome.result = PropSpawnResult::NoEntitySystem;
            return outcome;
        }

        auto *registry                 = entitySystem->GetClassRegistry();
        SDK::IEntityClass *entityClass = registry != nullptr ? registry->FindClass(SDK::EntityClassNames::BasicEntity) : nullptr;
        if (entityClass == nullptr) {
            outcome.result = PropSpawnResult::ClassNotRegistered;
            return outcome;
        }

        WorldPlacement placement;
        if (request.world) {
            placement = *request.world;
        }
        else if (!ResolvePlacement(request.placement, placement)) {
            outcome.result = PropSpawnResult::NoLocalPlayer;
            return outcome;
        }

        if (!NormalizeSpawnTransform(placement, request.scale)) {
            outcome.result = PropSpawnResult::InvalidTransform;
            return outcome;
        }
        if (request.physics != PropPhysics::Rigid && request.physics != PropPhysics::Static && request.physics != PropPhysics::None) {
            outcome.result = PropSpawnResult::InvalidPhysics;
            return outcome;
        }

        std::uint32_t ordinal = 0;
        {
            std::scoped_lock lock(_mutex);
            ordinal = ++_nameCounter;
        }

        char generated[128];
        std::snprintf(generated, sizeof(generated), "KCDC_Prop_%u", ordinal);
        const std::string name = generated;

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.name        = name.c_str();
        params.position    = placement.position;
        params.rotation    = placement.rotation;
        params.scale       = SDK::Vec3 {request.scale, request.scale, request.scale};

        // Level entities get this from the loader; a runtime spawn casts no
        // shadow without it.
        params.flags = SDK::ENTITY_FLAG_CASTSHADOW;

        // Set properties before InitEntity calls BasicEntity:OnSpawn.
        //
        // Not under a C_ScopedEngineLock: see ActorSpawner::SpawnAndInit for
        // why holding one across this sequence breaks the spawn. The game's own
        // runtime prop spawn, `C_HolsterProp::SpawnModelProp` (0x181F233B0),
        // avoids the two-step entirely by handing its Properties table to
        // `SEntitySpawnParams::pPropertiesTable` and spawning with
        // `autoInit = true`; that route replaces the class defaults wholesale
        // instead of overriding two keys, so it is not worth the behaviour
        // change here.
        SDK::IEntity *spawned = entitySystem->SpawnEntity(params, false);
        if (spawned == nullptr) {
            outcome.result = PropSpawnResult::SpawnRefused;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Spawning prop '{}' failed at SpawnEntity", request.model);
            return outcome;
        }

        if (!ApplyPropProperties(spawned, request)) {
            entitySystem->RemoveEntity(spawned->GetId(), false);
            outcome.result = PropSpawnResult::NoScriptTable;
            return outcome;
        }

        const SDK::EntityId entityId = spawned->GetId();
        if (!entitySystem->InitEntity(spawned, params)) {
            // InitEntity queues the entity for deletion on failure.
            outcome.result = PropSpawnResult::InitFailed;
            return outcome;
        }

        SDK::IEntity *entity = entitySystem->GetEntity(entityId);
        if (entity == nullptr || entity->IsGarbage()) {
            outcome.result = PropSpawnResult::InitFailed;
            return outcome;
        }

        // InitEntity ignores script failure. Check the actual slot, including
        // compound meshes, before reporting success. A `.chr`, `.cdf` or
        // `.cga` is loaded as a character instead (the LoadObject binding
        // dispatches on the extension, 0x1820F8F10), so it has no mesh to ask.
        if (entity->GetStatObj(static_cast<int>(0x80000000u)) == nullptr && entity->GetCharacter(0) == nullptr) {
            entitySystem->RemoveEntity(entityId, false);
            outcome.result = PropSpawnResult::ModelLoadFailed;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Prop '{}' initialized but loaded no mesh; entity {} was removed", request.model, entityId);
            return outcome;
        }

        if (!EnsurePhysics(entity, request)) {
            entitySystem->RemoveEntity(entityId, false);
            outcome.result = PropSpawnResult::PhysicsUnavailable;
            return outcome;
        }

        // A weightless rigid body floats off at the first push and never
        // comes to rest, so it is not built as one. Maps made before this
        // check stand walls and gates as rigid, and they stood still, so such
        // a prop stands as a static one rather than disappearing.
        SDK::pe_status_dynamics dynamics;
        SDK::IPhysicalEntity *body = request.physics == PropPhysics::Rigid ? entity->GetPhysics() : nullptr;
        if (body != nullptr && (body->GetStatus(&dynamics) == 0 || !(dynamics.mass > 0.0F))) {
            entitySystem->RemoveEntity(entityId, false);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Prop '{}' has no mass to fall or be pushed with, so it stands as a static prop; give it a mass to make it rigid", request.model);
            PropSpawnRequest standing = request;
            standing.physics          = PropPhysics::Static;
            return SpawnNow(standing);
        }

        outcome.prop.entityId = entityId;
        outcome.prop.model    = request.model;
        outcome.prop.name     = name;
        outcome.prop.physics  = request.physics;
        outcome.prop.scale    = request.scale;
        outcome.result        = PropSpawnResult::Succeeded;

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
            ->info("Spawned prop '{}' ({}, scale {:.2f}) as entity {} at {:.2f}, {:.2f}, {:.2f}", request.model, PropPhysicsToString(request.physics), request.scale, entityId, placement.position.x, placement.position.y, placement.position.z);

        return outcome;
    }

    void PropSpawner::ExecuteSpawn(const PropSpawnRequest &request) {
        const PropSpawnOutcome outcome = SpawnNow(request);
        if (outcome.prop.entityId == 0) {
            SetResult(outcome.result, 0);
            return;
        }

        std::scoped_lock lock(_mutex);
        _spawned.push_back(outcome.prop);
        _lastResult    = PropSpawnResult::Succeeded;
        _lastSpawnedId = outcome.prop.entityId;
    }

    void PropSpawner::DespawnNow(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }

        entitySystem->RemoveEntity(entityId, false);
    }

    void PropSpawner::ExecuteDespawn(SDK::EntityId entityId) {
        DespawnNow(entityId);

        std::scoped_lock lock(_mutex);
        for (auto it = _spawned.begin(); it != _spawned.end(); ++it) {
            if (it->entityId == entityId) {
                _spawned.erase(it);
                break;
            }
        }
    }

    void PropSpawner::ExecuteDespawnAll() {
        std::vector<SpawnedProp> owned;
        {
            std::scoped_lock lock(_mutex);
            owned.swap(_spawned);
        }

        for (const auto &prop : owned) {
            DespawnNow(prop.entityId);
        }
    }

    void PropSpawner::PruneDeadHandles() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            // Between levels there is nothing to ask; the handles are dead.
            std::scoped_lock lock(_mutex);
            _spawned.clear();
            return;
        }

        std::scoped_lock lock(_mutex);
        for (auto it = _spawned.begin(); it != _spawned.end();) {
            if (entitySystem->GetEntity(it->entityId) == nullptr) {
                it = _spawned.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void PropSpawner::SetResult(PropSpawnResult result, SDK::EntityId entityId) {
        std::scoped_lock lock(_mutex);
        _lastResult    = result;
        _lastSpawnedId = entityId;
    }

    std::vector<SpawnedProp> PropSpawner::GetSpawned() const {
        std::scoped_lock lock(_mutex);
        return _spawned;
    }

    PropSpawnResult PropSpawner::GetLastResult() const {
        std::scoped_lock lock(_mutex);
        return _lastResult;
    }

    SDK::EntityId PropSpawner::GetLastSpawnedId() const {
        std::scoped_lock lock(_mutex);
        return _lastSpawnedId;
    }
} // namespace KCDC::Common::Spawn
