#pragma once

#include "i_render_node.h"

namespace SDK {
    /**
     * A running particle emitter: one playing instance of an `IParticleEffect`.
     *
     * `CParticleEmitter`, vtable 0x183A55F48 (interface 0x183A37F40). Its own
     * slots start after the 73 of `IRenderNode`, in stock CE5 order:
     * GetVersion, IsAlive, Activate, Kill, Restart, SetEffect, GetEffect,
     * SetEmitGeom, SetSpawnParams (+0x288). Only what is proven is named.
     */
    class IParticleEmitter: public IRenderNode {
      public:
        virtual void unk_fn0073() = 0; // GetVersion, returns 1
        virtual void unk_fn0074() = 0; // IsAlive

        /**
         * false stops emission only: emitted particles live out their whole
         * lifetime, which is an hour for `WH_Particels.other.fake_army_*`.
         * This is what the engine does when a slot holding an emitter is
         * released.
         */
        virtual void Activate(bool active) = 0; // +0x258, 0x180422664

        /** Removes the emitter's particles at once. */
        virtual void Kill() = 0; // +0x260, 0x180AC09F0
    };

    static_assert(sizeof(IParticleEmitter) == sizeof(IRenderNode), "IParticleEmitter adds no data");
} // namespace SDK
