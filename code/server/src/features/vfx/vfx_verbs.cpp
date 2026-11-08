#include "vfx_verbs.h"

#include "features/vfx/vfx_scripting.h"

#include "shared/entity_filter.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/vfx/vfx_rpc.h"
#include "shared/pose_math.h"

#include <algorithm>
#include <cmath>
#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Vfx {
    namespace {
        constexpr const char *kLogger = "Vfx";

        /** Sends one burst to every player who could see it happen. */
        std::size_t Relay(Shared::RPC::VfxBurst &burst, const glm::vec3 &anchor, MafiaNet::VirtualWorldId virtualWorld, float radius) {
            auto *replication = Framework::CoreModules::GetReplication();
            auto *net         = Framework::CoreModules::GetNetworkPeer();
            if (replication == nullptr || net == nullptr) {
                return 0;
            }

            const float radiusSquared = radius * radius;
            std::size_t recipients    = 0;
            replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *viewer) {
                if (viewer->GetVirtualWorld() != virtualWorld || !viewer->transformReady) {
                    return;
                }

                const glm::vec3 separation = viewer->position - anchor;
                if (glm::dot(separation, separation) > radiusSquared) {
                    return;
                }

                // Reliable but unordered: it has to arrive, but it stands for
                // nothing else, so nothing need hold it back.
                net->SendRPC(burst, MafiaNet::ToGuid(viewer->ownerGUID), MafiaNet::Priority::Medium, MafiaNet::Reliability::Reliable);
                ++recipients;
            });
            return recipients;
        }
    } // namespace

    float Effects::BurstRadiusFor(const Shared::Game::VfxEffectEntry &effect) {
        return std::clamp(effect.viewDistance, kMinBurstRadius, kMaxBurstRadius);
    }

    std::vector<Shared::Entities::VfxEntity *> Effects::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::VfxEntity *> effects;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::VfxEntity>([&](Shared::Entities::VfxEntity *effect) {
                if (Shared::Entities::InWorld(effect, virtualWorld)) {
                    effects.push_back(effect);
                }
            });
        }
        return effects;
    }

    Shared::Entities::VfxEntity *Effects::Spawn(const Shared::Game::VfxEffectRef &effectRef, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t virtualWorld) {
        const std::optional<Shared::Game::VfxEffectEntry> entry = effectRef.Entry();
        if (!entry || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation) || !tuning.IsSane()) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *effect = replication->CreateEntity<Shared::Entities::VfxEntity>();
        if (effect == nullptr) {
            return nullptr;
        }

        // All of it before the entity replicates, so it rides the construction
        // snapshot rather than arriving a tick after the emitter was built from
        // the defaults -- which for an effect is a visible pop.
        effect->effect          = effectRef;
        effect->tuning          = tuning;
        effect->position        = position;
        effect->rotation        = glm::normalize(rotation);
        effect->streaming.range = Shared::Entities::VfxEntity::StreamRangeFor(*entry);
        effect->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Spawned effect {} (\"{}\") in world {} at {:.2f}, {:.2f}, {:.2f}", effect->GetNetworkID(), entry->name, virtualWorld, position.x, position.y, position.z);

        const auto networkId = effect->GetNetworkID();
        Scripting::Events::VfxSpawned(networkId);
        // A spawn handler may immediately destroy the entity.
        return replication->GetEntity<Shared::Entities::VfxEntity>(networkId);
    }

    void Effects::SetTuning(Shared::Entities::VfxEntity *effect, const Shared::Game::VfxTuning &tuning) {
        if (effect == nullptr || !tuning.IsSane() || effect->tuning == tuning) {
            return;
        }
        effect->tuning = tuning;
    }

    void Effects::Destroy(Shared::Entities::VfxEntity *effect) {
        if (effect == nullptr) {
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can read it.
        Scripting::Events::VfxDestroyed(effect->GetNetworkID());
        replication->DestroyEntity(effect);
    }

    std::size_t Effects::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::VfxEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::VfxEntity *effect : doomed) {
            Destroy(effect);
        }
        return doomed.size();
    }

    std::size_t Effects::Burst(const Shared::Game::VfxEffectRef &effectRef, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs, MafiaNet::VirtualWorldId virtualWorld) {
        const std::optional<Shared::Game::VfxEffectEntry> entry = effectRef.Entry();
        if (!entry) {
            return 0;
        }

        Shared::RPC::VfxBurst burst;
        burst.effect      = effectRef;
        burst.position    = position;
        burst.rotation    = Shared::Game::IsUsableRotation(rotation) ? glm::normalize(rotation) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        burst.tuning      = tuning;
        burst.durationMs  = durationMs;
        if (!burst.IsSane()) {
            return 0;
        }

        return Relay(burst, position, virtualWorld, BurstRadiusFor(*entry));
    }

    std::size_t Effects::BurstOnEntity(const Shared::Game::VfxEffectRef &effectRef, Framework::Networking::Replication::NetworkEntity *subject, const glm::vec3 &offset, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs) {
        const std::optional<Shared::Game::VfxEffectEntry> entry = effectRef.Entry();
        if (!entry || subject == nullptr) {
            return 0;
        }

        Shared::RPC::VfxBurst burst;
        burst.effect          = effectRef;
        burst.attachNetworkId = subject->GetNetworkID();
        burst.position        = offset;
        burst.rotation        = Shared::Game::IsUsableRotation(rotation) ? glm::normalize(rotation) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        burst.tuning          = tuning;
        burst.durationMs      = durationMs;
        if (!burst.IsSane()) {
            return 0;
        }

        return Relay(burst, subject->position, subject->GetVirtualWorld(), BurstRadiusFor(*entry));
    }
} // namespace KCDC::Features::Vfx
