#pragma once

#include <glm/glm.hpp>

namespace KCDC::Features::Npc {
    /**
     * Detects a body that makes no headway while ordered to move.
     *
     * Headway is net displacement from where the body stood when the window
     * opened, not any movement at all: a body rocking against a wall moves
     * every frame and gets nowhere, and must still read as blocked.
     */
    class MovementProgress final {
      public:
        void Reset() {
            _known        = false;
            _stallSeconds = 0.0F;
        }

        /** Returns true once the body has not got `kProgressDistance` from where it stood for `kStallSeconds`. */
        bool Update(const glm::vec3 &position, float dt) {
            const glm::vec2 planar(position);
            if (!_known || glm::distance(planar, _anchor) >= kProgressDistance) {
                _anchor       = planar;
                _known        = true;
                _stallSeconds = 0.0F;
            }
            else {
                _stallSeconds += dt;
            }
            return _stallSeconds >= kStallSeconds;
        }

        /** How long a body may make no headway while ordered to move before it is Blocked. */
        static constexpr float kStallSeconds = 3.0F;

        /** Ground covered from the window's anchor that counts as headway, in metres. */
        static constexpr float kProgressDistance = 0.5F;

      private:
        glm::vec2 _anchor {0.0F};
        float _stallSeconds = 0.0F;
        bool _known         = false;
    };
} // namespace KCDC::Features::Npc
