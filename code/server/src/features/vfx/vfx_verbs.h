#pragma once

#include "shared/features/vfx/vfx_entity.h"
#include "shared/features/vfx/vfx_tuning.h"
#include "shared/game/vfx_catalog.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <mafianet/types.h>
#include <networking/replication/network_entity.h>
#include <optional>
#include <vector>

namespace KCDC::Features::Vfx {
    /**
     * What the server can do with particle effects, in one place, so a script
     * and a command seed one the same way and raise the same events.
     *
     * Two shapes, and the difference between them is the whole design.
     * `Spawn` creates a replicated entity the interest grid streams like a
     * prop, so it is there for whoever walks up however long afterwards: a
     * campfire, a torch, a plume of smoke. `Burst` sends one message to the
     * players who can see the point it happens at and keeps nothing: a blood
     * spray, a spark, a puff of dust, where a player who was not there missed
     * nothing because there is nothing left to miss.
     */
    class Effects {
      public:
        /**
         * How far a burst is sent: the effect's own `CameraMaxDistance`, past
         * which a receiver would be told about something it would not draw.
         * The floor covers the effects authored for a few metres, still worth
         * sending to whoever is standing on them; the ceiling is a viewer's own
         * stream range, past which the player it happened to is not replicated.
         */
        static constexpr float kMinBurstRadius = 40.0F;
        static constexpr float kMaxBurstRadius = 250.0F;

        static float BurstRadiusFor(const Shared::Game::VfxEffectEntry &effect);

        /** Every replicated effect, optionally narrowed to one virtual world. */
        static std::vector<Shared::Entities::VfxEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** Creates a replicated effect, or null for invalid input or when a vfxSpawn handler destroys it. */
        static Shared::Entities::VfxEntity *Spawn(const Shared::Game::VfxEffectRef &effectRef, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t virtualWorld);

        /** Retunes a live effect, which rebuilds its emitter on every client that can see it. */
        static void SetTuning(Shared::Entities::VfxEntity *effect, const Shared::Game::VfxTuning &tuning);

        static void Destroy(Shared::Entities::VfxEntity *effect);
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** Plays an effect once for the players near a world point. Returns how many clients were told. */
        static std::size_t Burst(const Shared::Game::VfxEffectRef &effectRef, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs, MafiaNet::VirtualWorldId virtualWorld);

        /**
         * The same, hung on a replicated entity so it follows what it was
         * attached to. `offset` is in that entity's space, and the recipients
         * are chosen against where the subject is and its own virtual world.
         */
        static std::size_t BurstOnEntity(const Shared::Game::VfxEffectRef &effectRef, Framework::Networking::Replication::NetworkEntity *subject, const glm::vec3 &offset, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs);
    };
} // namespace KCDC::Features::Vfx
