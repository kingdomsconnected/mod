#pragma once

#include "shared/features/smart_object/smart_object_entity.h"

#include "common/spawn/static_brush.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"

#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace KCDC::Features::SmartObject {
    /**
     * One smart object standing in the world: an entity per blueprint part,
     * spawned under `guidBase | part`, with its prefab properties and its
     * links to the others, and a static brush per piece of furniture.
     *
     * The parts are what the game's own interaction runs on -- the seat a
     * body sits on and the trigger that offers it, the station and its
     * fixtures -- so once they stand, the game treats the object as one the
     * level placed. A replicated smart object stands as one, and so does the
     * World Builder's preview of one. Game thread only, except `Release`.
     */
    class SmartObjectAssembly final {
      public:
        enum class BuildResult {
            Built,
            /** The world could not take it this time; try again. */
            Retry,
            /** It never will: a class the game does not register. */
            Refused,
        };

        BuildResult Build(const Shared::Game::SmartObjectBlueprint &blueprint, std::uint64_t guidBase, const glm::vec3 &position, const glm::quat &rotation);

        /** Poses every part and brush again for a new pose of the whole. */
        void Move(const glm::vec3 &position, const glm::quat &rotation);

        /** Removes every part and brush. */
        void Destroy();

        /** Forgets everything without touching it, for a world that is gone. */
        void Forget();

        /** Hands the natives over for removal on the game thread, and forgets them. */
        void Release(std::vector<SDK::EntityId> &parts, std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> &brushes);

        bool IsBuilt() const {
            return _blueprint != nullptr;
        }

        const std::vector<SDK::EntityId> &Parts() const {
            return _parts;
        }

        /** Whether `entityId` is one of its parts. */
        bool OwnsEntity(SDK::EntityId entityId) const;

        /** The box around every part and brush; false when nothing stands. */
        bool Bounds(SDK::AABB &bounds) const;

      private:
        void ForgetFixtures();

        const Shared::Game::SmartObjectBlueprint *_blueprint = nullptr;
        std::uint64_t _guidBase                              = 0;
        std::vector<SDK::EntityId> _parts;
        std::vector<KCDC::Common::Spawn::StaticBrush> _brushes;
    };

    /**
     * Client binding between a replicated smart object and the assembly it
     * stands as, under the GUIDs every client mints from its serial.
     * `SmartObjectModule` owns the tick and the level change.
     */
    class SmartObject final: public Shared::Entities::SmartObjectEntity {
      public:
        /**
         * Hands the assembly to the module for the tick to remove: a replica
         * can go from the loading-time peer pump, mid-way through the entity
         * system's own work.
         */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** Builds the object once its level is up; retries a refused build a few times. Game thread. */
        void Frame();

        /** Forgets the native side without touching it, for a world that is gone. */
        void Unbind();

        bool IsBuilt() const {
            return _assembly.IsBuilt();
        }

      private:
        SmartObjectAssembly _assembly;
        bool _refused           = false;
        std::uint32_t _tries    = 0;
        MafiaNet::Time _nextTry = 0;
    };
} // namespace KCDC::Features::SmartObject
