#pragma once

#include "features/navigation/navmesh.h"
#include "shared/features/npc/npc_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Features::Npc {
    /** Who plans a move; independent of the client simulating the body. */
    enum class Pathfinding : std::uint8_t {
        /** Server routes whenever a mesh is loaded; without one, native steering or a dormant straight line. */
        Auto,
        /** Native steering straight at the goal, with a server route only once the game reports `blocked`. */
        Game,
        /** Server routes always; refused without a loaded mesh. */
        Server,
    };

    /** A move's original goal and the server-planned corners, if it uses them. */
    class NpcNavigation final {
      public:
        void Start(const Shared::Game::NpcIntent &goal, Pathfinding mode);
        void Invalidate();

        /**
         * Whether the server plans this move's corners.
         *
         * A move target is plain steering in this engine: the movement
         * controller turns it into a desired velocity and nothing more, so a
         * body steered straight at a goal walks into whatever is in the way.
         * Auto therefore routes a move on the server whenever there is a mesh
         * to route it on. Follow and flee chase a point that moves, so they
         * steer natively until the game reports `blocked`.
         */
        bool UsesServer(bool hasSimulator, bool meshReady) const;

        /** Plans corners; without a mesh, one straight leg to the destination. */
        void Plan(const Navigation::Navmesh &mesh, const Navigation::QueryOptions &options, const glm::vec3 &position);

        /** Switches a blocked game move to a server route to `destination`, to be planned. False when that cannot help. */
        bool StartFallback(const glm::vec3 &destination, bool meshReady);

        Shared::Game::NpcIntent SimulatorIntent(const glm::vec3 &position) const;
        void Advance(glm::vec3 &position, float distance);
        void AdvanceCorner();

        /**
         * How far up or down a query looks for the mesh around this move's
         * points. A flee destination is projected from the body's own height
         * and can land on ground that rises or falls by as much as the
         * distance run, so it searches that far.
         */
        float SearchHeight() const;

        Pathfinding Mode() const {
            return _mode;
        }

        const Shared::Game::NpcIntent &Goal() const {
            return _goal;
        }

        bool IsPlanned() const {
            return _planned;
        }

        const std::vector<glm::vec3> &Path() const {
            return _path;
        }

        const std::vector<Navigation::Navmesh::PathDoor> &Doors() const {
            return _doors;
        }

        /** A doorway still on the remaining path, rather than one already behind the body. */
        bool DoorAhead(const glm::vec3 &position, const glm::vec3 &door) const;

        bool HasCorner() const {
            return _index < _path.size();
        }

        /** Whether the corner being walked to is a bend rather than the end of the path. */
        bool HasIntermediateCorner() const {
            return _index + 1 < _path.size();
        }

        std::size_t CornerIndex() const {
            return _index;
        }

        const glm::vec3 &Corner() const {
            return _path.at(_index);
        }

        /** The corner after the one being walked to; only while `HasIntermediateCorner`. */
        const glm::vec3 &NextCorner() const {
            return _path.at(_index + 1);
        }

        /** Whether the planned route reaches its destination. */
        bool IsComplete() const {
            return _complete;
        }

        float CornerRadius() const;
        Shared::Game::NpcIntentStatus Status() const;

        /** The default vertical search, and the most a flee may widen it to. */
        static constexpr float kSearchHeight    = 4.0F;

        /** How close a body comes to a bend before the next corner is its target: walking, and faster than that. */
        static constexpr float kBendRadius     = 0.25F;
        static constexpr float kFastBendRadius = 0.4F;
        static constexpr float kMaxSearchHeight = 64.0F;

      private:
        Pathfinding _mode = Pathfinding::Auto;
        Shared::Game::NpcIntent _goal;
        glm::vec3 _destination {0.0F};
        std::vector<glm::vec3> _path;
        std::vector<Navigation::Navmesh::PathDoor> _doors;
        std::size_t _index = 0;
        bool _planned      = false;
        bool _complete     = false;
        bool _onMesh       = false;
    };
} // namespace KCDC::Features::Npc
