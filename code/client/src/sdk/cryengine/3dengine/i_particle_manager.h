#pragma once

#include "i_particle_effect.h"

namespace SDK {
    /**
     * The 3D engine's particle library: every effect the game has loaded, and
     * the lookup that loads the rest on demand.
     *
     * `CParticleManager` carries two vtables. The one this interface describes
     * is at 0x18402AE90, its second base at object offset +8, and that is the
     * pointer `gEnv` holds -- so the value read out of `gEnv+0x68` is already
     * adjusted and calls straight through. `CSystem::SetIParticleManager`
     * (0x181A717A0) writes exactly that field, which is what fixes the offset.
     *
     * Slot order matches CryEngine 5.x, but only `FindEffect` has been proven
     * from a call site, so only `FindEffect` is named.
     */
    class IParticleManager {
      public:
        virtual ~IParticleManager() = default; // +0x000
        virtual void Opaque01()     = 0;
        virtual void Opaque02()     = 0;
        virtual void Opaque03()     = 0;
        virtual void Opaque04()     = 0;
        virtual void Opaque05()     = 0;

        /**
         * The effect `name` stands for, or null when nothing declares it.
         *
         * `name` is split at its **first** dot: the prefix is a library, loaded
         * from `Libs/Particles/<library>.xml` if it is not already in, and the
         * remainder names an effect inside it. So `WH_Particels.fires.candle`
         * is library `WH_Particels` and effect `fires.candle`, not a
         * three-level path.
         *
         * `source` appears only in the engine's "Particle effect not found"
         * warning, to say who asked. `loadResources` blocks on the effect's
         * textures and meshes rather than handing back something that draws
         * nothing on its first frame.
         *
         * +0x030, proven from CScriptBind_Particle::SpawnEffect 0x1839AC81C
         * and CScriptBind_Entity::LoadParticleEffect 0x180F2D600.
         */
        virtual IParticleEffect *FindEffect(const char *name, const char *source = "", bool loadResources = true) = 0;

        /** gEnv's own field; null until the 3D engine has been initialised. */
        static IParticleManager *GetInstance();
    };
} // namespace SDK
