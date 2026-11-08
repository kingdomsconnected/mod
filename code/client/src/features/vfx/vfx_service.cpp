#include "vfx_service.h"
#include "sdk/cryengine/entitysystem/i_entity_render_proxy.h"

#include "vfx.h"

#include "core/application.h"

#include "features/dog/dog.h"
#include "features/horse/horse.h"
#include "features/player/player.h"
#include "features/prop/prop.h"

#include "common/module_support.h"

#include "sdk/cryengine/3dengine/i_particle_effect.h"
#include "sdk/cryengine/3dengine/i_particle_manager.h"
#include "sdk/cryengine/3dengine/spawn_params.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include "shared/game/vfx_catalog.h"

#include <core_modules.h>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Vfx {
    namespace {
        constexpr const char *kLogger = "Vfx";

        /** Appears only in the engine's "Particle effect not found" warning, to say who asked. */
        constexpr const char *kFindEffectSource = "KCDC";

        /** What the game's own Particle.SpawnEffect passes for "next free slot". */
        constexpr int kNextFreeSlot = -1;

        /**
         * The effect a catalog row names, or null.
         *
         * Not cached: an effect pointer belongs to a library, and a level
         * change is free to drop the library it came out of. The lookup itself
         * is a string-keyed tree search in the manager; `loadResources` is the
         * expensive half, because `CParticleManager` walks the whole effect
         * tree from there (0x180808D60 recurses over every child), so a caller
         * that only wants to know whether the name resolves asks for false.
         */
        SDK::IParticleEffect *ResolveEffect(const Shared::Game::VfxEffectRef &effect, bool loadResources) {
            const std::optional<Shared::Game::VfxEffectEntry> entry = effect.Entry();
            SDK::IParticleManager *particles                        = SDK::IParticleManager::GetInstance();
            if (!entry || particles == nullptr) {
                return nullptr;
            }
            // Not asked for before its library is mounted: FindEffect caches a miss as an empty
            // effect until the level's particles are purged, which would hide it for the session.
            if (effect.IsStreamed() && !KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(KCDC::Shared::StreamedAssets::Policy::ParticleLibraryEntryOf(effect.streamed))) {
                return nullptr;
            }

            // The catalog holds string_views over a compiled-in table, which is
            // not terminated at the view's end.
            char name[128];
            if (entry->name.size() >= sizeof(name)) {
                return nullptr;
            }
            std::snprintf(name, sizeof(name), "%.*s", static_cast<int>(entry->name.size()), entry->name.data());
            return particles->FindEffect(name, kFindEffectSource, loadResources);
        }

        /**
         * The spawn parameters one tuning asks for, over the engine's defaults.
         *
         * `CRenderProxy::LoadParticleEmitter` (0x1804218D8) applies these as an
         * overwrite, not a merge, so passing them also replaces the
         * `attachType`/`attachForm` it seeded from the effect. Harmless while no
         * shipped effect declares an attach type; a patch that authored one
         * would be the thing to re-check.
         */
        SDK::SpawnParams ToSpawnParams(const Shared::Game::VfxTuning &tuning) {
            SDK::SpawnParams params = SDK::MakeSpawnParams();
            params.prime            = tuning.prime;
            params.sizeScale        = tuning.scale;
            params.countScale       = tuning.countScale;
            params.speedScale       = tuning.speedScale;
            params.timeScale        = tuning.timeScale;
            params.strength         = tuning.strength;
            params.pulsePeriod      = tuning.pulsePeriod;
            return params;
        }

        /** The engine entity a replicated one is standing as, or 0. Only these four replica types build a body. */
        SDK::EntityId ResolveAttachEntity(std::uint64_t networkId) {
            if (auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(networkId)) {
                return human->ResolveBodyEntityId();
            }
            if (auto *horse = KCDC::Common::FindReplicaById<KCDC::Features::Horse::Horse>(networkId)) {
                return horse->GetEntityId();
            }
            if (auto *dog = KCDC::Common::FindReplicaById<KCDC::Features::Dog::Dog>(networkId)) {
                return dog->GetEntityId();
            }
            if (auto *prop = KCDC::Common::FindReplicaById<KCDC::Features::Prop::Prop>(networkId)) {
                return prop->GetEntityId();
            }
            return 0;
        }

        /**
         * Empties the BasicEntity properties that would give the carrier a body
         * of its own: `BasicEntity.lua` defaults `object_Model` to
         * `objects/default/primitive_pyramid.cgf` and physicalizes it as a
         * rigid body. Must run between SpawnEntity and InitEntity, because
         * `OnSpawn` reads all three once.
         */
        bool ClearCarrierProperties(SDK::IEntity *entity) {
            auto *proxy                    = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
            SDK::IScriptTable *scriptTable = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
            if (scriptTable == nullptr) {
                return false;
            }

            auto properties = scriptTable->GetTable("Properties");
            if (!properties) {
                return false;
            }

            // Enough on its own: SetFromProperties returns on an empty model
            // before it reaches SetupModel. The physics flags follow so the
            // intent survives someone editing that early return.
            properties->SetValue("object_Model", "");

            auto physics = properties->GetTable("Physics");
            if (!physics) {
                return false;
            }

            // Lua treats numeric zero as truthy; these must be booleans.
            physics->SetValue("bPhysicalize", false);
            physics->SetValue("bRigidBody", false);
            return true;
        }

        /** The live entity behind an id, or null once it has gone or been queued for deletion. */
        SDK::IEntity *ResolveEntity(SDK::EntityId entityId) {
            SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity         = entities != nullptr && entityId != 0 ? entities->GetEntity(entityId) : nullptr;
            return entity != nullptr && !entity->IsGarbage() ? entity : nullptr;
        }
    } // namespace

    void VfxService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::VfxBurst>([](const Shared::RPC::VfxBurst &burst, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetVfxService().Receive(burst);
            }
        });
    }

    void VfxService::Receive(const Shared::RPC::VfxBurst &burst) {
        if (!burst.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Dropped a malformed particle burst");
            return;
        }

        // A backlog this deep is a sender problem, and playing it all at once
        // would be worse than playing none of it.
        std::scoped_lock lock(_pendingBurstsMutex);
        if (_pendingBursts.size() < kMaxLiveEmitters) {
            _pendingBursts.push_back(burst);
        }
    }

    void VfxService::NativeContextChanged() {
        ++_nativeContext;
    }

    bool VfxService::IsAvailable(const Shared::Game::VfxEffectRef &effect) {
        return ResolveEffect(effect, false) != nullptr;
    }

    bool VfxService::HasBudget() {
        if (_emitters.size() < kMaxLiveEmitters) {
            return true;
        }
        if (!_warnedAtBudget) {
            _warnedAtBudget = true;
            Framework::Logging::GetLogger(kLogger)->warn("Refusing particle effects: {} are already playing, which is the budget", kMaxLiveEmitters);
        }
        return false;
    }

    int VfxService::LoadInto(SDK::EntityId entityId, const Shared::Game::VfxEffectRef &effectRef, const Shared::Game::VfxTuning &tuning) {
        SDK::IEntity *entity         = ResolveEntity(entityId);
        SDK::IParticleEffect *effect = ResolveEffect(effectRef, true);
        if (entity == nullptr || effect == nullptr) {
            return -1;
        }

        // Prime goes in twice because the render proxy writes the argument over
        // the struct field; the two have to agree.
        const SDK::SpawnParams params = ToSpawnParams(tuning);
        return entity->LoadParticleEmitter(kNextFreeSlot, effect, &params, params.prime, false);
    }

    std::uint32_t VfxService::Remember(SDK::EntityId entityId, int slot, bool carrier, std::uint32_t durationMs) {
        Emitter emitter;

        // Zero is the "no emitter" answer, so it is skipped rather than issued.
        emitter.handle = _nextHandle++;
        if (emitter.handle == kNoHandle) {
            emitter.handle = _nextHandle++;
        }

        emitter.entityId  = entityId;
        emitter.slot      = slot;
        emitter.carrier   = carrier;
        emitter.expiresAt = durationMs != 0 ? MafiaNet::GetTime() + durationMs : 0;
        _emitters.push_back(emitter);
        return emitter.handle;
    }

    void VfxService::Release(const Emitter &emitter, bool immediate) {
        // Releasing a slot only deactivates its emitter, which lets every
        // particle already out live its full lifetime.
        if (immediate) {
            SDK::IEntity *entity = ResolveEntity(emitter.entityId);
            if (SDK::IParticleEmitter *particles = entity != nullptr && emitter.slot >= 0 ? entity->GetParticleEmitter(emitter.slot) : nullptr) {
                particles->Kill();
            }
        }

        if (emitter.carrier) {
            if (SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance()) {
                entities->RemoveEntity(emitter.entityId, false);
            }
            return;
        }

        // Somebody else's entity, so only the slot goes -- and only while it
        // still holds an emitter, since a finite effect releases its own and
        // the engine is then free to give it away.
        SDK::IEntity *entity = ResolveEntity(emitter.entityId);
        if (entity != nullptr && emitter.slot >= 0 && entity->GetParticleEmitter(emitter.slot) != nullptr) {
            entity->FreeSlot(emitter.slot);
        }
    }

    std::uint32_t VfxService::Spawn(const Shared::Game::VfxEffectRef &effect, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs) {
        ObserveNativeContext();
        const std::optional<Shared::Game::VfxEffectEntry> entry = effect.Entry();
        SDK::IEntitySystem *entities                            = SDK::IEntitySystem::GetInstance();
        if (!entry || entities == nullptr || !tuning.IsSane() || !HasBudget()) {
            return kNoHandle;
        }

        SDK::IEntityClassRegistry *registry = entities->GetClassRegistry();
        SDK::IEntityClass *entityClass      = registry != nullptr ? registry->FindClass(SDK::EntityClassNames::BasicEntity) : nullptr;
        if (entityClass == nullptr) {
            return kNoHandle;
        }

        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Vfx_%u", _nextHandle);

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.name        = name;
        params.position    = KCDC::Common::ToNative(position);
        params.rotation    = KCDC::Common::ToNative(glm::normalize(rotation));

        // Uninitialised, so the properties can be emptied before OnSpawn reads them.
        SDK::IEntity *entity = entities->SpawnEntity(params, false);
        if (entity == nullptr) {
            return kNoHandle;
        }

        const SDK::EntityId entityId = entity->GetId();
        if (!ClearCarrierProperties(entity)) {
            entities->RemoveEntity(entityId, false);
            Framework::Logging::GetLogger(kLogger)->warn("The carrier for \"{}\" has no Properties table to empty", entry->name);
            return kNoHandle;
        }

        // InitEntity queues the entity for deletion on failure.
        if (!entities->InitEntity(entity, params) || ResolveEntity(entityId) == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The carrier for \"{}\" would not initialise", entry->name);
            return kNoHandle;
        }

        const int slot = LoadInto(entityId, effect, tuning);
        if (slot < 0) {
            entities->RemoveEntity(entityId, false);
            Framework::Logging::GetLogger(kLogger)->warn("The game would not load particle effect \"{}\"", entry->name);
            return kNoHandle;
        }

        return Remember(entityId, slot, true, durationMs);
    }

    std::uint32_t VfxService::Attach(const Shared::Game::VfxEffectRef &effect, SDK::EntityId entityId, const glm::vec3 &offset, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs) {
        ObserveNativeContext();
        const std::optional<Shared::Game::VfxEffectEntry> entry = effect.Entry();
        if (!entry || !tuning.IsSane() || ResolveEntity(entityId) == nullptr || !HasBudget()) {
            return kNoHandle;
        }

        const int slot = LoadInto(entityId, effect, tuning);
        if (slot < 0) {
            Framework::Logging::GetLogger(kLogger)->warn("The game would not load particle effect \"{}\" on entity {}", entry->name, entityId);
            return kNoHandle;
        }

        // Only when it is actually offset: a slot's transform starts at the
        // identity, and stating it again costs the engine a re-registration.
        if (SDK::IEntity *entity = ResolveEntity(entityId); entity != nullptr && (offset != glm::vec3(0.0F) || rotation != glm::identity<glm::quat>())) {
            entity->SetSlotLocalTM(slot, SDK::Matrix34FromTransform(KCDC::Common::ToNative(offset), KCDC::Common::ToNative(glm::normalize(rotation)), SDK::Vec3 {1.0F, 1.0F, 1.0F}));
        }

        return Remember(entityId, slot, false, durationMs);
    }

    VfxService::Emitter *VfxService::Find(std::uint32_t handle) {
        ObserveNativeContext();
        return const_cast<Emitter *>(static_cast<const VfxService *>(this)->Find(handle));
    }

    const VfxService::Emitter *VfxService::Find(std::uint32_t handle) const {
        if (handle == kNoHandle) {
            return nullptr;
        }
        for (const Emitter &emitter : _emitters) {
            if (emitter.handle == handle) {
                return &emitter;
            }
        }
        return nullptr;
    }

    std::array<const SDK::IRenderNode *, 2> VfxService::RenderNodes(std::uint32_t handle) const {
        std::array<const SDK::IRenderNode *, 2> nodes {};
        const auto *emitter = Find(handle);
        if (!emitter) {
            return nodes;
        }
        auto *entity = ResolveEntity(emitter->entityId);
        if (!entity) {
            return nodes;
        }
        auto *proxy = static_cast<SDK::IEntityRenderProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)));
        if (proxy) {
            nodes[0] = proxy->GetRenderNode();
        }
        if (emitter->slot >= 0) {
            nodes[1] = entity->GetParticleEmitter(emitter->slot);
        }
        return nodes;
    }

    bool VfxService::Stop(std::uint32_t handle, bool immediate) {
        ObserveNativeContext();
        if (handle == kNoHandle) {
            return false;
        }

        for (auto it = _emitters.begin(); it != _emitters.end(); ++it) {
            if (it->handle != handle) {
                continue;
            }
            Release(*it, immediate);
            _emitters.erase(it);
            return true;
        }
        return false;
    }

    void VfxService::QueueStop(std::uint32_t handle) {
        if (handle == kNoHandle) {
            return;
        }

        std::scoped_lock lock(_pendingStopsMutex);
        _pendingStops.push_back(handle);
    }

    bool VfxService::Move(std::uint32_t handle, const glm::vec3 &position, const glm::quat &rotation) {
        const Emitter *emitter = Find(handle);
        if (emitter == nullptr || !emitter->carrier) {
            return false;
        }

        SDK::IEntity *entity = ResolveEntity(emitter->entityId);
        if (entity == nullptr) {
            return false;
        }

        KCDC::Common::PlaceEntity(entity, position, rotation);
        return true;
    }

    void VfxService::StopAll() {
        ObserveNativeContext();
        std::vector<Emitter> owned;
        owned.swap(_emitters);
        for (const Emitter &emitter : owned) {
            Release(emitter);
        }
    }

    bool VfxService::IsAlive(std::uint32_t handle) const {
        return Find(handle) != nullptr;
    }

    void VfxService::Reset() {
        {
            std::scoped_lock lock(_pendingBurstsMutex);
            _pendingBursts.clear();
        }
        {
            std::scoped_lock lock(_pendingStopsMutex);
            _pendingStops.clear();
        }
        StopAll();
        _warnedAtBudget = false;
    }

    void VfxService::DrainStops() {
        std::vector<std::uint32_t> handles;
        {
            std::scoped_lock lock(_pendingStopsMutex);
            handles.swap(_pendingStops);
        }

        for (const std::uint32_t handle : handles) {
            Stop(handle);
        }
    }

    void VfxService::ObserveNativeContext() {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context) {
            return;
        }
        _observedContext = context;

        // Dropped rather than released: the world these ids named is gone, and
        // the ids themselves have already been handed out again.
        _emitters.clear();
        {
            std::scoped_lock lock(_pendingBurstsMutex);
            _pendingBursts.clear();
        }
        {
            std::scoped_lock lock(_pendingStopsMutex);
            _pendingStops.clear();
        }

        // The replicas are still here and still want their effects.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::Vfx::Vfx>([](KCDC::Features::Vfx::Vfx *vfx) {
                vfx->Unbind();
            });
        }
    }

    void VfxService::DrainBursts() {
        std::vector<Shared::RPC::VfxBurst> bursts;
        {
            std::scoped_lock lock(_pendingBurstsMutex);
            bursts.swap(_pendingBursts);
        }

        for (const Shared::RPC::VfxBurst &burst : bursts) {
            if (burst.attachNetworkId == 0) {
                Spawn(burst.effect, burst.position, burst.rotation, burst.tuning, burst.durationMs);
                continue;
            }

            // A static prop stands as a brush, which has nothing to attach to
            // and never moves under the effect: it plays where it would hang.
            if (auto *prop = KCDC::Common::FindReplicaById<KCDC::Features::Prop::Prop>(burst.attachNetworkId); prop != nullptr && prop->IsBrush()) {
                const glm::quat facing = glm::normalize(prop->rotation);
                Spawn(burst.effect, prop->position + facing * burst.position, glm::normalize(facing * burst.rotation), burst.tuning, burst.durationMs);
                continue;
            }

            // A subject this client has not streamed in is too far away to have
            // been seen; playing it at the origin would be worse than silence.
            const SDK::EntityId entityId = ResolveAttachEntity(burst.attachNetworkId);
            if (entityId != 0) {
                Attach(burst.effect, entityId, burst.position, burst.rotation, burst.tuning, burst.durationMs);
            }
        }
    }

    void VfxService::ReapExpired() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        for (auto it = _emitters.begin(); it != _emitters.end();) {
            const bool expired = it->expiresAt != 0 && now >= it->expiresAt;

            // An emitter on somebody else's entity goes with that entity.
            if (expired || ResolveEntity(it->entityId) == nullptr) {
                Release(*it);
                it = _emitters.erase(it);
                continue;
            }
            ++it;
        }
    }

    void VfxService::Update() {
        ObserveNativeContext();
        DrainStops();
        DrainBursts();
        ReapExpired();

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        replication->ForEach<KCDC::Features::Vfx::Vfx>([](KCDC::Features::Vfx::Vfx *vfx) {
            vfx->Frame();
        });
    }
} // namespace KCDC::Features::Vfx
