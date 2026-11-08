#pragma once

#include "../../types.h"
#include "i_particle_emitter.h"
#include "spawn_params.h"

namespace SDK {
    /**
     * One particle effect out of a library: a tree of emitter descriptions,
     * shared by every instance playing it.
     *
     * Obtained from `IParticleManager::FindEffect` and never constructed here.
     * Reference-counted in the engine, but an effect its library owns outlives
     * every emitter spawned from it and `CScriptBind_Particle::SpawnEffect`
     * takes no reference either, so neither does this.
     *
     * CParticleEffect's table is at 0x183A562C8 and holds 28 entries; only
     * `Spawn` is proven from a call site, so only `Spawn` is named.
     */
    class IParticleEffect {
      public:
        virtual ~IParticleEffect() = default; // +0x000
        virtual void Opaque01()    = 0;
        virtual void Opaque02()    = 0;
        virtual void Opaque03()    = 0;
        virtual void Opaque04()    = 0;
        virtual void Opaque05()    = 0;

        /**
         * Starts a free-standing emitter at `location`, owned by nothing.
         *
         * `location` is CryEngine's `ParticleLoc`, which is a `QuatTS` and
         * nothing more: rotation, world position in metres, uniform scale. The
         * game builds one through a helper at 0x180B0134C that turns a
         * direction into the quaternion; a caller that already has a rotation
         * fills the three fields itself.
         *
         * `params` may be null, as the engine's own Lua bind passes. The
         * returned emitter is not retained, so a continuous effect spawned this
         * way runs until the level unloads -- which is why the mod puts those
         * in an entity slot it can free instead.
         *
         * +0x030, proven from CScriptBind_Particle::SpawnEffect 0x1839AC81C.
         */
        virtual IParticleEmitter *Spawn(const QuatTS &location, const SpawnParams *params = nullptr) = 0;
    };
} // namespace SDK
