#pragma once

#include "sdk/types.h"
#include "soul_catalog.h"
#include "spawn_transform.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_archetype.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"

#include <function2/function2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <variant>
#include <vector>

namespace KCDC::Common::Spawn {
    /** One entry of the debug panel's actor-class picker. */
    struct ActorClassEntry {
        const char *className;
        const char *displayName;
    };

    /** Actor classes registered by the game's factory pass. */
    extern const ActorClassEntry kSpawnableActorClasses[];
    extern const std::size_t kSpawnableActorClassCount;

    /** Index of ActorClassNames::Horse inside kSpawnableActorClasses. */
    inline constexpr std::size_t kHorseActorClassIndex = 0;

    /**
     * Spawn-params name prefix on every actor ActorSpawner spawns. The only
     * thing separating a copy from the level's own population, which is the
     * same entity class with the same flags; the population sink reads it.
     */
    inline constexpr const char *kSpawnedActorNamePrefix = "KCDC_";

    /** Spawn placement expressed relative to the local player. */
    struct PlacementParams {
        float forwardDistance = 4.0F;
        float lateralOffset   = 0.0F;
        float heightOffset    = 0.0F;
        bool faceThePlayer    = true;
    };

    /** Stage the last spawn attempt reached, surfaced by the debug panel. */
    enum class SpawnResult : std::uint8_t {
        None,
        Succeeded,
        NoEntitySystem,
        NoLocalPlayer,
        ClassNotRegistered,
        ArchetypeNotFound,
        SpawnRefused,
        InitFailed,
        InvalidTransform,
        InvalidSoulGuid,
        NoScriptTable,
    };

    const char *SpawnResultToString(SpawnResult result);

    inline bool IsRetryableSpawnResult(SpawnResult result) {
        return result == SpawnResult::NoEntitySystem || result == SpawnResult::ClassNotRegistered || result == SpawnResult::SpawnRefused;
    }

    /** Game thread only. Returns false when no local player is available. */
    bool ResolvePlacement(const PlacementParams &placement, WorldPlacement &out);

    /** What QueueSpawn takes, bundled for the game-thread entry point. */
    struct SpawnRequest {
        std::string actorClass;
        std::string soulGuid;  // empty spawns a blank soul
        std::string archetype; // empty spawns from the bare entity class

        /**
         * Script properties written onto `Properties` before initialization,
         * beside the soul: what the class's own Lua reads in its
         * `OnSpawn`/`Reset`, such as a horse's `bNotPlayerMountable`, and
         * what the actor records its model from on first setup
         * (`fileModel`, read by `ReadActorScriptProperties` 0x180AE5C68).
         */
        std::vector<std::pair<std::string, std::variant<bool, std::string>>> properties;

        /**
         * The soul archetype the body's soul is created from, written onto
         * the entity's own script table as `defaultSoulArchetype`. Soul setup
         * reads it from there (0x180AD83DC -> soul+0x374), where a class
         * table sets it (`NPC_Female.lua`), and the archetype decides the
         * gender the soul's default body, head and hair come from. Empty
         * leaves the class's.
         */
        std::string soulArchetype;

        /**
         * The channel id the body is spawned on, as `CActorSystem::CreateActor`
         * takes one; 0 for a body nobody drives.
         *
         * A non-zero id is a remote player's body: the game object gets it
         * before `C_Player::Init`, which is what makes the actor player
         * controlled and keeps it from being put away inside its own Init,
         * and the spawn runs in the native player module's remote scope.
         * Allocate it from `NativePlayerModule::AllocateChannelId`.
         */
        SDK::ChannelId channelId = 0;

        // Player-relative placement, used when `world` is unset.
        PlacementParams placement {};

        // Overrides relative placement; does not require a local player.
        std::optional<WorldPlacement> world;
    };

    /** A live spawn owned by this mod. */
    struct SpawnedActor {
        SDK::EntityId entityId = 0;
        std::string entityClass;
        std::string archetype; // empty when spawned from a bare entity class
        std::string name;
        std::string soulGuid; // empty when spawned without a soul
        bool isActor = false;
    };

