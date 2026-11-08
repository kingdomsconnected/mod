#include "actor_spawner.h"

#include "spawn_budget.h"
#include "features/player/native_player_module.h"

#include <logging/logger.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "sdk/cryengine/action/i_actor.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_game_object_system.h"
#include "sdk/cryengine/entitysystem/c_entity_archetype_manager.h"
#include "sdk/cryengine/entitysystem/c_entity_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_archetype.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

namespace KCDC::Common::Spawn {
    const ActorClassEntry kSpawnableActorClasses[] = {
        {SDK::ActorClassNames::Horse, "Horse"},
        {SDK::ActorClassNames::Dog, "Dog"},
        {SDK::ActorClassNames::Wolf, "Wolf"},
        {SDK::ActorClassNames::WildDog, "Wild dog"},
        {SDK::ActorClassNames::NPC, "NPC"},
        {SDK::ActorClassNames::NPCNonAI, "NPC (no AI)"},
        {SDK::ActorClassNames::NPCFemale, "NPC (female)"},
        {SDK::ActorClassNames::DummyTarget, "Dummy target"},
    };

    const std::size_t kSpawnableActorClassCount = sizeof(kSpawnableActorClasses) / sizeof(kSpawnableActorClasses[0]);

    static_assert(kHorseActorClassIndex < (sizeof(kSpawnableActorClasses) / sizeof(kSpawnableActorClasses[0])), "kHorseActorClassIndex is out of range");

    const char *SpawnResultToString(SpawnResult result) {
        switch (result) {
        case SpawnResult::None: return "no request yet";
        case SpawnResult::Succeeded: return "spawned";
        case SpawnResult::NoEntitySystem: return "IEntitySystem is not available";
        case SpawnResult::NoLocalPlayer: return "no local player to place against";
        case SpawnResult::ClassNotRegistered: return "entity class is not registered";
        case SpawnResult::ArchetypeNotFound: return "archetype is not in the registry and its library did not resolve";
        case SpawnResult::SpawnRefused: return "SpawnEntity refused (spawn lock or spawn sink)";
        case SpawnResult::InitFailed: return "entity initialization failed or removed the entity";
        case SpawnResult::InvalidTransform: return "spawn transform is not finite or its rotation is degenerate";
        case SpawnResult::InvalidSoulGuid: return "soul must be a plain 8-4-4-4-12 GUID";
        case SpawnResult::NoScriptTable: return "the entity class has no script table to carry the soul";
        }
        return "unknown";
    }

    namespace {
        /**
         * Entity flags `CActorSystem::CreateActor` (0x180B86120) puts on a
         * channel-less actor, and the one it adds for a channel-bound one
         * (0x404000 in all).
         *
         * The first two flags enable trigger areas and movement within
         * them. The channel bit's name is not established; CreateActor adds
         * it whenever the channel id is non-zero.
         */
        constexpr std::uint32_t kLocalActorFlags         = 0x4000;
        constexpr std::uint32_t kLocalActorExtendedFlags = 4;
        constexpr std::uint32_t kChannelActorFlag        = 0x400000;

        /** Yaw-only quaternion whose entity forward (+Y at identity) is `direction`. */
        SDK::Quat YawTowards(float directionX, float directionY) {
            const float yaw  = std::atan2(-directionX, directionY);
            const float half = yaw * 0.5F;
            return SDK::Quat {0.0F, 0.0F, std::sin(half), std::cos(half)};
        }

        const char *EntityClassName(SDK::IEntityClass *entityClass) {
            const char *name = entityClass != nullptr ? entityClass->GetName() : nullptr;
            return name != nullptr ? name : "";
        }
    } // namespace

