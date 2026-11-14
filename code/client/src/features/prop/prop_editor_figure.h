#pragma once

#include "sdk/types.h"

#include <cmath>
#include <memory>
#include <string>

namespace KCDC::Features::Prop {
    /** Roughly where an adult's eyes are above its feet, in metres: where the eye view puts the camera. */
    inline constexpr float kFigureEyeHeight = 1.65F;

    /** How far in front of the figure's face the eye view stands, so the camera is not inside the head. */
    inline constexpr float kFigureEyeLead = 0.3F;

    /**
     * The rotation a body takes to face `yawDegrees`. The yaw is the editor
     * camera's own: counter-clockwise from +Y seen from above, which is the
     * engine's turn about +Z.
     */
    inline SDK::Quat FigureRotation(float yawDegrees) {
        const float half = yawDegrees * 0.5F * 3.14159265358979F / 180.0F;
        return SDK::Quat {0.0F, 0.0F, std::sin(half), std::cos(half)};
    }

    /** Where the eye view stands for a figure at `position` facing `yawDegrees`. */
    inline SDK::Vec3 FigureEyePoint(const SDK::Vec3 &position, float yawDegrees) {
        const float yaw = yawDegrees * 3.14159265358979F / 180.0F;
        return SDK::Vec3 {position.x - std::sin(yaw) * kFigureEyeLead, position.y + std::cos(yaw) * kFigureEyeLead, position.z + kFigureEyeHeight};
    }

    /**
     * A person standing in the scene to judge its proportions by: a
     * temporary NPC of one of the shipped roles, held still where it was
     * put. It belongs to the editing session, not to the project -- nothing
     * saves or exports it -- and it stays through F7, so the scene can be
     * walked with it standing there.
     *
     * The game keeps one preview body, which the patrol preview uses too:
     * whoever owns both stops one before starting the other. Game thread.
     */
    class EditorFigure final {
      public:
        EditorFigure();
        ~EditorFigure();

        /** Stands a figure of `role` with its feet at `position`, replacing the one standing. False for an unknown role or a place that is not one. */
        bool Place(const std::string &role, const SDK::Vec3 &position, float yawDegrees);

        /** Takes the figure away. `worldGone` forgets the body without touching it, for a level that has unloaded. */
        void Remove(bool worldGone = false);

        /** Every tick while a level is loaded, editor open or not. */
        void Update();

        /** Asked for and not removed; the body may still be coming. */
        bool Active() const;

        /** The body is up and dressed. */
        bool Standing() const;

        const std::string &Role() const;
        const SDK::Vec3 &Position() const;
        float YawDegrees() const;
        const std::string &Status() const;

      private:
        struct State;
        std::unique_ptr<State> _state;
    };
} // namespace KCDC::Features::Prop
