#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace KCDC::Shared::Game {
    /**
     * A patch of the level's merged terrain vegetation taken out of the
     * world: the tall grass, flowers and ferns the level bakes into its
     * terrain detail rather than placing as objects, which no object edit
     * can name. Every such plant rooted inside the box goes; put the box
     * under a house and its floor stays clear.
     *
     * An upright box: a centre, half sizes along its own two ground axes and
     * up, and a turn about the vertical. That is a building's footprint, and
     * nothing is lost by leaving out a tilt -- the plants stand on the
     * ground, and the box only has to be tall enough to hold their roots.
     *
     * Plants the player can gather are left standing: the game's own
     * respawn keeps count of those, and a herb that vanished without being
     * picked would still be offered to the hand.
     */
    struct GrassClearing {
        /** Past this a box is a mistake: a clearing is a footprint, not a field. */
        static constexpr float kMaxHalfSize = 128.0F;

        /** Under this a box holds no plant the sector's own position grid could tell apart. */
        static constexpr float kMinHalfSize = 0.01F;

        glm::vec3 center {0.0F};

        /** Half the box along its own X, its own Y, and up. */
        glm::vec3 halfSize {0.0F};

        /**
         * Degrees about the vertical, counter-clockwise seen from above: the
         * box's X axis turned from the world's. Kept in degrees, as a map
         * file and a script spell it, so a box read back is the box written
         * to the last bit -- it is its own key.
         */
        float yawDegrees = 0.0F;

        bool IsSane() const {
            const auto inRange = [](float half) {
                return std::isfinite(half) && half >= kMinHalfSize && half <= kMaxHalfSize;
            };
            return std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z) && std::isfinite(yawDegrees) && inRange(halfSize.x) && inRange(halfSize.y) && inRange(halfSize.z);
        }

        /** Whether a plant rooted at `point` is in the box, its faces included. */
        bool Contains(const glm::vec3 &point) const {
            return Contains(point, std::sin(glm::radians(yawDegrees)), std::cos(glm::radians(yawDegrees)));
        }

        /** The same test for a caller weighing thousands of plants against one box, which takes the turn's sine and cosine once. */
        bool Contains(const glm::vec3 &point, float sine, float cosine) const {
            const float x = point.x - center.x, y = point.y - center.y;
            return std::fabs(x * cosine + y * sine) <= halfSize.x && std::fabs(y * cosine - x * sine) <= halfSize.y && std::fabs(point.z - center.z) <= halfSize.z;
        }

        /** The world-aligned box around it, which is what a sector is first weighed against. */
        void Bounds(glm::vec3 &min, glm::vec3 &max) const {
            const float sine = std::fabs(std::sin(glm::radians(yawDegrees))), cosine = std::fabs(std::cos(glm::radians(yawDegrees)));
            const glm::vec3 reach {halfSize.x * cosine + halfSize.y * sine, halfSize.x * sine + halfSize.y * cosine, halfSize.z};
            min = center - reach;
            max = center + reach;
        }

        bool operator==(const GrassClearing &other) const = default;
    };
} // namespace KCDC::Shared::Game