    bool ResolvePlacement(const PlacementParams &placement, WorldPlacement &out) {
        auto *framework = SDK::IGameFramework::GetInstance();
        if (framework == nullptr) {
            return false;
        }

        auto *player = framework->GetClientEntity();
        if (player == nullptr) {
            return false;
        }

        // Flatten the player's facing onto the ground plane; a camera-only
        // pitch must not tilt where the actor lands.
        const SDK::Vec3 &forward = player->GetForwardDir();
        float forwardX           = forward.x;
        float forwardY           = forward.y;

        const float length = std::sqrt((forwardX * forwardX) + (forwardY * forwardY));
        if (length < 1e-4F) {
            forwardX = 0.0F;
            forwardY = 1.0F;
        }
        else {
            forwardX /= length;
            forwardY /= length;
        }

        // Right-handed, Z-up: right = forward x up.
        const float rightX = forwardY;
        const float rightY = -forwardX;

        const SDK::Vec3 origin = player->GetWorldPos();
        out.position           = SDK::Vec3 {
            origin.x + (forwardX * placement.forwardDistance) + (rightX * placement.lateralOffset),
            origin.y + (forwardY * placement.forwardDistance) + (rightY * placement.lateralOffset),
            origin.z + placement.heightOffset,
        };

        out.rotation = placement.faceThePlayer ? YawTowards(-forwardX, -forwardY) : YawTowards(forwardX, forwardY);
        return true;
    }

    void ActorSpawner::LoadSoulCatalog() {
        _soulCatalog.Load();
    }

