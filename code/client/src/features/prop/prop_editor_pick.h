#pragma once

#include "sdk/types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace KCDC::Features::Prop {
    /** Slack past a solid surface, so an object lying flush on it still counts as in front of it. */
    inline constexpr float kPickInFrontSlack = 0.05F;

    /** Boxes the ray enters this close together tie, and the smaller wins: the candle, not the canopy over it. */
    inline constexpr float kPickBoxTieMetres = 0.25F;

    /**
     * One object the editor placed, as a click weighs it: the mesh's own box
     * and the transform standing it in the world, which together are the box
     * turned and scaled with the object rather than the world-aligned box
     * around it. A smart object, built from several parts, carries its world
     * box under an identity transform.
     */
    struct PlacedPickBox {
        std::uint64_t id = 0;
        SDK::AABB localBounds {};
        SDK::Matrix34 matrix {};
        /** It has a collider, so the collision ray speaks for it before its box does. */
        bool solid = false;
    };

    /** What the collision ray struck before the boxes are asked. */
    struct PickSurface {
        bool hit       = false; // something solid within reach: terrain, the level, or one of the editor's objects
        float distance = 0.0F;
        SDK::Vec3 point {};
        std::uint64_t placed = 0; // the editor's object that collider belongs to, or 0
    };

    enum class PlacedPickMethod : std::uint8_t {
        None,
        Collision, // the ray struck the object's own collider
        Bounds,    // the ray entered the object's oriented box
    };

    struct PlacedPick {
        std::uint64_t id        = 0;
        float distance          = 0.0F;
        PlacedPickMethod method = PlacedPickMethod::None;
    };

    /** The inverse of an affine transform. False for one that flattens space, which has none. */
    inline bool InvertAffine(const SDK::Matrix34 &matrix, SDK::Matrix34 &inverse) {
        const float a = matrix.m[0][0], b = matrix.m[0][1], c = matrix.m[0][2];
        const float d = matrix.m[1][0], e = matrix.m[1][1], f = matrix.m[1][2];
        const float g = matrix.m[2][0], h = matrix.m[2][1], i = matrix.m[2][2];

        const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12F) {
            return false;
        }
        const float scale = 1.0F / determinant;

        inverse.m[0][0] = (e * i - f * h) * scale;
        inverse.m[0][1] = (c * h - b * i) * scale;
        inverse.m[0][2] = (b * f - c * e) * scale;
        inverse.m[1][0] = (f * g - d * i) * scale;
        inverse.m[1][1] = (a * i - c * g) * scale;
        inverse.m[1][2] = (c * d - a * f) * scale;
        inverse.m[2][0] = (d * h - e * g) * scale;
        inverse.m[2][1] = (b * g - a * h) * scale;
        inverse.m[2][2] = (a * e - b * d) * scale;
        for (int row = 0; row < 3; ++row) {
            inverse.m[row][3] = -(inverse.m[row][0] * matrix.m[0][3] + inverse.m[row][1] * matrix.m[1][3] + inverse.m[row][2] * matrix.m[2][3]);
        }
        return true;
    }

    /** The volume a box takes up in the world once `matrix` has turned and scaled it. */
    inline float OrientedBoxVolume(const SDK::AABB &localBounds, const SDK::Matrix34 &matrix) {
        const float a = matrix.m[0][0], b = matrix.m[0][1], c = matrix.m[0][2];
        const float d = matrix.m[1][0], e = matrix.m[1][1], f = matrix.m[1][2];
        const float g = matrix.m[2][0], h = matrix.m[2][1], i = matrix.m[2][2];
        const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        return std::max(localBounds.max.x - localBounds.min.x, 0.0F) * std::max(localBounds.max.y - localBounds.min.y, 0.0F) * std::max(localBounds.max.z - localBounds.min.z, 0.0F) * std::fabs(determinant);
    }

    /** Whether a world point lies in the oriented box, widened by `slack` metres along each of its own axes. */
    inline bool OrientedBoxContains(const SDK::AABB &localBounds, const SDK::Matrix34 &matrix, const SDK::Vec3 &point, float slack) {
        SDK::Matrix34 inverse {};
        if (!InvertAffine(matrix, inverse)) {
            return false;
        }
        const SDK::Vec3 local = inverse.TransformPoint(point);
        // The slack is world metres; each local axis is that many metres long per unit.
        const float axisLength[3] {std::sqrt(matrix.m[0][0] * matrix.m[0][0] + matrix.m[1][0] * matrix.m[1][0] + matrix.m[2][0] * matrix.m[2][0]), std::sqrt(matrix.m[0][1] * matrix.m[0][1] + matrix.m[1][1] * matrix.m[1][1] + matrix.m[2][1] * matrix.m[2][1]),
            std::sqrt(matrix.m[0][2] * matrix.m[0][2] + matrix.m[1][2] * matrix.m[1][2] + matrix.m[2][2] * matrix.m[2][2])};
        const float localPoint[3] {local.x, local.y, local.z};
        const float boxMin[3] {localBounds.min.x, localBounds.min.y, localBounds.min.z};
        const float boxMax[3] {localBounds.max.x, localBounds.max.y, localBounds.max.z};
        for (int axis = 0; axis < 3; ++axis) {
            const float localSlack = axisLength[axis] > 1e-6F ? slack / axisLength[axis] : 0.0F;
            if (localPoint[axis] < boxMin[axis] - localSlack || localPoint[axis] > boxMax[axis] + localSlack) {
                return false;
            }
        }
        return true;
    }

    /**
     * Where a ray enters a box standing under `matrix`, in metres along a
     * unit `direction`, or a negative number when it misses. A ray starting
     * inside reports zero and sets `inside`.
     *
     * The ray is taken into the box's own space, where the box is
     * axis-aligned. An affine map keeps a point's parameter along the ray,
     * so the local direction is left unnormalised and the answer is still in
     * world metres.
     */
    inline float RayOrientedBoxEntry(const SDK::Vec3 &origin, const SDK::Vec3 &direction, const SDK::AABB &localBounds, const SDK::Matrix34 &matrix, bool &inside) {
        inside = false;
        SDK::Matrix34 inverse {};
        if (!InvertAffine(matrix, inverse)) {
            return -1.0F;
        }
        const SDK::Vec3 localOrigin = inverse.TransformPoint(origin);
        const SDK::Vec3 localDirection {inverse.m[0][0] * direction.x + inverse.m[0][1] * direction.y + inverse.m[0][2] * direction.z, inverse.m[1][0] * direction.x + inverse.m[1][1] * direction.y + inverse.m[1][2] * direction.z,
            inverse.m[2][0] * direction.x + inverse.m[2][1] * direction.y + inverse.m[2][2] * direction.z};
        inside = localOrigin.x >= localBounds.min.x && localOrigin.x <= localBounds.max.x && localOrigin.y >= localBounds.min.y && localOrigin.y <= localBounds.max.y && localOrigin.z >= localBounds.min.z && localOrigin.z <= localBounds.max.z;
        return SDK::RayBoxEntryDistance(localOrigin, localDirection, localBounds);
    }

    /**
     * Which of the editor's objects a click along a ray selects, given what
     * the collision ray struck.
     *
     * A collider is the nearest thing the editor has to the surface the
     * cursor is on, so it decides for every solid object: one is selected
     * where the ray strikes its collision, and never because the ray merely
     * crossed the box around it.
     *
     * A box is asked only where there is no collision to ask:
     *
     * - An object without a collider is offered when the ray enters its
     *   oriented box in front of the struck surface. When that surface is a
     *   smaller object of the editor's, the smaller one was aimed at and
     *   keeps the click.
     * - A solid object whose collision does not cover its mesh -- a tree is
     *   solid at the trunk only -- is offered by its oriented box when the
     *   ray struck none of the editor's objects, unless the struck point
     *   lies in that box: it then stands on or around what was clicked, as
     *   the ground under a crown does.
     *
     * A box the ray starts inside is never offered: the camera is in it, so
     * it would take every click. Boxes entered within `kPickBoxTieMetres` of
     * each other tie and the smaller wins.
     */
    inline PlacedPick ChoosePlacedPick(const std::vector<PlacedPickBox> &boxes, const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, const PickSurface &surface) {
        const float limit = surface.hit ? std::min(surface.distance + kPickInFrontSlack, maxDistance) : maxDistance;

        std::uint64_t chosen = 0;
        float chosenDistance = 0.0F;
        float nearest        = limit;
        float nearestVolume  = std::numeric_limits<float>::max();
        float struckVolume   = std::numeric_limits<float>::max();

        for (const PlacedPickBox &box : boxes) {
            if (box.id == surface.placed) {
                if (surface.placed != 0) {
                    struckVolume = OrientedBoxVolume(box.localBounds, box.matrix);
                }
                continue;
            }
            if (box.solid && (surface.placed != 0 || (surface.hit && OrientedBoxContains(box.localBounds, box.matrix, surface.point, kPickInFrontSlack)))) {
                continue;
            }

            bool inside          = false;
            const float distance = RayOrientedBoxEntry(origin, direction, box.localBounds, box.matrix, inside);
            if (distance < 0.0F || inside || distance > limit || distance > nearest + kPickBoxTieMetres) {
                continue;
            }
            const float volume = OrientedBoxVolume(box.localBounds, box.matrix);
            if (distance > nearest - kPickBoxTieMetres && volume >= nearestVolume) {
                continue;
            }

            nearest        = std::min(nearest, distance);
            nearestVolume  = volume;
            chosen         = box.id;
            chosenDistance = distance;
        }

        if (chosen != 0 && (surface.placed == 0 || nearestVolume <= struckVolume)) {
            return PlacedPick {chosen, chosenDistance, PlacedPickMethod::Bounds};
        }
        if (surface.placed != 0) {
            return PlacedPick {surface.placed, surface.distance, PlacedPickMethod::Collision};
        }
        return PlacedPick {};
    }
} // namespace KCDC::Features::Prop
