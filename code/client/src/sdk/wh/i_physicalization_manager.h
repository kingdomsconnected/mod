#pragma once

namespace SDK::wh {
    /**
     * The 3D engine's layer physics: the static colliders a level ships
     * precomputed per entity layer in `physics/layer<id>.dat`, built and torn
     * down as the layer streams.
     *
     * Primary vtable of `C_PhysicalizationManager` (0x183A6D6D8), exactly two
     * slots and no destructor. The 3D engine's system event listener
     * (0x1809B5298) drives slot 0 with the layer id `IEntitySystem::GetLayerId`
     * gives for the name events 4101 (a layer's objects streamed in, sent by
     * 0x1809B6F90) and 4102 (streamed out, 0x180A57C7C) carry; level load
     * calls it once for layer 0.
     */
    class I_PhysicalizationManager {
      public:
        /**
         * Builds a layer's colliders, or destroys them. Only while the
         * `wh_e_PernamentPhysicalization` CVar is on; with it off the brushes
         * physicalize themselves. 0x1809B60FC.
         */
        virtual void ActivateLayer(int layerId, bool activate) = 0; // +0x00

        /** Destroys every layer's colliders and drops pending reads. 0x180A603E8. */
        virtual void DeactivateAllLayers() = 0; // +0x08
    };

    static_assert(sizeof(I_PhysicalizationManager) == 0x8, "I_PhysicalizationManager size mismatch");
} // namespace SDK::wh