    /** One entry of the game's entity-archetype registry. */
    struct ArchetypeEntry {
        std::string name;        // `<library>.<Name>`
        std::string entityClass; // class the archetype is bound to
    };

    /** Result of an immediate spawn; `actor.entityId` is 0 when it failed. */
    struct SpawnOutcome {
        SpawnedActor actor;
        SpawnResult result = SpawnResult::None;
    };

    class ActorSpawner;

    /**
     * A body that has been asked for and has not necessarily been made yet.
     *
     * A replica requests a body through this ticket. The spawner paces
     * entity creation, initialization and clothing loads against
     * `kSpawnFrameBudget`, then calls the ticket back when the body exists.
     *
     * Game thread only. The ticket lives until its callback has run or it is
     * returned, whichever comes first -- so drop the pointer inside the
     * callback, and `Return` it before dying if the callback never came. The
     * callback holds whoever asked.
     */
    class SpawnTicket final {
        friend class ActorSpawner;

      public:
        /**
         * Called once, on the frame the request ran. `true` means the body
         * exists and `GetEntityId` names it; `false` means the attempt failed
         * and `GetResult` says how.
         */
        using FinishCallback = fu2::function<void(SpawnTicket &, bool) const>;

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** `None` until the request has run. */
        SpawnResult GetResult() const {
            return _result;
        }

      private:
        SpawnRequest _request;
        FinishCallback _onFinished;
        SDK::EntityId _entityId = 0;
        SpawnResult _result     = SpawnResult::None;

        // Set once the ticket can never fire again, whether it ran or was
        // returned. `Update` sweeps on it after the drain, which is what lets a
        // callback return its own ticket without the object dying under it.
        bool _finished = false;

        // Set by `Return` alone: the requester is gone, even if the ticket is
        // running at that moment.
        bool _returned = false;
    };

    /**
     * Native actor spawns. Update drains queued commands on the game thread.
     * Stored handles are EntityIds; resolve IEntity pointers on each use.
     */
    class ActorSpawner final {
      public:
        /** Loads the shipped soul catalog. Call once during PostInit. */
        void LoadSoulCatalog();

        const SoulCatalog &GetSoulCatalog() const {
            return _soulCatalog;
        }

        // Callable from any thread.

        /**
         * Sets Properties.guidSharedSoulId before initialization. An empty
         * GUID creates a blank soul.
         */
        void QueueSpawn(const char *actorClass, const char *soulGuid, const char *archetype, const PlacementParams &placement);
        void QueueDespawn(SDK::EntityId entityId);
        void QueueDespawnAll();

        /**
         * Resolves `name` through IEntitySystem::LoadEntityArchetype, which
         * registers every archetype in the library named by the part before
         * the first `.`. Refreshes the catalog afterwards.
         */
        void QueueLoadArchetypeLibrary(const char *name);
        void QueueRefreshArchetypes();

        // Game thread only.

        /**
         * Returns a caller-owned entity outside the debug list. The caller
         * must despawn it; the returned entity ID is zero on failure.
         *
         * Spawns and dresses on the calling frame, so a caller that may issue
         * many at once wants `Request` instead.
         */
        SpawnOutcome SpawnNow(const SpawnRequest &request);

        /**
         * Asks for a body and hands back a ticket for it straight away. The
         * spawner executes the request on a later frame, within the per-frame
         * budget, and calls `onFinished` once when it has.
         *
         * This is the path every replicated body takes. `Return` the ticket
         * before the caller dies: `onFinished` holds it.
         */
        SpawnTicket *Request(const SpawnRequest &request, SpawnTicket::FinishCallback onFinished);

        /**
         * Drops a ticket. A request that has not run yet never will; one that
         * already produced a body leaves that body alone, because by then it
         * belongs to whatever asked for it. Safe with a null ticket.
         */
        void Return(SpawnTicket *ticket);