    void ActorSpawner::QueueSpawn(const char *actorClass, const char *soulGuid, const char *archetype, const PlacementParams &placement) {
        const bool hasArchetype = archetype != nullptr && *archetype != '\0';
        if ((actorClass == nullptr || *actorClass == '\0') && !hasArchetype) {
            return;
        }

        Command command;
        command.kind       = CommandKind::Spawn;
        command.actorClass = actorClass != nullptr ? actorClass : "";
        command.soulGuid   = soulGuid != nullptr ? soulGuid : "";
        command.archetype  = hasArchetype ? archetype : "";
        command.placement  = placement;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void ActorSpawner::QueueDespawn(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        Command command;
        command.kind     = CommandKind::Despawn;
        command.entityId = entityId;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void ActorSpawner::QueueDespawnAll() {
        Command command;
        command.kind = CommandKind::DespawnAll;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void ActorSpawner::QueueLoadArchetypeLibrary(const char *name) {
        if (name == nullptr || *name == '\0') {
            return;
        }

        Command command;
        command.kind      = CommandKind::LoadArchetypeLibrary;
        command.archetype = name;

        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void ActorSpawner::QueueRefreshArchetypes() {
        std::scoped_lock lock(_mutex);
        _archetypeRefreshRequested = true;
    }

    void ActorSpawner::Update() {
        std::vector<Command> commands;
        bool refreshArchetypes = false;
        {
            std::scoped_lock lock(_mutex);
            commands.swap(_pending);
            refreshArchetypes          = _archetypeRefreshRequested;
            _archetypeRefreshRequested = false;
        }

        // One budget for both queues. Tickets go first: a replicated body that
        // a player is waiting to see matters more than a debug spawn, and the
        // debug queue is almost always empty in a real session.
        SpawnBudget budget(kSpawnFrameBudget);

        // Indexed rather than iterated: a callback can Request another body,
        // and growing the vector would invalidate an iterator. Anything added
        // during this pass is left for the next frame, which the budget wanted
        // anyway.
        const std::size_t ticketCount = _tickets.size();
        for (std::size_t i = 0; i < ticketCount; ++i) {
            SpawnTicket *ticket = _tickets[i].get();
            if (ticket == nullptr || ticket->_finished) {
                continue;
            }
            if (budget.Exhausted()) {
                break;
            }

            budget.Charge();
            ExecuteTicket(*ticket);
        }

        // Tickets are destroyed here and nowhere else. A ticket's life ends
        // with its callback, which is why every caller drops its pointer there.
        _tickets.erase(std::remove_if(_tickets.begin(), _tickets.end(),
                           [](const std::unique_ptr<SpawnTicket> &ticket) {
                               return ticket == nullptr || ticket->_finished;
                           }),
            _tickets.end());

        // Commands keep their queued order rather than being sorted by kind.
        // The game's queue drains despawns before spawns, but it can only do
        // that because they live in separate vectors and no despawn can refer
        // to a spawn it shares a frame with; here `DespawnAll` queued behind a
        // spawn is meant to take that spawn with it.
        std::size_t executed = 0;
        for (const auto &command : commands) {
            if (budget.Exhausted()) {
                break;
            }

            switch (command.kind) {
            case CommandKind::Spawn: ExecuteSpawn(command); break;
            case CommandKind::Despawn: ExecuteDespawn(command.entityId); break;
            case CommandKind::DespawnAll: ExecuteDespawnAll(); break;
            case CommandKind::LoadArchetypeLibrary:
                ExecuteLoadArchetypeLibrary(command.archetype);
                refreshArchetypes = true;
                break;
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

        if (refreshArchetypes || ArchetypeCatalogIsStale()) {
            RefreshArchetypes();
        }

        PruneDeadHandles();
    }

    // Set before InitEntity, where the actor's soul is created.
    bool ActorSpawner::ApplySpawnProperties(SDK::IEntity *entity, const std::string &soulGuid, const std::string &soulArchetype, const std::vector<std::pair<std::string, std::variant<bool, std::string>>> &requested) {
        auto *proxy = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
        if (proxy == nullptr) {
            return false;
        }

        SDK::IScriptTable *scriptTable = proxy->GetScriptTable();
        if (scriptTable == nullptr) {
            return false;
        }

        // The entity's own table, where it shadows the class's field.
        if (!soulArchetype.empty()) {
            scriptTable->SetValue("defaultSoulArchetype", soulArchetype.c_str());
        }

        auto properties = scriptTable->GetTable("Properties");
        if (!properties) {
            return false;
        }

        if (entity->GetArchetype() != nullptr) {
            // Archetypes share Properties; detach before overriding the soul.
            properties = properties->CloneTable();
            if (!properties) {
                return false;
            }
            scriptTable->SetValue("Properties", properties.get());
        }
        if (!soulGuid.empty()) {
            properties->SetValue("guidSharedSoulId", soulGuid.c_str());
        }
        for (const auto &[key, value] : requested) {
            if (const bool *flag = std::get_if<bool>(&value)) {
                properties->SetValue(key.c_str(), *flag);
            }
            else {
                properties->SetValue(key.c_str(), std::get<std::string>(value).c_str());
            }
        }
        return true;
    }

    /**
     * The sequence `C_RiderPlayerControl::SpawnHorseActor` (0x1829F297C) and
     * `CActorSystem::CreateActor` (0x180B86120) use: resolve the class, fill
     * the params, spawn, initialize, and read the id from the entity.
     * The flags enable trigger areas and movement within them using KCD2's
     * own bit values.
     */
    SpawnResult ActorSpawner::SpawnAndInit(SDK::IEntityClass *entityClass, SDK::IEntityArchetype *archetype, const char *name, const WorldPlacement &placement, const std::string &soulGuid, const std::string &soulArchetype, const std::vector<std::pair<std::string, std::variant<bool, std::string>>> &properties, SDK::ChannelId channelId, SDK::EntityId &entityId) {
        entityId = 0;

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return SpawnResult::NoEntitySystem;
        }

        SDK::SEntitySpawnParams params {};
        // A channel-bound body is a remote player's: its constructor builds
        // the combat player before the channel is set, so the remote scope
        // is what tells the containment hooks this early.
        const bool channelBound = channelId != 0;
        const KCDC::Features::Player::NativePlayerModule::RemoteScope playerScope(channelBound);
        SDK::IGameObjectSystem::SEntitySpawnParamsForGameObjectWithPreactivatedExtension playerSpawn;
        if (channelBound) {
            // The game object factory runs this before the extension's Init,
            // as CActorSystem::CreateActor's own hook does. The id names no
            // CryNetwork channel, and nothing resolves it.
            playerSpawn.hookFunction = [](SDK::IEntity *, SDK::IGameObject *gameObject, void *data) -> bool {
                gameObject->SetChannelId(*static_cast<SDK::ChannelId *>(data));
                return true;
            };
            playerSpawn.pUserData = &channelId;
            params.userData       = &playerSpawn;
        }
        params.entityClass   = entityClass;
        params.archetype     = archetype;
        params.name          = name;
        // A body with a blank soul is spawned as spawned. With no EntityGuid
        // its soul is keyed by nothing, so every soul lookup for the body
        // mints another one, and without the spawned mark the soul outlives
        // the body -- both leak the soul table's 65535 slots as bodies stream
        // in and out. A body carrying a soul row is left as it was:
        // InitSoulFromEntity denies a spawned body the row of a live,
        // level-placed unique soul (C_Soul::IsPlayerSoul), which is what an
        // NPC, horse or dog replica of a story soul is.
        params.flags         = kLocalActorFlags | (soulGuid.empty() ? SDK::ENTITY_FLAG_SPAWNED : 0U) | (channelBound ? kChannelActorFlag : 0U);
        params.extendedFlags = kLocalActorExtendedFlags;
        params.position      = placement.position;
        params.rotation      = placement.rotation;
        params.scale         = SDK::Vec3 {1.0F, 1.0F, 1.0F};

        // Deliberately NOT under a C_ScopedEngineLock, although every native
        // that spawns in two steps holds one across exactly this sequence.
        // Taking it from this hook made every spawn fail: the entity came out
        // initialized but could not be found by id straight afterwards. A
        // native's locking is part of where it is called from, and this runs
        // from a CryAction PostUpdate hook rather than an engine spawn point.

        // Separate retryable spawn refusal from failed initialization.
        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            return SpawnResult::SpawnRefused;
        }

        const SDK::EntityId spawned = entity->GetId();
        if ((!soulGuid.empty() || !soulArchetype.empty() || !properties.empty()) && !ApplySpawnProperties(entity, soulGuid, soulArchetype, properties)) {
            entitySystem->RemoveEntity(spawned, false);
            return SpawnResult::NoScriptTable;
        }

        if (!entitySystem->InitEntity(entity, params)) {
            // The native initializer owns failure cleanup; do not reuse entity.
            return SpawnResult::InitFailed;
        }

        entityId = spawned;
        return SpawnResult::Succeeded;
    }

    bool ActorSpawner::LoadClassCharacter(SDK::IEntity *entity) {
        auto *proxy = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
        if (proxy == nullptr) {
            return false;
        }

        SDK::IScriptTable *scriptTable = proxy->GetScriptTable();
        auto properties                = scriptTable != nullptr ? scriptTable->GetTable("Properties") : SDK::ScriptTablePtr {};
        const char *model              = nullptr;
        if (!properties || !properties->GetValue("fileModel", model) || model[0] == 0) {
            return false;
        }

        return entity->LoadCharacter(0, model) >= 0 && entity->GetCharacter(0) != nullptr;
    }

    bool ActorSpawner::EnsureClassCharacter(SDK::IEntity *entity) {
        return entity->GetCharacter(0) != nullptr || LoadClassCharacter(entity);
    }

    void ActorSpawner::ExecuteSpawn(const Command &command) {
        SpawnRequest request;
        request.actorClass = command.actorClass;
        request.soulGuid   = command.soulGuid;
        request.archetype  = command.archetype;
        request.placement  = command.placement;

        const SpawnOutcome outcome = SpawnNow(request);
        if (outcome.actor.entityId == 0) {
            SetResult(outcome.result, 0);
            return;
        }

        std::scoped_lock lock(_mutex);
        _spawned.push_back(outcome.actor);
        _lastResult    = SpawnResult::Succeeded;
        _lastSpawnedId = outcome.actor.entityId;
    }

    SpawnTicket *ActorSpawner::Request(const SpawnRequest &request, SpawnTicket::FinishCallback onFinished) {
        auto ticket         = std::make_unique<SpawnTicket>();
        ticket->_request    = request;
        ticket->_onFinished = std::move(onFinished);

        SpawnTicket *handle = ticket.get();
        _tickets.push_back(std::move(ticket));
        return handle;
    }

    void ActorSpawner::Return(SpawnTicket *ticket) {
        if (ticket == nullptr) {
            return;
        }

        // Looked up rather than dereferenced. A ticket that has already run was
        // swept at the end of that frame, so a caller holding the old pointer
        // must not fault on it -- it simply finds nothing to cancel.
        for (auto &owned : _tickets) {
            if (owned.get() == ticket) {
                owned->_finished   = true;
                owned->_returned   = true;
                owned->_onFinished = nullptr;
                return;
            }
        }
    }

    void ActorSpawner::ExecuteTicket(SpawnTicket &ticket) {
        ticket._finished = true;

        // Moved out before the call so that a callback returning this ticket --
        // which clears `_onFinished` -- is not destroying the callable it is
        // running inside of.
        SpawnTicket::FinishCallback callback = std::move(ticket._onFinished);
        ticket._onFinished                   = nullptr;

        const SpawnOutcome outcome = SpawnNow(ticket._request);
        ticket._entityId           = outcome.actor.entityId;
        ticket._result             = outcome.result;

        SetResult(outcome.result, outcome.actor.entityId);

        // Building a body loads its models, and a load pumps the peer: the
        // replica that asked can be deleted and return this ticket before
        // `SpawnNow` comes back. Its callback would run on the freed requester
        // and bind it into the soul and life registries, so the body it asked
        // for is removed instead -- nobody is left to own it.
        if (ticket._returned) {
            QueueDespawn(ticket._entityId);
            return;
        }

        if (callback) {
            callback(ticket, ticket._entityId != 0);
        }
    }

    SpawnOutcome ActorSpawner::SpawnNow(const SpawnRequest &request) {
        SpawnOutcome outcome;

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            outcome.result = SpawnResult::NoEntitySystem;
            return outcome;
        }

        // An archetype supplies its own entity class.
        SDK::IEntityArchetype *archetype = nullptr;
        if (!request.archetype.empty()) {
            archetype = entitySystem->LoadEntityArchetype(request.archetype.c_str());
            if (archetype == nullptr) {
                outcome.result = SpawnResult::ArchetypeNotFound;
                return outcome;
            }
        }

        SDK::IEntityClass *entityClass = archetype != nullptr ? archetype->GetClass() : nullptr;
        if (entityClass == nullptr) {
            auto *registry = entitySystem->GetClassRegistry();
            entityClass    = registry != nullptr ? registry->FindClass(request.actorClass.c_str()) : nullptr;
        }

        if (entityClass == nullptr) {
            outcome.result = SpawnResult::ClassNotRegistered;
            return outcome;
        }

        const bool withSoul = !request.soulGuid.empty();
        if (withSoul && !IsSoulGuid(request.soulGuid)) {
            outcome.result = SpawnResult::InvalidSoulGuid;
            return outcome;
        }

        WorldPlacement placement;
        if (request.world) {
            placement = *request.world;
        }
        else if (!ResolvePlacement(request.placement, placement)) {
            outcome.result = SpawnResult::NoLocalPlayer;
            return outcome;
        }

        if (!NormalizeSpawnTransform(placement)) {
            outcome.result = SpawnResult::InvalidTransform;
            return outcome;
        }

        std::uint32_t ordinal = 0;
        {
            std::scoped_lock lock(_mutex);
            ordinal = ++_nameCounter;
        }

        char generated[128];
        std::snprintf(generated, sizeof(generated), "%s%s_%u", kSpawnedActorNamePrefix, EntityClassName(entityClass), ordinal);
        const std::string name = generated;

        auto *actorSystem = SDK::IActorSystem::GetInstance();

        // IActorSystem::CreateActor (0x180B86120) returns only an IActor.
        // Its IGameObjectExtension::m_entityId does not reliably identify the
        // spawned entity. SpawnAndInit retains the entity to read its id
        // directly and also accepts an archetype or soul GUID.
        SDK::EntityId entityId = 0;
        outcome.result         = SpawnAndInit(entityClass, archetype, name.c_str(), placement, request.soulGuid, request.soulArchetype, request.properties, request.channelId, entityId);
        if (entityId == 0) {
            if (outcome.result == SpawnResult::SpawnRefused) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Spawning entity class '{}' (archetype '{}') failed at SpawnEntity", EntityClassName(entityClass), request.archetype);
            }
            return outcome;
        }

        SDK::IEntity *entity = entitySystem->GetEntity(entityId);
        if (entity == nullptr || entity->IsGarbage()) {
            outcome.result = SpawnResult::InitFailed;
            return outcome;
        }

        outcome.actor.entityId    = entityId;
        outcome.actor.entityClass = EntityClassName(entity->GetClass());
        outcome.actor.archetype   = request.archetype;
        outcome.actor.name        = name;
        outcome.actor.soulGuid    = request.soulGuid;
        outcome.actor.isActor     = actorSystem != nullptr && actorSystem->GetActor(outcome.actor.entityId) != nullptr;
        outcome.result            = SpawnResult::Succeeded;

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
            ->info("Spawned '{}' (class {}, archetype '{}', soul '{}', actor {}) as entity {} at {:.2f}, {:.2f}, {:.2f}", outcome.actor.name, outcome.actor.entityClass, outcome.actor.archetype, outcome.actor.soulGuid, outcome.actor.isActor, outcome.actor.entityId,
                placement.position.x, placement.position.y, placement.position.z);
        return outcome;
    }

    void ActorSpawner::DespawnNow(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }

        // Deferred removal: the handle is dead to the caller the moment this
        // returns, even though the entity system drains the queue later.
        entitySystem->RemoveEntity(entityId, false);
    }

    void ActorSpawner::ExecuteDespawn(SDK::EntityId entityId) {
        DespawnNow(entityId);

        std::scoped_lock lock(_mutex);
        for (auto it = _spawned.begin(); it != _spawned.end(); ++it) {
            if (it->entityId == entityId) {
                _spawned.erase(it);
                break;
            }
        }
    }

    void ActorSpawner::ExecuteDespawnAll() {
        std::vector<SpawnedActor> owned;
        {
            std::scoped_lock lock(_mutex);
            owned.swap(_spawned);
        }

        for (const auto &actor : owned) {
            DespawnNow(actor.entityId);
        }
    }

    void ActorSpawner::ExecuteLoadArchetypeLibrary(const std::string &name) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }

