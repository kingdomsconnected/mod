#pragma once

#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"
#include "sdk/wh/engine3d/c_physicalization_manager.h"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::World {
    /**
     * The level's precomputed brush colliders, as the mod can address them.
     *
     * While `wh_e_PernamentPhysicalization` is on -- the shipped default -- a
     * level brush owns no physics. `C_PhysicalizationManager` builds one
     * anonymous `PE_STATIC` per record of the layer's `physics/layer<id>.dat`
     * and keeps nothing that names the brush it stands for, so hiding or
     * moving the brush leaves its collision where the level put it.
     *
     * This pairs every collider with the record it was built from as it is
     * built -- the mesh the record names and where it stands, which is the
     * key the map editor already finds a brush by -- and forgets a layer's the
     * moment the manager destroys them. A caller can then take one brush's
     * collider out of the world and put it back.
     *
     * Both registers are keyed by mesh, so a hold or a freshly built record
     * looks at the few placements of one mesh rather than the whole level.
     * Everything is guarded by the manager's own recursive lock, which its
     * builder and its destroyer already hold around every change to its list:
     * the builder runs on whichever thread completes the layer file's read,
     * the rest on the game thread.
     */
    class LevelColliderModule final {
      public:
        static LevelColliderModule &Get();

        /** Installs the build and teardown detours. Called once, at boot. */
        static void Install();

        /**
         * Pairs the colliders one native build just appended with the records
         * it was given, and takes out the ones a hold covers. The builder
         * makes one collider per record whose mesh the level has loaded, in
         * record order, and skips the rest; the same test here lines them
         * up. Under the manager lock.
         */
        void NoteLayerBuilt(int layerId, std::span<const SDK::wh::engine3d::S_LayerPhysicsRecord> records, std::span<SDK::IPhysicalEntity *const> built);

        /** The manager is about to destroy one layer's colliders; forget them first. Under the manager lock. */
        void ForgetLayer(int layerId);

        /**
         * Takes the collider of the brush of `statObj` the level put at
         * `position` out of the world, for as long as a hold is kept on it.
         * Holds count: two callers holding the same brush keep it out until
         * both let go. Covers a collider built later too, when the layer
         * streams back in. False when there is no layer physics to hold
         * anything in. Game thread.
         */
        bool Hold(SDK::IStatObj *statObj, const SDK::Vec3 &position);

        /**
         * Lets go of one hold. The collider comes back when the last one
         * goes, and only then is this true -- the caller hands the brush its
         * collision back on true, and not before. Game thread.
         */
        bool Release(SDK::IStatObj *statObj, const SDK::Vec3 &position);

        struct Placement {
            const SDK::IStatObj *mesh = nullptr;
            SDK::Vec3 position {};
        };

        /**
         * The mesh and placement a layer collider was built from. Copied
         * under the manager lock; absent for colliders this did not pair.
         * The builder gives its colliders no foreign data, so a ray cannot
         * otherwise identify the brush it hit. Game thread.
         */
        std::optional<Placement> FindPlacement(const SDK::IPhysicalEntity *collider);

      private:
        /** One collider the manager built from a record of this mesh. */
        struct Collider {
            int layerId = 0;
            SDK::Vec3 position {};
            SDK::IPhysicalEntity *entity = nullptr;
        };

        /** One placement of this mesh kept out of the world, and by how many callers. */
        struct Held {
            SDK::Vec3 position {};
            std::uint32_t count = 0;
        };

        /** The hold on the placement of `statObj` at `position`, or null. */
        Held *FindHeld(const SDK::IStatObj *statObj, const SDK::Vec3 &position);

        /** Suspends or restores every live collider of one placement. Under the manager lock. */
        void SetSuspended(const SDK::IStatObj *statObj, const SDK::Vec3 &position, bool suspended);

        std::unordered_map<const SDK::IStatObj *, std::vector<Collider>> _colliders;
        std::unordered_map<const SDK::IStatObj *, std::vector<Held>> _held;
        /** The same pairing read the other way, for a ray that wants its brush. */
        std::unordered_map<const SDK::IPhysicalEntity *, Placement> _placements;
    };
} // namespace KCDC::Features::World