        /**
         * Loads Properties.fileModel after entity initialization. Loading it
         * before C_Actor::Init is unsafe.
         *
         * Straight into slot 0, past `C_Actor::LoadModel`, so the actor never
         * records it: a body woken afterwards has `SetupActorModel` see an
         * empty current model and load the same file a second time. Use
         * `EnsureClassCharacter` after the wake instead.
         */
        static bool LoadClassCharacter(SDK::IEntity *entity);

        /**
         * Whether a woken body has its character, loading the class's only
         * when the wake did not.
         *
         * A body spawned with a soul row starts put away (`C_Actor::Init` ->
         * `SetPutAway(true)`, whose put-away branch releases slot 0), and the
         * wake branch runs `SetupActorModel` (0x180AE55E0), which records and
         * loads the model from Properties -- once. A blank-soul body carries
         * ENTITY_FLAG_SPAWNED, so Init skips the put-away and runs the same
         * setup (slot 247) itself, and the wake finds nothing to do. Either
         * way, a class whose setup loads nothing still gets
         * `LoadClassCharacter`.
         */
        static bool EnsureClassCharacter(SDK::IEntity *entity);

        /** Queues an entity spawned by SpawnNow for removal. */
        void DespawnNow(SDK::EntityId entityId);

        /** True when a string is a plain 8-4-4-4-12 GUID. */
        static bool IsSoulGuid(const std::string &value);

        // Game thread only; driven from CCryAction::PostUpdate.
        void Update();

        std::vector<SpawnedActor> GetSpawned() const;
        std::vector<ArchetypeEntry> GetArchetypes() const;
        SpawnResult GetLastResult() const;
        SDK::EntityId GetLastSpawnedId() const;

      private:
        enum class CommandKind : std::uint8_t {
            Spawn,
            Despawn,
            DespawnAll,
            LoadArchetypeLibrary,
        };

        struct Command {
            CommandKind kind = CommandKind::Spawn;
            std::string actorClass;
            std::string soulGuid;
            std::string archetype;
            PlacementParams placement {};
            SDK::EntityId entityId = 0;
        };

        void ExecuteSpawn(const Command &command);

        /**
         * The whole spawn: SpawnEntity, the soul GUID if there is one, then
         * InitEntity, under the engine lock the natives hold across the same
         * sequence. Keeps the entity in hand to read its id directly.
         */
        static SpawnResult SpawnAndInit(SDK::IEntityClass *entityClass, SDK::IEntityArchetype *archetype, const char *name, const WorldPlacement &placement, const std::string &soulGuid, const std::string &soulArchetype, const std::vector<std::pair<std::string, std::variant<bool, std::string>>> &properties, SDK::ChannelId channelId, SDK::EntityId &entityId);

        /** The soul GUID and any requested properties onto the entity's own `Properties`, the soul archetype onto its script table. */
        static bool ApplySpawnProperties(SDK::IEntity *entity, const std::string &soulGuid, const std::string &soulArchetype, const std::vector<std::pair<std::string, std::variant<bool, std::string>>> &properties);
        void ExecuteDespawn(SDK::EntityId entityId);
        void ExecuteDespawnAll();
        void ExecuteLoadArchetypeLibrary(const std::string &name);
        bool ArchetypeCatalogIsStale() const;
        void RefreshArchetypes();
        void PruneDeadHandles();

        void SetResult(SpawnResult result, SDK::EntityId entityId);

        /** Executes one ticket and fires its callback. Game thread only. */
        void ExecuteTicket(SpawnTicket &ticket);

        mutable std::mutex _mutex;
        std::vector<Command> _pending;

        // Game thread only, and deliberately not under `_mutex`: executing a
        // ticket calls back into the replica that asked for it, which reaches
        // straight back here to `Return` it.
        std::vector<std::unique_ptr<SpawnTicket>> _tickets;
        std::vector<SpawnedActor> _spawned;
        std::vector<ArchetypeEntry> _archetypes;
        SoulCatalog _soulCatalog;
        bool _archetypeRefreshRequested = false;
        SpawnResult _lastResult         = SpawnResult::None;
        SDK::EntityId _lastSpawnedId    = 0;
        std::uint32_t _nameCounter      = 0;
    };
} // namespace KCDC::Common::Spawn