        // A miss still loads the library named by the part before the first
        // `.`, which is the point: it registers everything that library holds.
        entitySystem->LoadEntityArchetype(name.c_str());
    }

    bool ActorSpawner::IsSoulGuid(const std::string &value) {
        static constexpr std::size_t kGroups[] = {8, 4, 4, 4, 12};

        std::size_t cursor = 0;
        for (std::size_t group = 0; group < 5; ++group) {
            if (group > 0) {
                if (cursor >= value.size() || value[cursor] != '-') {
                    return false;
                }
                ++cursor;
            }
            for (std::size_t i = 0; i < kGroups[group]; ++i) {
                if (cursor >= value.size() || std::isxdigit(static_cast<unsigned char>(value[cursor])) == 0) {
                    return false;
                }
                ++cursor;
            }
        }

        return cursor == value.size();
    }

    bool ActorSpawner::ArchetypeCatalogIsStale() const {
        // One pointer chase per frame. The registry only grows as archetypes
        // resolve, so a size change is enough to catch a level load pulling
        // new ones in.
        auto *entitySystem = SDK::CEntitySystem::GetInstance();
        auto *manager      = entitySystem != nullptr ? entitySystem->GetArchetypeManager() : nullptr;
        if (manager == nullptr) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        return manager->archetypeCount != _archetypes.size();
    }

    void ActorSpawner::RefreshArchetypes() {
        std::vector<ArchetypeEntry> catalog;

        auto *entitySystem = SDK::CEntitySystem::GetInstance();
        auto *manager      = entitySystem != nullptr ? entitySystem->GetArchetypeManager() : nullptr;
        if (manager != nullptr) {
            for (const auto *node = manager->FirstArchetype(); node != nullptr; node = SDK::EntityArchetypeManagerLayout::NextArchetype(node)) {
                if (node->archetype == nullptr) {
                    continue;
                }

                ArchetypeEntry entry;
                entry.name        = node->name != nullptr ? node->name : "";
                entry.entityClass = EntityClassName(node->archetype->GetClass());
                if (entry.name.empty()) {
                    continue;
                }
                catalog.push_back(std::move(entry));
            }

            std::sort(catalog.begin(), catalog.end(), [](const ArchetypeEntry &lhs, const ArchetypeEntry &rhs) {
                return lhs.name < rhs.name;
            });
        }

        std::scoped_lock lock(_mutex);
        _archetypes = std::move(catalog);
    }

    void ActorSpawner::PruneDeadHandles() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            // Between levels there is no entity system to ask; drop the
            // handles rather than reporting entities that cannot exist.
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

    void ActorSpawner::SetResult(SpawnResult result, SDK::EntityId entityId) {
        std::scoped_lock lock(_mutex);
        _lastResult    = result;
        _lastSpawnedId = entityId;
    }

    std::vector<SpawnedActor> ActorSpawner::GetSpawned() const {
        std::scoped_lock lock(_mutex);
        return _spawned;
    }

    std::vector<ArchetypeEntry> ActorSpawner::GetArchetypes() const {
        std::scoped_lock lock(_mutex);
        return _archetypes;
    }

    SpawnResult ActorSpawner::GetLastResult() const {
        std::scoped_lock lock(_mutex);
        return _lastResult;
    }

    SDK::EntityId ActorSpawner::GetLastSpawnedId() const {
        std::scoped_lock lock(_mutex);
        return _lastSpawnedId;
    }
} // namespace KCDC::Common::Spawn
